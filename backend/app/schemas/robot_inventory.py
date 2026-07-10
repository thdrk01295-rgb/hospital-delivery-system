from datetime import datetime

from pydantic import BaseModel

from app.models.robot_inventory import (
    ROBOT_KIT_CAPACITY,
    ROBOT_CLOTHES_TOP_CAPACITY,
    ROBOT_CLOTHES_BOTTOM_CAPACITY,
)


class RobotInventoryRead(BaseModel):
    robot_id: int
    kit_count: int
    clothes_top_count: int
    clothes_bottom_count: int
    kit_capacity: int = ROBOT_KIT_CAPACITY
    clothes_top_capacity: int = ROBOT_CLOTHES_TOP_CAPACITY
    clothes_bottom_capacity: int = ROBOT_CLOTHES_BOTTOM_CAPACITY
    updated_at: datetime

    model_config = {"from_attributes": True}
