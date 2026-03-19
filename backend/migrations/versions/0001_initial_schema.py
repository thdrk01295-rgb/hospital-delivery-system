"""initial schema — all tables

Revision ID: 0001
Revises:
Create Date: 2026-03-19
"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa

revision: str = "0001"
down_revision: Union[str, None] = None
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.create_table(
        "locations",
        sa.Column("id", sa.Integer(), primary_key=True, autoincrement=True),
        sa.Column("location_code", sa.String(20), nullable=False, unique=True),
        sa.Column("location_type", sa.String(30), nullable=False),
        sa.Column("floor", sa.Integer(), nullable=True),
        sa.Column("room", sa.Integer(), nullable=True),
        sa.Column("bed", sa.Integer(), nullable=True),
        sa.Column("display_name", sa.String(100), nullable=False),
        sa.Column("is_active", sa.Boolean(), nullable=False, server_default=sa.true()),
        sa.Column("created_at", sa.DateTime(), nullable=False, server_default=sa.func.now()),
        sa.Column("updated_at", sa.DateTime(), nullable=False, server_default=sa.func.now()),
    )
    op.create_index("ix_locations_location_code", "locations", ["location_code"])
    op.create_index("ix_locations_location_type", "locations", ["location_type"])

    op.create_table(
        "robots",
        sa.Column("id", sa.Integer(), primary_key=True, autoincrement=True),
        sa.Column("robot_code", sa.String(50), nullable=False, unique=True),
        sa.Column("current_state", sa.String(30), nullable=False, server_default="IDLE"),
        sa.Column("current_location_id", sa.Integer(),
                  sa.ForeignKey("locations.id"), nullable=True),
        sa.Column("battery_percent", sa.Float(), nullable=False, server_default="100.0"),
        sa.Column("last_seen_at", sa.DateTime(), nullable=True),
        sa.Column("created_at", sa.DateTime(), nullable=False, server_default=sa.func.now()),
        sa.Column("updated_at", sa.DateTime(), nullable=False, server_default=sa.func.now()),
    )
    op.create_index("ix_robots_robot_code", "robots", ["robot_code"])

    op.create_table(
        "tasks",
        sa.Column("id", sa.Integer(), primary_key=True, autoincrement=True),
        sa.Column("task_type", sa.String(40), nullable=False),
        sa.Column("origin_location_id", sa.Integer(),
                  sa.ForeignKey("locations.id"), nullable=True),
        sa.Column("destination_location_id", sa.Integer(),
                  sa.ForeignKey("locations.id"), nullable=True),
        sa.Column("patient_bed_code", sa.String(10), nullable=True),
        sa.Column("requested_by_role", sa.String(20), nullable=False),
        sa.Column("requested_by_user", sa.String(100), nullable=True),
        sa.Column("priority", sa.Integer(), nullable=False, server_default="5"),
        sa.Column("status", sa.String(20), nullable=False, server_default="PENDING"),
        sa.Column("assigned_robot_id", sa.Integer(),
                  sa.ForeignKey("robots.id"), nullable=True),
        sa.Column("note", sa.Text(), nullable=True),
        sa.Column("order_top", sa.Integer(), nullable=True),
        sa.Column("order_bottom", sa.Integer(), nullable=True),
        sa.Column("order_bedding", sa.Integer(), nullable=True),
        sa.Column("order_other", sa.Integer(), nullable=True),
        sa.Column("created_at", sa.DateTime(), nullable=False, server_default=sa.func.now()),
        sa.Column("started_at", sa.DateTime(), nullable=True),
        sa.Column("completed_at", sa.DateTime(), nullable=True),
        sa.Column("updated_at", sa.DateTime(), nullable=False, server_default=sa.func.now()),
    )
    op.create_index("ix_tasks_task_type", "tasks", ["task_type"])
    op.create_index("ix_tasks_priority", "tasks", ["priority"])
    op.create_index("ix_tasks_status", "tasks", ["status"])

    op.create_table(
        "clothing_inventory",
        sa.Column("id", sa.Integer(), primary_key=True, autoincrement=True),
        sa.Column("location_id", sa.Integer(),
                  sa.ForeignKey("locations.id"), nullable=False, unique=True),
        sa.Column("clean_count", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("used_count", sa.Integer(), nullable=False, server_default="0"),
        sa.Column("updated_at", sa.DateTime(), nullable=False, server_default=sa.func.now()),
    )

    op.create_table(
        "abnormal_events",
        sa.Column("id", sa.Integer(), primary_key=True, autoincrement=True),
        sa.Column("event_type", sa.String(30), nullable=False),
        sa.Column("related_task_id", sa.Integer(),
                  sa.ForeignKey("tasks.id"), nullable=True),
        sa.Column("note", sa.Text(), nullable=True),
        sa.Column("occurred_at", sa.DateTime(), nullable=False, server_default=sa.func.now()),
        sa.Column("resolved_at", sa.DateTime(), nullable=True),
    )
    op.create_index("ix_abnormal_events_event_type", "abnormal_events", ["event_type"])


def downgrade() -> None:
    op.drop_table("abnormal_events")
    op.drop_table("clothing_inventory")
    op.drop_table("tasks")
    op.drop_table("robots")
    op.drop_table("locations")
