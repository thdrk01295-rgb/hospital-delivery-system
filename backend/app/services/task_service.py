"""
Task service — creates, queries, and updates tasks.
"""
from datetime import datetime, timezone
from typing import Optional

from sqlalchemy.orm import Session

from app.models.task import Task
from app.models.location import Location
from app.constants.enums import TaskStatus, TaskType, RequestedByRole
from app.schemas.task import NurseOrderCreate, PatientClothingRequestCreate


def create_nurse_task(db: Session, body: NurseOrderCreate, nurse_id: str = "nurse") -> Task:
    task = Task(
        task_type=body.task_type,
        origin_location_id=body.origin_location_id,
        destination_location_id=body.destination_location_id,
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
    Origin is resolved from the patient's bed_code.
    Destination is the station (for return) or warehouse (for rental).
    """
    # Resolve origin: patient's bed
    origin = db.query(Location).filter(Location.location_code == bed_code).first()
    # Resolve destination based on task type
    if body.task_type == TaskType.PATIENT_CLOTHES_RETURN:
        dest = db.query(Location).filter(Location.location_type == "laundry").first()
    else:
        dest = db.query(Location).filter(Location.location_type == "warehouse").first()

    task = Task(
        task_type=body.task_type,
        origin_location_id=origin.id if origin else None,
        destination_location_id=dest.id if dest else None,
        patient_bed_code=bed_code,
        requested_by_role=RequestedByRole.PATIENT,
        requested_by_user=bed_code,
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
