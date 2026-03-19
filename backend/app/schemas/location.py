from datetime import datetime
from typing import Optional

from pydantic import BaseModel


class LocationBase(BaseModel):
    location_code: str
    location_type: str
    floor: Optional[int] = None
    room: Optional[int] = None
    bed: Optional[int] = None
    display_name: str
    is_active: bool = True


class LocationRead(LocationBase):
    id: int
    created_at: datetime
    updated_at: datetime

    model_config = {"from_attributes": True}


class BedSelectorMeta(BaseModel):
    """Metadata for the 3-step bed selector in the UI."""
    floors: list[int]
    rooms_by_floor: dict[int, list[int]]
    beds_per_room: int = 6
