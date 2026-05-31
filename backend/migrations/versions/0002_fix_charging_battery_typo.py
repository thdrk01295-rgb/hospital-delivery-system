"""fix CHAGING_BATTERY typo → CHARGING_BATTERY in robots.current_state

Revision ID: 0002
Revises: 0001
Create Date: 2026-05-31
"""
from typing import Sequence, Union

from alembic import op

revision: str = "0002"
down_revision: Union[str, None] = "0001"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    # robots.current_state is the only column in the schema that stores RobotState values.
    # No robot_state_log, audit, or history tables exist in this schema.
    op.execute(
        "UPDATE robots "
        "SET current_state = 'CHARGING_BATTERY' "
        "WHERE current_state = 'CHAGING_BATTERY'"
    )


def downgrade() -> None:
    op.execute(
        "UPDATE robots "
        "SET current_state = 'CHAGING_BATTERY' "
        "WHERE current_state = 'CHARGING_BATTERY'"
    )
