"""add inventory_applied to tasks; CHECK constraints and seed on robot_inventories

Revision ID: 0005
Revises: 0004
Create Date: 2026-07-10

Changes:
  - tasks.inventory_applied (BIT NOT NULL DEFAULT 0) — idempotency guard for
    inventory mutations: prevents a second finalize_task_complete call from
    applying inventory twice.
  - robot_inventories: CHECK constraints enforce 0 <= count <= 6 for each column.
  - robot_inventories: seed the AMR-001 robot row at kit=6, top=6, bottom=6
    (idempotent — skips if a row already exists for that robot).
"""
from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op

revision: str = "0005"
down_revision: Union[str, None] = "0004"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    # ── 1. Add inventory_applied to tasks ────────────────────────────────────
    op.add_column(
        "tasks",
        sa.Column("inventory_applied", sa.Boolean, nullable=False, server_default="0"),
    )

    # ── 2. CHECK constraints on robot_inventories ─────────────────────────────
    op.create_check_constraint(
        "ck_ri_kit_count",
        "robot_inventories",
        "kit_count >= 0 AND kit_count <= 6",
    )
    op.create_check_constraint(
        "ck_ri_top_count",
        "robot_inventories",
        "clothes_top_count >= 0 AND clothes_top_count <= 6",
    )
    op.create_check_constraint(
        "ck_ri_bottom_count",
        "robot_inventories",
        "clothes_bottom_count >= 0 AND clothes_bottom_count <= 6",
    )

    # ── 3. Seed inventory row for the initial AMR robot ───────────────────────
    # Idempotent: only inserts when no row exists yet for the robot.
    op.execute(sa.text("""
        INSERT INTO robot_inventories
            (robot_id, kit_count, clothes_top_count, clothes_bottom_count, updated_at)
        SELECT r.id, 6, 6, 6, CURRENT_TIMESTAMP
          FROM robots r
         WHERE r.robot_code = 'AMR-001'
           AND NOT EXISTS (
               SELECT 1 FROM robot_inventories ri WHERE ri.robot_id = r.id
           )
    """))


def downgrade() -> None:
    # Seed row removal is intentionally omitted — data loss is non-reversible.
    op.drop_constraint("ck_ri_bottom_count", "robot_inventories", type_="check")
    op.drop_constraint("ck_ri_top_count",    "robot_inventories", type_="check")
    op.drop_constraint("ck_ri_kit_count",    "robot_inventories", type_="check")
    op.drop_column("tasks", "inventory_applied")
