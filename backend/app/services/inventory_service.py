"""
Inventory service — manages clothing stock per location.
"""
from sqlalchemy.orm import Session

from app.models.inventory import ClothingInventory
from app.models.location import Location


def get_all_inventory(db: Session) -> list[ClothingInventory]:
    return db.query(ClothingInventory).join(ClothingInventory.location).all()


def get_inventory_by_location(db: Session, location_id: int) -> ClothingInventory | None:
    return (
        db.query(ClothingInventory)
        .filter(ClothingInventory.location_id == location_id)
        .first()
    )


def upsert_inventory(db: Session, location_id: int,
                     clean_count: int, used_count: int) -> ClothingInventory:
    inv = get_inventory_by_location(db, location_id)
    if inv:
        inv.clean_count = clean_count
        inv.used_count = used_count
    else:
        inv = ClothingInventory(
            location_id=location_id,
            clean_count=clean_count,
            used_count=used_count,
        )
        db.add(inv)
    db.commit()
    db.refresh(inv)
    return inv
