from datetime import datetime
from typing import Optional

from pydantic import BaseModel, model_validator

from app.schemas.location import LocationRead


class RobotStatusRead(BaseModel):
    id: int
    robot_code: str
    current_state: str
    battery_percent: float
    last_seen_at: Optional[datetime] = None
    current_location: Optional[LocationRead] = None
    location_display: Optional[str] = None   # formatted text, e.g. "2층 207호 4번 침상"

    model_config = {"from_attributes": True}


# ── MQTT payload schemas (used by MQTT handler for validation) ─────────────

class MqttRobotStatusPayload(BaseModel):
    robot_id: str
    state: str
    task_id: Optional[int] = None
    timestamp: Optional[datetime] = None


class MqttRobotLocationPayload(BaseModel):
    robot_id: str
    location_code: str
    timestamp: Optional[datetime] = None


class MqttRobotBatteryPayload(BaseModel):
    robot_id: str
    battery_percent: float
    timestamp: Optional[datetime] = None


class MqttRobotErrorPayload(BaseModel):
    robot_id: str
    error_message: Optional[str] = None  # final standard field
    error: Optional[str] = None           # fallback for older robot clients
    task_id: Optional[int] = None
    timestamp: Optional[datetime] = None

    @model_validator(mode="after")
    def resolve_error_message(self) -> "MqttRobotErrorPayload":
        if not self.error_message:
            self.error_message = self.error or "Unknown error"
        return self


class MqttTaskCompletePayload(BaseModel):
    robot_id: str
    task_id: int
    timestamp: Optional[datetime] = None


class MqttLockStatusPayload(BaseModel):
    """robot/lock_status — v3: sent by robot after lock/unlock command is executed."""
    robot_id: str
    task_id: Optional[int] = None
    command: str                   # UNLOCK | LOCK
    status: str                    # ACCEPTED | OPENED | LOCKED | FAILED
    message: Optional[str] = None
    timestamp: Optional[datetime] = None
