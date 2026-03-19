"""
Task dispatcher.
Called whenever a robot becomes IDLE (via MQTT handler) to select and send the next task.

Priority rules (lower number = higher priority):
  1 — device abnormal (battery_low, emergency_call)
  2 — specimen_delivery
  3 — kit_delivery
  4 — logistics_delivery
  5 — clothes_refill
  6 — patient clothes rental / return
  7 — used_clothes_collection
"""
import json
import logging

from sqlalchemy.orm import Session

from app.constants.enums import TaskStatus, BLOCKING_ROBOT_STATES, ABNORMAL_ROBOT_STATES
from app.constants import mqtt_topics
from app.models.task import Task
from app.models.robot import Robot

logger = logging.getLogger(__name__)


async def maybe_dispatch(db: Session) -> None:
    """
    Selects the highest-priority PENDING task and dispatches it to the robot if available.
    This is an async function so it can be scheduled on the event loop from the MQTT thread.
    """
    robot = db.query(Robot).first()
    if not robot:
        return

    if robot.current_state in BLOCKING_ROBOT_STATES:
        logger.debug(f"Robot not available for dispatch (state={robot.current_state})")
        return

    # Fetch the next pending task by priority then creation time
    task = (
        db.query(Task)
        .filter(Task.status == TaskStatus.PENDING)
        .order_by(Task.priority.asc(), Task.created_at.asc())
        .first()
    )

    if not task:
        logger.debug("No pending tasks to dispatch")
        return

    # Mark as dispatched
    task.status = TaskStatus.DISPATCHED
    task.assigned_robot_id = robot.id
    db.commit()

    # Publish task assignment to MQTT
    _publish_task_assignment(robot, task)

    # Broadcast task update over WebSocket
    await _broadcast_task_update(task)

    logger.info(f"Dispatched task {task.id} ({task.task_type}) to robot {robot.robot_code}")


def _publish_task_assignment(robot: Robot, task: Task) -> None:
    from app.mqtt.client import publish

    origin_code = task.origin_location.location_code if task.origin_location else None
    dest_code = task.destination_location.location_code if task.destination_location else None

    payload = {
        "task_id": task.id,
        "task_type": task.task_type,
        "origin": origin_code,
        "destination": dest_code,
        "priority": task.priority,
    }
    publish(mqtt_topics.SERVER_TASK_ASSIGN, payload)


async def _broadcast_task_update(task: Task) -> None:
    from app.websocket.manager import ws_manager
    from app.constants import ws_events
    from app.schemas.task import TaskRead

    task_dict = TaskRead.model_validate(task).model_dump(mode="json")
    await ws_manager.broadcast(ws_events.TASK_STATUS_UPDATE, task_dict)


def check_auto_triggers(db: Session) -> None:
    """
    Checks inventory thresholds and creates system tasks automatically if needed.
    Called periodically (e.g. after each task completes or on a timer).
    """
    from app.config.settings import settings
    from app.models.inventory import ClothingInventory
    from app.constants.enums import TaskType, RequestedByRole
    from app.models.location import Location

    _check_used_clothes_threshold(db, settings.USED_CLOTHES_COLLECTION_THRESHOLD)
    _check_clean_clothes_threshold(db, settings.CLOTHES_REFILL_LOW_THRESHOLD)


def _check_used_clothes_threshold(db: Session, threshold: int) -> None:
    from app.models.inventory import ClothingInventory
    from app.constants.enums import TaskType, TaskStatus, RequestedByRole
    from app.models.location import Location

    over_threshold = (
        db.query(ClothingInventory)
        .filter(ClothingInventory.used_count >= threshold)
        .all()
    )
    for inv in over_threshold:
        # Only create a collection task if none is already pending for this location
        existing = (
            db.query(Task)
            .filter(
                Task.task_type == TaskType.USED_CLOTHES_COLLECTION,
                Task.origin_location_id == inv.location_id,
                Task.status.in_([TaskStatus.PENDING, TaskStatus.DISPATCHED, TaskStatus.IN_PROGRESS]),
            )
            .first()
        )
        if not existing:
            task = Task(
                task_type=TaskType.USED_CLOTHES_COLLECTION,
                origin_location_id=inv.location_id,
                requested_by_role=RequestedByRole.SYSTEM,
                priority=Task.resolve_priority(TaskType.USED_CLOTHES_COLLECTION),
                status=TaskStatus.PENDING,
                note=f"Auto-triggered: used_count={inv.used_count}",
            )
            db.add(task)
    db.commit()


def _check_clean_clothes_threshold(db: Session, threshold: int) -> None:
    from app.models.inventory import ClothingInventory
    from app.constants.enums import TaskType, TaskStatus, RequestedByRole
    from app.models.location import Location

    low_stock = (
        db.query(ClothingInventory)
        .filter(ClothingInventory.clean_count <= threshold)
        .all()
    )
    for inv in low_stock:
        existing = (
            db.query(Task)
            .filter(
                Task.task_type == TaskType.CLOTHES_REFILL,
                Task.destination_location_id == inv.location_id,
                Task.status.in_([TaskStatus.PENDING, TaskStatus.DISPATCHED, TaskStatus.IN_PROGRESS]),
            )
            .first()
        )
        if not existing:
            task = Task(
                task_type=TaskType.CLOTHES_REFILL,
                destination_location_id=inv.location_id,
                requested_by_role=RequestedByRole.SYSTEM,
                priority=Task.resolve_priority(TaskType.CLOTHES_REFILL),
                status=TaskStatus.PENDING,
                note=f"Auto-triggered: clean_count={inv.clean_count}",
            )
            db.add(task)
    db.commit()
