from fastapi import APIRouter, Depends, HTTPException, Header, status
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
)
from app.services.auth_service import decode_token
from app.constants.enums import TaskType

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
def create_order(
    body: NurseOrderCreate,
    authorization: Optional[str] = Header(default=None),
    db: Session = Depends(get_db),
):
    payload = _get_token_payload(authorization)
    if payload.get("role") != "nurse":
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Nurses only")
    task = create_nurse_task(db, body, nurse_id=payload.get("sub", "nurse"))
    return task


@router.post("/nurse/emergency", response_model=TaskRead, status_code=status.HTTP_201_CREATED)
def trigger_emergency(
    authorization: Optional[str] = Header(default=None),
    db: Session = Depends(get_db),
):
    payload = _get_token_payload(authorization)
    if payload.get("role") != "nurse":
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Nurses only")
    return create_emergency_task(db)


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


# ── Shared / nurse dashboard endpoints ──────────────────────────────────────

@router.get("/ongoing", response_model=list[TaskRead])
def ongoing_tasks(db: Session = Depends(get_db)):
    return get_ongoing_tasks(db)


@router.get("/completed", response_model=list[TaskRead])
def completed_tasks(db: Session = Depends(get_db)):
    return get_completed_tasks(db)
