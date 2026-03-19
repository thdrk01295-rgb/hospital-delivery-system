from datetime import datetime

from sqlalchemy import DateTime, ForeignKey, Integer, func
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.base import Base


class ClothingInventory(Base):
    """
    Tracks clothing inventory at a given location.
    clean_count  — freshly laundered clothes available.
    used_count   — soiled clothes awaiting collection.
    """
    __tablename__ = "clothing_inventory"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    location_id: Mapped[int] = mapped_column(
        Integer, ForeignKey("locations.id"), nullable=False, unique=True
    )
    clean_count: Mapped[int] = mapped_column(Integer, nullable=False, default=0)
    used_count: Mapped[int] = mapped_column(Integer, nullable=False, default=0)
    updated_at: Mapped[datetime] = mapped_column(
        DateTime, server_default=func.now(), onupdate=func.now(), nullable=False
    )

    # Relationships
    location: Mapped["Location"] = relationship(  # noqa: F821
        "Location", back_populates="inventory"
    )

    def __repr__(self) -> str:
        return (
            f"<ClothingInventory location_id={self.location_id} "
            f"clean={self.clean_count} used={self.used_count}>"
        )
