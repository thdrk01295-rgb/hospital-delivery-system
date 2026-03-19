"""
Seed script: inserts all hospital locations into the database.

Bed locations  — 4 floors × 8 rooms × 6 beds = 192 beds
Non-bed locations — station, laundry, warehouse, specimen_lab, exam_A, exam_B, exam_C

Run from the backend/ directory:
    python -m seeds.seed_locations

Or call seed_all() from application code (e.g. in a setup CLI command).
"""
import sys
import os

# Allow running as a standalone script from backend/
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from app.db.session import SessionLocal
from app.models.location import Location
from app.constants.enums import LocationType
from app.utils.location_utils import build_bed_code, NON_BED_DISPLAY


def _generate_bed_locations() -> list[dict]:
    records = []
    for floor in range(1, 5):           # floors 1–4
        for room_num in range(1, 9):    # 8 rooms per floor
            room = floor * 100 + room_num   # e.g. floor=2, room_num=7 → room=207
            for bed in range(1, 7):     # 6 beds per room
                code = build_bed_code(floor, room, bed)
                records.append({
                    "location_code": code,
                    "location_type": LocationType.BED,
                    "floor": floor,
                    "room": room,
                    "bed": bed,
                    "display_name": f"{floor}F / {room} / Bed {bed}",
                })
    return records


def _generate_non_bed_locations() -> list[dict]:
    return [
        {
            "location_code": loc_type,
            "location_type": loc_type,
            "floor": None,
            "room": None,
            "bed": None,
            "display_name": display,
        }
        for loc_type, display in NON_BED_DISPLAY.items()
    ]


def seed_all(db=None) -> None:
    close_db = db is None
    if db is None:
        db = SessionLocal()

    try:
        existing_codes = {row.location_code for row in db.query(Location.location_code).all()}

        to_insert = []
        all_records = _generate_non_bed_locations() + _generate_bed_locations()

        for rec in all_records:
            if rec["location_code"] not in existing_codes:
                to_insert.append(Location(**rec))

        if to_insert:
            db.bulk_save_objects(to_insert)
            db.commit()
            print(f"Seeded {len(to_insert)} location(s).")
        else:
            print("All locations already seeded — nothing to insert.")
    finally:
        if close_db:
            db.close()


if __name__ == "__main__":
    from app.db.init_db import create_all_tables
    print("Initializing tables...")
    create_all_tables()
    print("Seeding locations...")
    seed_all()
    print("Done.")
