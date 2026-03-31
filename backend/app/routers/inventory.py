from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from app.db.session import get_db
from app.schemas.inventory import InventoryRead, InventoryUpdate
from app.services.inventory_service import (
    get_all_inventory,
    get_inventory_by_location,
    upsert_inventory,
)

router = APIRouter(prefix="/inventory", tags=["inventory"])


@router.get("", response_model=list[InventoryRead])
def list_inventory(db: Session = Depends(get_db)):
    return get_all_inventory(db)


@router.get("/{location_id}", response_model=InventoryRead)
def get_inventory(location_id: int, db: Session = Depends(get_db)):
    inv = get_inventory_by_location(db, location_id)
    if not inv:
        raise HTTPException(status_code=404, detail="Inventory not found for this location")
    return inv


@router.put("/{location_id}", response_model=InventoryRead)
def update_inventory(location_id: int, body: InventoryUpdate, db: Session = Depends(get_db)):
    return upsert_inventory(db, location_id, body.clean_count, body.used_count)
