"""
Database initialization helpers.

Production / staging:
    Use Alembic for all schema changes.
    Run:  alembic upgrade head
    The migrations/versions/ directory contains the versioned migration scripts.

Development (local only):
    create_all() is available as a convenience but DOES NOT track schema versions.
    It will be silently ignored if a table already exists, meaning schema changes
    in later migrations will NOT be applied automatically.

    Prefer running  alembic upgrade head  even in development to stay consistent.
"""
from app.db.base import Base
from app.db.session import engine

# Import all models so SQLAlchemy registers them before create_all
import app.models  # noqa: F401


def create_all_tables() -> None:
    """
    Development convenience only.
    Creates all tables that do not yet exist — does NOT apply Alembic migrations.
    Do NOT call this in production; run  alembic upgrade head  instead.
    """
    Base.metadata.create_all(bind=engine)
