from datetime import datetime
from typing import Optional

from pydantic import BaseModel


class AbnormalEventRead(BaseModel):
    id: int
    event_type: str
    related_task_id: Optional[int] = None
    note: Optional[str] = None
    occurred_at: datetime
    resolved_at: Optional[datetime] = None
    is_active: bool

    model_config = {"from_attributes": True}
