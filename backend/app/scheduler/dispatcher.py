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
import logging

from sqlalchemy.orm import Session

from app.constants.enums import TaskStatus, BLOCKING_ROBOT_STATES
from app.constants import mqtt_topics
from app.models.task import Task
from app.models.robot import Robot

logger = logging.getLogger(__name__)


async def maybe_dispatch() -> None:
    """
    Selects the highest-priority PENDING task and dispatches it to the robot if available.

    Creates its own DB session so it is safe to call from the asyncio event loop, from
    MQTT callbacks (via run_coroutine_threadsafe), or directly from HTTP endpoints.
    Never shares a session with the caller — avoids the closed-session threading bug.
    """
    from app.db.session import SessionLocal
    from app.models.abnormal_event import AbnormalEvent

    db = SessionLocal()
    try:
        robot = db.query(Robot).first()
        if not robot:
            logger.info("[dispatch] Skip — no robot row in DB")
            return

        if robot.current_state in BLOCKING_ROBOT_STATES:
            logger.info(f"[dispatch] Skip — robot state={robot.current_state} (blocking)")
            return

        # Don't dispatch normal tasks while a low_battery event is unresolved.
        # Guards the edge case where the robot sends IDLE before battery recovers.
        active_low_bat = (
            db.query(AbnormalEvent)
            .filter(
                AbnormalEvent.event_type == "low_battery",
                AbnormalEvent.resolved_at.is_(None),
            )
            .first()
        )
        if active_low_bat:
            logger.info(f"[dispatch] Skip — unresolved low_battery event (id={active_low_bat.id})")
            return

        task = (
            db.query(Task)
            .filter(
                Task.status == TaskStatus.PENDING,
                Task.destination_location_id.isnot(None),
            )
            .order_by(Task.priority.asc(), Task.created_at.asc())
            .first()
        )

        if not task:
            logger.info("[dispatch] Skip — no PENDING tasks with a valid destination")
            return

        logger.info(
            f"[dispatch] Selecting task_id={task.id} type={task.task_type} "
            f"priority={task.priority} dest_id={task.destination_location_id}"
        )

        task.status = TaskStatus.DISPATCHED
        task.assigned_robot_id = robot.id
        db.commit()

        _publish_task_assignment(robot, task)
        await _broadcast_task_update(task)

        logger.info(
            f"[dispatch] Published server/task_assign: task_id={task.id} "
            f"type={task.task_type} robot={robot.robot_code}"
        )
    except Exception:
        logger.exception("[dispatch] Unexpected error in maybe_dispatch")
    finally:
        db.close()


def _publish_task_assignment(robot: Robot, task: Task) -> None:
    from app.mqtt.client import publish

    origin_code = task.origin_location.location_code if task.origin_location else None
    dest_code = task.destination_location.location_code if task.destination_location else None

    payload = {
        "robot_id": robot.robot_code,
        "task_id": task.id,
        "task_type": task.task_type,
        "origin": origin_code,    # location_code or null
        "destination": dest_code, # location_code; non-null enforced by dispatch filter
        "priority": task.priority,
    }
    logger.info(f"[dispatch] MQTT server/task_assign payload: {payload}")
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
