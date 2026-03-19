from datetime import datetime
from typing import Optional

from sqlalchemy import DateTime, ForeignKey, Integer, String, Text, func
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.base import Base
from app.constants.enums import TaskStatus, RequestedByRole, TASK_PRIORITY


class Task(Base):
    """
    Core work unit dispatched to the robot.
    Priority is stored as an integer (1 = highest, 7 = lowest) per the project spec.
    """
    __tablename__ = "tasks"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    task_type: Mapped[str] = mapped_column(String(40), nullable=False, index=True)

    origin_location_id: Mapped[Optional[int]] = mapped_column(
        Integer, ForeignKey("locations.id"), nullable=True
    )
    destination_location_id: Mapped[Optional[int]] = mapped_column(
        Integer, ForeignKey("locations.id"), nullable=True
    )

    # Bed code of the requesting patient (populated for patient tasks)
    patient_bed_code: Mapped[Optional[str]] = mapped_column(String(10), nullable=True)

    requested_by_role: Mapped[str] = mapped_column(
        String(20), nullable=False, default=RequestedByRole.NURSE
    )
    # Nurse user identifier (bed_code serves as patient identifier)
    requested_by_user: Mapped[Optional[str]] = mapped_column(String(100), nullable=True)

    priority: Mapped[int] = mapped_column(Integer, nullable=False, default=5, index=True)
    status: Mapped[str] = mapped_column(
        String(20), nullable=False, default=TaskStatus.PENDING, index=True
    )

    assigned_robot_id: Mapped[Optional[int]] = mapped_column(
        Integer, ForeignKey("robots.id"), nullable=True
    )

    note: Mapped[Optional[str]] = mapped_column(Text, nullable=True)

    # Order content fields (for clothes tasks: top/bottom/bedding/other counts)
    order_top: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    order_bottom: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    order_bedding: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    order_other: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)

    created_at: Mapped[datetime] = mapped_column(DateTime, server_default=func.now(), nullable=False)
    started_at: Mapped[Optional[datetime]] = mapped_column(DateTime, nullable=True)
    completed_at: Mapped[Optional[datetime]] = mapped_column(DateTime, nullable=True)
    updated_at: Mapped[datetime] = mapped_column(
        DateTime, server_default=func.now(), onupdate=func.now(), nullable=False
    )

    # Relationships
    origin_location: Mapped[Optional["Location"]] = relationship(  # noqa: F821
        "Location", foreign_keys=[origin_location_id], back_populates="tasks_as_origin"
    )
    destination_location: Mapped[Optional["Location"]] = relationship(  # noqa: F821
        "Location", foreign_keys=[destination_location_id], back_populates="tasks_as_destination"
    )
    assigned_robot: Mapped[Optional["Robot"]] = relationship(  # noqa: F821
        "Robot", back_populates="tasks"
    )

    @staticmethod
    def resolve_priority(task_type: str) -> int:
        return TASK_PRIORITY.get(task_type, 5)

    def __repr__(self) -> str:
        return (
            f"<Task id={self.id} type={self.task_type} "
            f"priority={self.priority} status={self.status}>"
        )
