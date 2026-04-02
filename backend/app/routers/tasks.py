import asyncio

from fastapi import APIRouter, Depends, HTTPException, Header, status, BackgroundTasks
from sqlalchemy.orm import Session
from typing import Optional

from app.db.session import get_db
from app.schemas.task import NurseOrderCreate, PatientClothingRequestCreate, TaskRead
from app.services.task_service import (
    create_nurse_task,
    create_patient_task,
    get_patient_active_task,
    get_ongoing_tasks,
    get_completed_tasks,
    create_emergency_task,
    update_task_status,
)
from app.services.abnormal_event_service import open_event
from app.services.auth_service import decode_token
from app.constants.enums import TaskType, TaskStatus
from app.constants import ws_events
from app.websocket.manager import ws_manager

router = APIRouter(prefix="/tasks", tags=["tasks"])


def _get_token_payload(authorization: Optional[str]) -> dict:
    if not authorization or not authorization.startswith("Bearer "):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Missing token")
    try:
        return decode_token(authorization.split(" ", 1)[1])
    except Exception:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid token")


# ── Nurse endpoints ──────────────────────────────────────────────────────────

@router.post("/nurse/order", response_model=TaskRead, status_code=status.HTTP_201_CREATED)
async def create_order(
    body: NurseOrderCreate,
    authorization: Optional[str] = Header(default=None),
    db: Session = Depends(get_db),
):
    payload = _get_token_payload(authorization)
    if payload.get("role") != "nurse":
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Nurses only")
    task = create_nurse_task(db, body, nurse_id=payload.get("sub", "nurse"))
    task_dict = TaskRead.model_validate(task).model_dump(mode="json")
    await ws_manager.broadcast(ws_events.TASK_STATUS_UPDATE, task_dict)
    return task


@router.post("/nurse/emergency", response_model=TaskRead, status_code=status.HTTP_201_CREATED)
async def trigger_emergency(
    authorization: Optional[str] = Header(default=None),
    db: Session = Depends(get_db),
):
    payload = _get_token_payload(authorization)
    if payload.get("role") != "nurse":
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Nurses only")

    task = create_emergency_task(db)

    # Record an abnormal event for the emergency call
    event = open_event(db, "emergency_call", related_task_id=task.id,
                       note="Emergency station call triggered by nurse")
    await ws_manager.broadcast(
        ws_events.ABNORMAL_EVENT_UPDATE,
        {"event_type": "emergency_call", "event_id": event.id,
         "active": True, "related_task_id": task.id},
    )
    return task


# ── Patient endpoints ────────────────────────────────────────────────────────

@router.get("/patient/me", response_model=Optional[TaskRead])
def get_my_task(
    authorization: Optional[str] = Header(default=None),
    db: Session = Depends(get_db),
):
    """
    Returns the active task belonging to the logged-in patient.
    The server resolves the patient's bed/location from the auth token —
    the client sends no query parameters.
    Returns null (HTTP 200 with null body) when no active task exists.
    """
    payload = _get_token_payload(authorization)
    if payload.get("role") != "patient":
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Patients only")
    bed_code = payload.get("bed_code")
    return get_patient_active_task(db, bed_code)


@router.post("/patient/request", response_model=TaskRead, status_code=status.HTTP_201_CREATED)
def patient_clothing_request(
    body: PatientClothingRequestCreate,
    authorization: Optional[str] = Header(default=None),
    db: Session = Depends(get_db),
):
    payload = _get_token_payload(authorization)
    if payload.get("role") != "patient":
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Patients only")
    if body.task_type not in (TaskType.PATIENT_CLOTHES_RENTAL, TaskType.PATIENT_CLOTHES_RETURN):
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST,
                            detail="Invalid task type for patient request")
    bed_code = payload.get("bed_code")
    return create_patient_task(db, body, bed_code=bed_code)


@router.post("/{task_id}/complete", response_model=TaskRead)
async def patient_complete_task(
    task_id: int,
    authorization: Optional[str] = Header(default=None),
    db: Session = Depends(get_db),
):
    """
    Called by the patient when they press the 완료 button in DELIVERY_OPEN_PAT state.
    Marks the task COMPLETE and broadcasts task_status_update.
    """
    payload = _get_token_payload(authorization)
    if payload.get("role") != "patient":
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Patients only")

    bed_code = payload.get("bed_code")

    # Verify the task belongs to this patient
    from app.models.task import Task
    task = db.query(Task).filter(Task.id == task_id).first()
    if not task:
        raise HTTPException(status_code=404, detail="Task not found")
    if task.patient_bed_code != bed_code:
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN,
                            detail="Task does not belong to this patient")
    if task.status not in (TaskStatus.DISPATCHED, TaskStatus.IN_PROGRESS):
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST,
                            detail=f"Task cannot be completed from status '{task.status}'")

    task = update_task_status(db, task_id, TaskStatus.COMPLETE)
    task_dict = TaskRead.model_validate(task).model_dump(mode="json")
    await ws_manager.broadcast(ws_events.TASK_STATUS_UPDATE, task_dict)
    return task


@router.post("/patient/cancel", response_model=TaskRead)
async def patient_cancel_task(
    authorization: Optional[str] = Header(default=None),
    db: Session = Depends(get_db),
):
    """Cancels the patient's current active task (PENDING/DISPATCHED/IN_PROGRESS)."""
    payload = _get_token_payload(authorization)
    if payload.get("role") != "patient":
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Patients only")

    bed_code = payload.get("bed_code")
    from app.models.task import Task
    task = db.query(Task).filter(
        Task.patient_bed_code == bed_code,
        Task.status.in_([TaskStatus.PENDING, TaskStatus.DISPATCHED, TaskStatus.IN_PROGRESS]),
    ).order_by(Task.created_at.desc()).first()

    if not task:
        raise HTTPException(status_code=404, detail="No active task to cancel")

    task = update_task_status(db, task.id, TaskStatus.CANCELLED)
    task_dict = TaskRead.model_validate(task).model_dump(mode="json")
    await ws_manager.broadcast(ws_events.TASK_STATUS_UPDATE, task_dict)
    return task


# ── Shared / nurse dashboard endpoints ──────────────────────────────────────

@router.get("/ongoing", response_model=list[TaskRead])
def ongoing_tasks(db: Session = Depends(get_db)):
    return get_ongoing_tasks(db)


@router.get("/completed", response_model=list[TaskRead])
def completed_tasks(db: Session = Depends(get_db)):
    return get_completed_tasks(db)
