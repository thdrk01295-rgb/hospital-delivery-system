"""add robot_inventories table

Revision ID: 0004
Revises: 0003
Create Date: 2026-07-10

Tracks per-robot item counts (kits, clothing) that are updated atomically
when tasks complete.
"""
from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op

revision: str = "0004"
down_revision: Union[str, None] = "0003"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.create_table(
        "robot_inventories",
        sa.Column("id",                   sa.Integer,  primary_key=True, autoincrement=True),
        sa.Column("robot_id",             sa.Integer,  sa.ForeignKey("robots.id"), unique=True, nullable=False),
        sa.Column("kit_count",            sa.Integer,  nullable=False, server_default="0"),
        sa.Column("clothes_top_count",    sa.Integer,  nullable=False, server_default="0"),
        sa.Column("clothes_bottom_count", sa.Integer,  nullable=False, server_default="0"),
        sa.Column("updated_at",           sa.DateTime, nullable=False, server_default=sa.func.now()),
    )
    op.create_index("ix_robot_inventories_robot_id", "robot_inventories", ["robot_id"])


def downgrade() -> None:
    op.drop_index("ix_robot_inventories_robot_id", table_name="robot_inventories")
    op.drop_table("robot_inventories")
