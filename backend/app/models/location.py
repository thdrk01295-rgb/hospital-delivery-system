from datetime import datetime
from typing import Optional

from sqlalchemy import Boolean, DateTime, Integer, String, func
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.db.base import Base


class Location(Base):
    """
    Normalized location model covering both bed locations and non-bed locations.

    Bed locations:   location_type="bed",  floor/room/bed are set.
                     location_code is the 5-digit bed code (e.g. "22074").
    Non-bed locations: location_type one of station/laundry/warehouse/…,
                       floor/room/bed are NULL.
                       location_code equals the location_type string.
    """
    __tablename__ = "locations"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    location_code: Mapped[str] = mapped_column(String(20), unique=True, nullable=False, index=True)
    location_type: Mapped[str] = mapped_column(String(30), nullable=False, index=True)
    floor: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    room: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    bed: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    display_name: Mapped[str] = mapped_column(String(100), nullable=False)
    is_active: Mapped[bool] = mapped_column(Boolean, default=True, nullable=False)
    created_at: Mapped[datetime] = mapped_column(DateTime, server_default=func.now(), nullable=False)
    updated_at: Mapped[datetime] = mapped_column(
        DateTime, server_default=func.now(), onupdate=func.now(), nullable=False
    )

    # Relationships
    tasks_as_origin: Mapped[list["Task"]] = relationship(  # noqa: F821
        "Task", foreign_keys="Task.origin_location_id", back_populates="origin_location"
    )
    tasks_as_destination: Mapped[list["Task"]] = relationship(  # noqa: F821
        "Task", foreign_keys="Task.destination_location_id", back_populates="destination_location"
    )
    inventory: Mapped[Optional["ClothingInventory"]] = relationship(  # noqa: F821
        "ClothingInventory", back_populates="location", uselist=False
    )
    robots_at_location: Mapped[list["Robot"]] = relationship(  # noqa: F821
        "Robot", back_populates="current_location"
    )

    def __repr__(self) -> str:
        return f"<Location id={self.id} code={self.location_code} name={self.display_name}>"
