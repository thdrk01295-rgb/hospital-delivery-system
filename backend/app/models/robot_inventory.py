from datetime import datetime

from sqlalchemy import DateTime, ForeignKey, Integer, func
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.base import Base

ROBOT_KIT_CAPACITY          = 6
ROBOT_CLOTHES_TOP_CAPACITY  = 6
ROBOT_CLOTHES_BOTTOM_CAPACITY = 6


class RobotInventory(Base):
    """Tracks consumable item counts loaded onto a robot."""
    __tablename__ = "robot_inventories"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    robot_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("robots.id"), unique=True, nullable=False, index=True
    )
    kit_count: Mapped[int]           = mapped_column(Integer, nullable=False, default=0)
    clothes_top_count: Mapped[int]   = mapped_column(Integer, nullable=False, default=0)
    clothes_bottom_count: Mapped[int] = mapped_column(Integer, nullable=False, default=0)
    updated_at: Mapped[datetime] = mapped_column(
        DateTime, server_default=func.now(), onupdate=func.now(), nullable=False
    )

    robot: Mapped["Robot"] = relationship("Robot", back_populates="inventory")  # noqa: F821

    def __repr__(self) -> str:
        return (
            f"<RobotInventory robot_id={self.robot_id} "
            f"kit={self.kit_count} top={self.clothes_top_count} bottom={self.clothes_bottom_count}>"
        )
