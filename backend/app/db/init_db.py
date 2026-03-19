"""
Creates all tables if they do not exist.
Call this at application startup (after all models are imported).
"""
from app.db.base import Base
from app.db.session import engine

# Import all models so SQLAlchemy registers them before create_all
import app.models  # noqa: F401


def init_db() -> None:
    Base.metadata.create_all(bind=engine)
