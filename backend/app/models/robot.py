from datetime import datetime
from typing import Optional

from sqlalchemy import DateTime, Float, ForeignKey, Integer, String, func
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.base import Base
from app.constants.enums import RobotState


class Robot(Base):
    """
    Represents a physical AMR robot.
    There will typically be one robot in the system but the model supports multiple.
    """
    __tablename__ = "robots"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    robot_code: Mapped[str] = mapped_column(String(50), unique=True, nullable=False, index=True)
    current_state: Mapped[str] = mapped_column(
        String(30), nullable=False, default=RobotState.IDLE
    )
    current_location_id: Mapped[Optional[int]] = mapped_column(
        Integer, ForeignKey("locations.id"), nullable=True
    )
    battery_percent: Mapped[float] = mapped_column(Float, nullable=False, default=100.0)
    last_seen_at: Mapped[Optional[datetime]] = mapped_column(DateTime, nullable=True)
    created_at: Mapped[datetime] = mapped_column(DateTime, server_default=func.now(), nullable=False)
    updated_at: Mapped[datetime] = mapped_column(
        DateTime, server_default=func.now(), onupdate=func.now(), nullable=False
    )

    # Relationships
    current_location: Mapped[Optional["Location"]] = relationship(  # noqa: F821
        "Location", back_populates="robots_at_location"
    )
    tasks: Mapped[list["Task"]] = relationship(  # noqa: F821
        "Task", back_populates="assigned_robot"
    )

    def is_available(self) -> bool:
        """True when the robot can accept a new task dispatch."""
        from app.constants.enums import BLOCKING_ROBOT_STATES
        return self.current_state not in BLOCKING_ROBOT_STATES

    def __repr__(self) -> str:
        return f"<Robot id={self.id} code={self.robot_code} state={self.current_state}>"
