"""
MQTT message handlers.
Each handler updates the database and broadcasts a WebSocket event.
Async WebSocket broadcasts are scheduled via asyncio.run_coroutine_threadsafe.
"""
import asyncio
import logging
from datetime import datetime, timezone

from app.constants import mqtt_topics, ws_events
from app.schemas.robot import (
    MqttRobotStatusPayload,
    MqttRobotLocationPayload,
    MqttRobotBatteryPayload,
    MqttRobotErrorPayload,
    MqttTaskCompletePayload,
)
from app.constants.enums import RobotState, TaskStatus

logger = logging.getLogger(__name__)

# The running asyncio event loop — set from main.py at startup
_loop: asyncio.AbstractEventLoop | None = None


def set_event_loop(loop: asyncio.AbstractEventLoop) -> None:
    global _loop
    _loop = loop


def _schedule(coro):
    if _loop:
        asyncio.run_coroutine_threadsafe(coro, _loop)


def dispatch_message(topic: str, payload: dict) -> None:
    handlers = {
        mqtt_topics.ROBOT_STATUS:        _handle_robot_status,
        mqtt_topics.ROBOT_LOCATION:      _handle_robot_location,
        mqtt_topics.ROBOT_BATTERY:       _handle_robot_battery,
        mqtt_topics.ROBOT_ERROR:         _handle_robot_error,
        mqtt_topics.ROBOT_TASK_COMPLETE: _handle_task_complete,
    }
    handler = handlers.get(topic)
    if handler:
        handler(payload)
    else:
        logger.warning(f"No handler for MQTT topic: {topic}")


def _handle_robot_status(raw: dict) -> None:
    from app.db.session import SessionLocal
    from app.services.robot_service import update_robot_state, get_robot_status_dict
    from app.services.abnormal_event_service import resolve_all_by_type
    from app.websocket.manager import ws_manager
    from app.scheduler.dispatcher import maybe_dispatch

    # Temporary: normalize legacy misspelled value from older robot clients
    if raw.get("state") == "CHAGING_BATTERY":
        raw = {**raw, "state": "CHARGING_BATTERY"}

    data = MqttRobotStatusPayload(**raw)
    db = SessionLocal()
    try:
        robot = update_robot_state(db, data.robot_id, data.state)
        status_dict = get_robot_status_dict(robot)
        _schedule(ws_manager.broadcast(ws_events.ROBOT_STATE_UPDATE, status_dict))

        # NOTE: LOW_BATTERY state is no longer triggered here.
        # Low-battery handling (event + task requeue + station-return dispatch)
        # is now driven entirely by robot/battery with battery_percent <= threshold.

        # Robot returned to IDLE → resolve open error / low_battery events and dispatch
        if data.state == RobotState.IDLE:
            for etype in ("error", "low_battery"):
                resolved = resolve_all_by_type(db, etype)
                if resolved:
                    _schedule(ws_manager.broadcast(
                        ws_events.ABNORMAL_EVENT_UPDATE,
                        {"event_type": etype, "active": False},
                    ))
            _schedule(maybe_dispatch(db))
    finally:
        db.close()


def _handle_robot_location(raw: dict) -> None:
    """
    Deprecated: robot/location is no longer required in the MQTT contract.
    The server now infers robot current location from robot/task_complete destination.
    This handler is retained for backward compatibility if the robot still publishes it.
    """
    from app.db.session import SessionLocal
    from app.services.robot_service import update_robot_location, get_robot_status_dict
    from app.websocket.manager import ws_manager

    data = MqttRobotLocationPayload(**raw)
    db = SessionLocal()
    try:
        robot = update_robot_location(db, data.robot_id, data.location_code)
        status_dict = get_robot_status_dict(robot)
        _schedule(ws_manager.broadcast(ws_events.ROBOT_LOCATION_UPDATE, status_dict))
    finally:
        db.close()


def _handle_robot_battery(raw: dict) -> None:
    from app.db.session import SessionLocal
    from app.services.robot_service import update_robot_battery, get_robot_status_dict
    from app.websocket.manager import ws_manager
    from app.config.settings import settings

    data = MqttRobotBatteryPayload(**raw)
    db = SessionLocal()
    try:
        robot = update_robot_battery(db, data.robot_id, data.battery_percent)
        status_dict = get_robot_status_dict(robot)
        _schedule(ws_manager.broadcast(ws_events.ROBOT_BATTERY_UPDATE, status_dict))

        if data.battery_percent <= settings.LOW_BATTERY_THRESHOLD:
            _handle_low_battery(db, robot)
    finally:
        db.close()


