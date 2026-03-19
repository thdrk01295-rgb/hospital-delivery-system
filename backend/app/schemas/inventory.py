from datetime import datetime

from pydantic import BaseModel

from app.schemas.location import LocationRead


class InventoryRead(BaseModel):
    id: int
    location: LocationRead
    clean_count: int
    used_count: int
    updated_at: datetime

    model_config = {"from_attributes": True}


class InventoryUpdate(BaseModel):
    clean_count: int
    used_count: int
