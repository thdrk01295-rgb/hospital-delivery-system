"""
MQTT message handlers.
Each handler updates the database and broadcasts a WebSocket event.
Async WebSocket broadcasts are scheduled via asyncio.run_coroutine_threadsafe.
"""
import asyncio
import logging

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
        mqtt_topics.ROBOT_STATUS:       _handle_robot_status,
        mqtt_topics.ROBOT_LOCATION:     _handle_robot_location,
        mqtt_topics.ROBOT_BATTERY:      _handle_robot_battery,
        mqtt_topics.ROBOT_ERROR:        _handle_robot_error,
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
    from app.websocket.manager import ws_manager
    from app.scheduler.dispatcher import maybe_dispatch

    data = MqttRobotStatusPayload(**raw)
    db = SessionLocal()
    try:
        robot = update_robot_state(db, data.robot_id, data.state)
        status_dict = get_robot_status_dict(robot)
        _schedule(ws_manager.broadcast(ws_events.ROBOT_STATE_UPDATE, status_dict))

        # If robot just became IDLE (finished a task), try dispatching the next task.
        if data.state == RobotState.IDLE:
            _schedule(maybe_dispatch(db))
    finally:
        db.close()


def _handle_robot_location(raw: dict) -> None:
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

    data = MqttRobotBatteryPayload(**raw)
    db = SessionLocal()
    try:
        robot = update_robot_battery(db, data.robot_id, data.battery_percent)
        status_dict = get_robot_status_dict(robot)
        _schedule(ws_manager.broadcast(ws_events.ROBOT_BATTERY_UPDATE, status_dict))
    finally:
        db.close()


def _handle_robot_error(raw: dict) -> None:
    from app.db.session import SessionLocal
    from app.services.robot_service import update_robot_state, get_robot_status_dict
    from app.websocket.manager import ws_manager

    data = MqttRobotErrorPayload(**raw)
    db = SessionLocal()
    try:
        robot = update_robot_state(db, data.robot_id, RobotState.ERROR)
        status_dict = get_robot_status_dict(robot)
        _schedule(ws_manager.broadcast(ws_events.ROBOT_STATE_UPDATE, status_dict))
    finally:
        db.close()


def _handle_task_complete(raw: dict) -> None:
    from app.db.session import SessionLocal
    from app.services.task_service import update_task_status
    from app.schemas.task import TaskRead
    from app.websocket.manager import ws_manager

    data = MqttTaskCompletePayload(**raw)
    db = SessionLocal()
    try:
        task = update_task_status(db, data.task_id, TaskStatus.COMPLETE)
        if task:
            task_dict = TaskRead.model_validate(task).model_dump(mode="json")
            _schedule(ws_manager.broadcast(ws_events.TASK_STATUS_UPDATE, task_dict))
    finally:
        db.close()