def _handle_low_battery(db, robot) -> None:
    """
    Idempotent low-battery handling.
    Triggered when battery_percent <= LOW_BATTERY_THRESHOLD from robot/battery.

    Actions (each guarded for idempotency):
      1. Set robot state to LOW_BATTERY (skip if already LOW_BATTERY or CHARGING_BATTERY)
      2. Open low_battery AbnormalEvent (open_event is already idempotent)
      3. Requeue any active DISPATCHED/IN_PROGRESS task; publish server/task_cancel for it
      4. Dispatch a BATTERY_LOW station-return task to STATION-01 (skip if already exists)
    """
    from app.models.task import Task
    from app.models.location import Location
    from app.services.abnormal_event_service import open_event
    from app.services.task_service import requeue_task
    from app.services.robot_service import get_robot_status_dict
    from app.websocket.manager import ws_manager
    from app.constants.enums import TaskType, RequestedByRole
    from app.schemas.task import TaskRead
    from app.mqtt.client import publish

    # --- 1. Set state to LOW_BATTERY (do not override CHARGING_BATTERY) ---
    if robot.current_state not in (RobotState.LOW_BATTERY, RobotState.CHARGING_BATTERY):
        robot.current_state = RobotState.LOW_BATTERY
        robot.last_seen_at = datetime.now(timezone.utc)
        db.commit()
        db.refresh(robot)
        _schedule(ws_manager.broadcast(ws_events.ROBOT_STATE_UPDATE, get_robot_status_dict(robot)))

    # --- 2. Open low_battery event (idempotent) ---
    event = open_event(db, "low_battery", note=f"Battery at {robot.battery_percent:.1f}%")
    _schedule(ws_manager.broadcast(
        ws_events.ABNORMAL_EVENT_UPDATE,
        {"event_type": "low_battery", "event_id": event.id, "active": True},
    ))

    # --- 3. Requeue any active task assigned to this robot ---
    active_task = (
        db.query(Task)
        .filter(
            Task.status.in_([TaskStatus.DISPATCHED, TaskStatus.IN_PROGRESS]),
            Task.assigned_robot_id == robot.id,
        )
        .first()
    )
    if active_task:
        publish(mqtt_topics.SERVER_TASK_CANCEL, {"robot_id": robot.robot_code, "task_id": active_task.id})
        requeued = requeue_task(db, active_task.id)
        if requeued:
            task_dict = TaskRead.model_validate(requeued).model_dump(mode="json")
            _schedule(ws_manager.broadcast(ws_events.TASK_STATUS_UPDATE, task_dict))

    # --- 4. Dispatch station-return task (idempotent: skip if already exists) ---
    existing_return = (
        db.query(Task)
        .filter(
            Task.task_type == TaskType.BATTERY_LOW,
            Task.status.in_([TaskStatus.PENDING, TaskStatus.DISPATCHED, TaskStatus.IN_PROGRESS]),
        )
        .first()
    )
    if existing_return:
        return

    station = db.query(Location).filter(Location.location_code == "STATION-01").first()
    if not station:
        logger.warning("STATION-01 not found in locations table — cannot dispatch low-battery station-return task")
        return

    return_task = Task(
        task_type=TaskType.BATTERY_LOW,
        destination_location_id=station.id,
        origin_location_id=robot.current_location_id,
        requested_by_role=RequestedByRole.SYSTEM,
        priority=Task.resolve_priority(TaskType.BATTERY_LOW),
        status=TaskStatus.DISPATCHED,
        assigned_robot_id=robot.id,
        note="Auto: low-battery station return",
    )
    db.add(return_task)
    db.commit()
    db.refresh(return_task)

    origin_code = return_task.origin_location.location_code if return_task.origin_location else None
    publish(mqtt_topics.SERVER_TASK_ASSIGN, {
        "robot_id": robot.robot_code,
        "task_id": return_task.id,
        "task_type": return_task.task_type,
        "origin": origin_code,
        "destination": station.location_code,
        "priority": return_task.priority,
    })

    from app.schemas.task import TaskRead as TR
    task_dict = TR.model_validate(return_task).model_dump(mode="json")
    _schedule(ws_manager.broadcast(ws_events.TASK_STATUS_UPDATE, task_dict))
    logger.info(f"Low-battery station-return task {return_task.id} dispatched to {station.location_code}")


def _handle_robot_error(raw: dict) -> None:
    from app.db.session import SessionLocal
    from app.services.robot_service import update_robot_state, get_robot_status_dict
    from app.services.abnormal_event_service import open_event
    from app.websocket.manager import ws_manager

    data = MqttRobotErrorPayload(**raw)
    db = SessionLocal()
    try:
        robot = update_robot_state(db, data.robot_id, RobotState.ERROR)
        status_dict = get_robot_status_dict(robot)
        _schedule(ws_manager.broadcast(ws_events.ROBOT_STATE_UPDATE, status_dict))

        event = open_event(db, "error", note=data.error_message)
        _schedule(ws_manager.broadcast(
            ws_events.ABNORMAL_EVENT_UPDATE,
            {"event_type": "error", "event_id": event.id, "active": True,
             "note": data.error_message},
        ))
    finally:
        db.close()


def _handle_task_complete(raw: dict) -> None:
    """
    Handles robot/task_complete:
      1. Mark task COMPLETE in DB.
      2. Infer robot current location from the completed task's destination.
      3. Broadcast task_status_update and robot_location_update (if location changed).
    """
    from app.db.session import SessionLocal
    from app.services.task_service import update_task_status
    from app.services.robot_service import get_or_create_robot, get_robot_status_dict
    from app.schemas.task import TaskRead
    from app.websocket.manager import ws_manager

    data = MqttTaskCompletePayload(**raw)
    db = SessionLocal()
    try:
        task = update_task_status(db, data.task_id, TaskStatus.COMPLETE)
        if not task:
            logger.warning(f"robot/task_complete: task {data.task_id} not found")
            return

        task_dict = TaskRead.model_validate(task).model_dump(mode="json")
        _schedule(ws_manager.broadcast(ws_events.TASK_STATUS_UPDATE, task_dict))

        # Infer robot current location from the completed task's destination
        if task.destination_location_id:
            robot = get_or_create_robot(db, data.robot_id)
            if robot.current_location_id != task.destination_location_id:
                robot.current_location_id = task.destination_location_id
                robot.last_seen_at = datetime.now(timezone.utc)
                db.commit()
                db.refresh(robot)
                status_dict = get_robot_status_dict(robot)
                _schedule(ws_manager.broadcast(ws_events.ROBOT_LOCATION_UPDATE, status_dict))
    finally:
        db.close()
