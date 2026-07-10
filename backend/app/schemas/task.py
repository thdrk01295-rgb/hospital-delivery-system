from datetime import datetime
from typing import Optional

from pydantic import BaseModel, model_validator

from app.constants.enums import TaskType, TaskStatus, RequestedByRole, TASK_PRIORITY
from app.schemas.location import LocationRead


# ── Request bodies ──────────────────────────────────────────────────────────

# Types that never require the nurse to supply a destination (server resolves it)
_AUTO_DEST_TYPES: set[TaskType] = {TaskType.KIT_REFILL, TaskType.CLOTHES_REFILL}
# Types that must never receive an origin from the nurse
_NO_ORIGIN_TYPES: set[TaskType] = {TaskType.KIT_DELIVERY, TaskType.KIT_REFILL, TaskType.CLOTHES_REFILL}


class NurseOrderCreate(BaseModel):
    task_type: TaskType
    origin_location_id: Optional[int] = None
    destination_location_id: Optional[int] = None
    note: Optional[str] = None
    order_top: Optional[int] = None
    order_bottom: Optional[int] = None
    order_bedding: Optional[int] = None
    order_other: Optional[int] = None

    @model_validator(mode="after")
    def check_locations(self) -> "NurseOrderCreate":
        # BATTERY_LOW is system-internal only — never user-creatable
        if self.task_type == TaskType.BATTERY_LOW:
            raise ValueError(
                "battery_low is a system-internal task type and cannot be created manually"
            )
        # Silently clear origin for types where it is not applicable
        if self.task_type in _NO_ORIGIN_TYPES:
            self.origin_location_id = None
        # Auto-dest types (kit_refill, clothes_refill) don't need a client-supplied destination
        dest_required = self.task_type not in _AUTO_DEST_TYPES and self.task_type != TaskType.EMERGENCY_CALL
        if dest_required and not self.destination_location_id:
            raise ValueError("destination_location_id is required for this task type")
        return self


class PatientClothingRequestCreate(BaseModel):
    """
    Created by the patient page via POST /tasks/patient/request.
    bed_code is resolved from the auth token on the server — not sent by the client.
    """
    task_type: TaskType   # must be PATIENT_CLOTHES_RENTAL or PATIENT_CLOTHES_RETURN
    note: Optional[str] = None
    order_top: Optional[int] = None
    order_bottom: Optional[int] = None


# ── Response bodies ──────────────────────────────────────────────────────────

class TaskRead(BaseModel):
    id: int
    task_type: str
    status: str
    priority: int
    requested_by_role: str
    requested_by_user: Optional[str] = None
    patient_bed_code: Optional[str] = None
    origin_location: Optional[LocationRead] = None
    destination_location: Optional[LocationRead] = None
    note: Optional[str] = None
    order_top: Optional[int] = None
    order_bottom: Optional[int] = None
    order_bedding: Optional[int] = None
    order_other: Optional[int] = None
    assigned_robot_id: Optional[int] = None
    created_at: datetime
    started_at: Optional[datetime] = None
    completed_at: Optional[datetime] = None
    updated_at: datetime

    model_config = {"from_attributes": True}


class TaskStatusUpdate(BaseModel):
    """Payload sent over WebSocket when a task status changes."""
    event: str = "task_status_update"
    task: TaskRead


# ── MQTT publish schema ──────────────────────────────────────────────────────

class MqttTaskAssignPayload(BaseModel):
    task_id: int
    task_type: str
    origin: Optional[str] = None         # location_code
    destination: Optional[str] = None    # location_code
    priority: int
