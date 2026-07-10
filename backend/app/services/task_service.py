"""
Task service — creates, queries, and updates tasks.
"""
import logging
from datetime import datetime, timezone
from typing import Optional

from sqlalchemy.orm import Session

logger = logging.getLogger(__name__)

from app.models.task import Task
from app.models.location import Location
from app.models.robot import Robot
from app.constants.enums import TaskStatus, TaskType, RequestedByRole
from app.schemas.task import NurseOrderCreate, PatientClothingRequestCreate

# Task types where origin must always be null (robot doesn't start from a specific stop)
_NO_ORIGIN_NURSE_TYPES: set[str] = {
    TaskType.KIT_DELIVERY,
    TaskType.KIT_REFILL,
    TaskType.CLOTHES_REFILL,
}

# Task types with a fixed server-resolved destination (nurse doesn't choose)
_FIXED_DEST: dict[str, str] = {
    TaskType.KIT_REFILL:    "WAREHOUSE-01",
    TaskType.CLOTHES_REFILL: "LAUNDRY-01",
}


def create_nurse_task(db: Session, body: NurseOrderCreate, nurse_id: str = "nurse") -> Task:
    origin_id = None if body.task_type in _NO_ORIGIN_NURSE_TYPES else body.origin_location_id

    # Resolve fixed destination for types that always go to a known location
    dest_id = body.destination_location_id
    if body.task_type in _FIXED_DEST:
        loc_code = _FIXED_DEST[body.task_type]
        loc = db.query(Location).filter(Location.location_code == loc_code).first()
        dest_id = loc.id if loc else None

    task = Task(
        task_type=body.task_type,
        origin_location_id=origin_id,
        destination_location_id=dest_id,
        requested_by_role=RequestedByRole.NURSE,
        requested_by_user=nurse_id,
        priority=Task.resolve_priority(body.task_type),
        status=TaskStatus.PENDING,
        note=body.note,
        order_top=body.order_top,
        order_bottom=body.order_bottom,
        order_bedding=body.order_bedding,
        order_other=body.order_other,
    )
    db.add(task)
    db.commit()
    db.refresh(task)
    return task


def create_patient_task(db: Session, body: PatientClothingRequestCreate,
                        bed_code: str) -> Task:
    """
    Creates a patient clothes rental or return task.
    Destination = patient's bed.  Origin = null (robot starts from wherever it is).
    """
    dest = db.query(Location).filter(Location.location_code == bed_code).first()

    task = Task(
        task_type=body.task_type,
        origin_location_id=None,
        destination_location_id=dest.id if dest else None,
        patient_bed_code=bed_code,
        requested_by_role=RequestedByRole.PATIENT,
        requested_by_user=bed_code,
        priority=Task.resolve_priority(body.task_type),
        status=TaskStatus.PENDING,
        note=body.note,
        order_top=body.order_top,
        order_bottom=body.order_bottom,
    )
    db.add(task)
    db.commit()
    db.refresh(task)
    return task


def get_patient_active_task(db: Session, bed_code: str) -> Optional[Task]:
    """
    Returns the single active task for a patient identified by bed_code.
    Used by GET /tasks/patient/me — server resolves identity from auth token.
    Active = PENDING | DISPATCHED | IN_PROGRESS.
    """
    return (
        db.query(Task)
        .filter(
            Task.patient_bed_code == bed_code,
            Task.status.in_([TaskStatus.PENDING, TaskStatus.DISPATCHED, TaskStatus.IN_PROGRESS]),
        )
        .order_by(Task.created_at.desc())
        .first()
    )


def get_ongoing_tasks(db: Session) -> list[Task]:
    return (
        db.query(Task)
        .filter(Task.status.in_([TaskStatus.PENDING, TaskStatus.DISPATCHED, TaskStatus.IN_PROGRESS]))
        .order_by(Task.priority.asc(), Task.created_at.asc())
        .all()
    )


def get_completed_tasks(db: Session, limit: int = 50) -> list[Task]:
    return (
        db.query(Task)
        .filter(Task.status.in_([TaskStatus.COMPLETE, TaskStatus.CANCELLED, TaskStatus.FAILED]))
        .order_by(Task.completed_at.desc())
        .limit(limit)
        .all()
    )


def update_task_status(db: Session, task_id: int, status: TaskStatus,
                       robot_id: Optional[int] = None) -> Optional[Task]:
    task = db.query(Task).filter(Task.id == task_id).first()
    if not task:
        return None
    task.status = status
    if robot_id is not None:
        task.assigned_robot_id = robot_id
    if status == TaskStatus.IN_PROGRESS and not task.started_at:
        task.started_at = datetime.now(timezone.utc)
    if status in (TaskStatus.COMPLETE, TaskStatus.CANCELLED, TaskStatus.FAILED):
        task.completed_at = datetime.now(timezone.utc)
    db.commit()
    db.refresh(task)
    return task


def finalize_task_complete(db: Session, task_id: int,
                           robot_id: Optional[int] = None) -> Optional[Task]:
    """
    Atomically marks a task COMPLETE and updates robot inventory in one transaction.

    Idempotency: if task.inventory_applied is already True the function returns
    the existing row without re-applying any mutations, making it safe to call
    from duplicate MQTT messages or retried HTTP requests.

    Raises ValueError if an inventory mutation would make a count negative;
    the transaction is NOT committed in that case.
    """
    from app.services.robot_inventory_service import apply_inventory_effect_for_completed_task

    task = db.query(Task).filter(Task.id == task_id).first()
    if not task:
        return None

    # Idempotency guard — if already finalized, skip without re-mutating inventory
    if task.inventory_applied:
        logger.info(
            f"[finalize] task_id={task_id} already finalized "
            f"(inventory_applied=True) — returning cached result"
        )
        return task

    task.status = TaskStatus.COMPLETE
    task.inventory_applied = True
    if not task.started_at:
        task.started_at = datetime.now(timezone.utc)
    task.completed_at = datetime.now(timezone.utc)

    if robot_id is not None:
        # May raise ValueError; caller must NOT commit if this raises
        apply_inventory_effect_for_completed_task(db, robot_id, task)

    db.commit()
    db.refresh(task)
    return task


def requeue_task(db: Session, task_id: int) -> Optional[Task]:
    """Resets a DISPATCHED or IN_PROGRESS task back to PENDING for re-dispatch after emergency."""
    task = db.query(Task).filter(Task.id == task_id).first()
    if not task:
        return None
    task.status = TaskStatus.PENDING
    task.assigned_robot_id = None
    task.started_at = None
    db.commit()
    db.refresh(task)
    return task


def create_emergency_task(db: Session) -> Task:
    task = Task(
        task_type=TaskType.EMERGENCY_CALL,
        requested_by_role=RequestedByRole.NURSE,
        priority=Task.resolve_priority(TaskType.EMERGENCY_CALL),
        status=TaskStatus.PENDING,
        note="Emergency station call",
    )
    db.add(task)
    db.commit()
    db.refresh(task)
    return task
