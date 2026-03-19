from datetime import datetime
from typing import Optional

from pydantic import BaseModel

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
    timestamp: datetime


class MqttRobotLocationPayload(BaseModel):
    robot_id: str
    location_code: str
    timestamp: datetime


class MqttRobotBatteryPayload(BaseModel):
    robot_id: str
    battery_percent: float
    timestamp: datetime


class MqttRobotErrorPayload(BaseModel):
    robot_id: str
    error_message: str
    timestamp: datetime


class MqttTaskCompletePayload(BaseModel):
    robot_id: str
    task_id: int
    timestamp: datetime
