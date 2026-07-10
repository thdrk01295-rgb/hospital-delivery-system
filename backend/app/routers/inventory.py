from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from app.db.session import get_db
from app.schemas.inventory import InventoryRead, InventoryUpdate
from app.schemas.robot_inventory import RobotInventoryRead
from app.services.inventory_service import (
    get_all_inventory,
    get_inventory_by_location,
    upsert_inventory,
)
from app.services.robot_inventory_service import get_or_create_robot_inventory

router = APIRouter(prefix="/inventory", tags=["inventory"])


@router.get("/robot", response_model=RobotInventoryRead)
def get_robot_inventory(db: Session = Depends(get_db)):
    """Returns the current robot inventory counts and capacities."""
    from app.models.robot import Robot as RobotModel
    robot = db.query(RobotModel).first()
    if not robot:
        raise HTTPException(status_code=404, detail="No robot found")
    inv = get_or_create_robot_inventory(db, robot.id)
    db.commit()
    db.refresh(inv)
    return inv


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
