from datetime import datetime
from typing import Optional

from sqlalchemy import DateTime, ForeignKey, Integer, String, Text, func
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.base import Base


class AbnormalEvent(Base):
    """
    Records device-abnormal events: ERROR, LOW_BATTERY, and EMERGENCY_CALL.
    An event is 'active' while resolved_at is NULL.
    Only one unresolved event should exist at a time; the service enforces this.
    """
    __tablename__ = "abnormal_events"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)

    # "error" | "low_battery" | "emergency_call"
    event_type: Mapped[str] = mapped_column(String(30), nullable=False, index=True)

    # Optional reference to the task created for this event (e.g. EMERGENCY_CALL task)
    related_task_id: Mapped[Optional[int]] = mapped_column(
        Integer, ForeignKey("tasks.id"), nullable=True
    )

    note: Mapped[Optional[str]] = mapped_column(Text, nullable=True)

    occurred_at: Mapped[datetime] = mapped_column(DateTime, server_default=func.now(), nullable=False)
    resolved_at: Mapped[Optional[datetime]] = mapped_column(DateTime, nullable=True)

    # Relationships
    related_task: Mapped[Optional["Task"]] = relationship("Task")  # noqa: F821

    @property
    def is_active(self) -> bool:
        return self.resolved_at is None

    def __repr__(self) -> str:
        return (
            f"<AbnormalEvent id={self.id} type={self.event_type} "
            f"active={self.is_active}>"
        )
