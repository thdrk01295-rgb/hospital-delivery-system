"""
Shared fixtures for the test suite.
Uses an in-memory SQLite database so no MSSQL connection is required.
"""
import pytest
from unittest.mock import patch
from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker

from app.db.base import Base
from app.models import task, robot, robot_inventory, location  # noqa: F401 — ensure models are registered


@pytest.fixture
def engine():
    eng = create_engine("sqlite:///:memory:", connect_args={"check_same_thread": False})
    Base.metadata.create_all(eng)
    yield eng
    Base.metadata.drop_all(eng)
    eng.dispose()


@pytest.fixture
def db(engine):
    conn = engine.connect()
    txn = conn.begin()
    Session = sessionmaker(bind=conn, join_transaction_mode="create_savepoint")
    session = Session()
    yield session
    session.close()
    txn.rollback()
    conn.close()


@pytest.fixture
def robot_row(db):
    """Create a Robot row and return it."""
    from app.models.robot import Robot
    from app.constants.enums import RobotState
    r = Robot(robot_code="AMR-001", current_state=RobotState.IDLE, battery_percent=100.0)
    db.add(r)
    db.flush()
    return r


@pytest.fixture
def inv(db, robot_row):
    """Create a RobotInventory seeded to full capacity and return it."""
    from app.models.robot_inventory import RobotInventory, ROBOT_KIT_CAPACITY, ROBOT_CLOTHES_TOP_CAPACITY, ROBOT_CLOTHES_BOTTOM_CAPACITY
    i = RobotInventory(
        robot_id=robot_row.id,
        kit_count=ROBOT_KIT_CAPACITY,
        clothes_top_count=ROBOT_CLOTHES_TOP_CAPACITY,
        clothes_bottom_count=ROBOT_CLOTHES_BOTTOM_CAPACITY,
    )
    db.add(i)
    db.flush()
    return i
