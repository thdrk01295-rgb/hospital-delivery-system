"""fix non-bed location_code values to canonical robot-facing codes

Revision ID: 0003
Revises: 0002
Create Date: 2026-06-05

Before: location_code stored the LocationType enum value (e.g. "station", "exam_A")
After:  location_code stores the canonical code the robot expects (e.g. "STATION-01", "EXAM-A")
"""
from typing import Sequence, Union

from alembic import op

revision: str = "0003"
down_revision: Union[str, None] = "0002"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None

# (old_code, new_code) pairs
_RENAMES = [
    ("station",      "STATION-01"),
    ("laundry",      "LAUNDRY-01"),
    ("warehouse",    "WAREHOUSE-01"),
    ("specimen_lab", "SPECIMEN-LAB"),
    ("exam_A",       "EXAM-A"),
    ("exam_B",       "EXAM-B"),
    ("exam_C",       "EXAM-C"),
]


def upgrade() -> None:
    for old, new in _RENAMES:
        op.execute(
            f"UPDATE locations SET location_code = '{new}' WHERE location_code = '{old}'"
        )


def downgrade() -> None:
    for old, new in _RENAMES:
        op.execute(
            f"UPDATE locations SET location_code = '{old}' WHERE location_code = '{new}'"
        )
