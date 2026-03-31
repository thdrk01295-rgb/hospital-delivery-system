from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from app.db.session import get_db
from app.models.location import Location
from app.schemas.location import LocationRead, BedSelectorMeta
from app.constants.enums import LocationType

router = APIRouter(prefix="/locations", tags=["locations"])


@router.get("", response_model=list[LocationRead])
def list_locations(db: Session = Depends(get_db)):
    return db.query(Location).filter(Location.is_active == True).all()


@router.get("/non-bed", response_model=list[LocationRead])
def list_non_bed_locations(db: Session = Depends(get_db)):
    return (
        db.query(Location)
        .filter(Location.location_type != LocationType.BED.value, Location.is_active == True)
        .all()
    )


@router.get("/bed-selector-meta", response_model=BedSelectorMeta)
def bed_selector_meta():
    """Returns the static floor/room/bed structure for the 3-step bed selector UI."""
    floors = list(range(1, 5))                   # 1–4
    rooms_by_floor = {
        f: [f * 100 + r for r in range(1, 9)]    # e.g. floor 2 → [201..208]
        for f in floors
    }
    return BedSelectorMeta(floors=floors, rooms_by_floor=rooms_by_floor, beds_per_room=6)
