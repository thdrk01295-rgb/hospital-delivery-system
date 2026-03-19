"""
Robot service — reads and updates robot state in the database.
MQTT handlers call these functions after parsing incoming payloads.
"""
from datetime import datetime, timezone
from typing import Optional

from sqlalchemy.orm import Session

from app.models.robot import Robot
from app.models.location import Location
from app.constants.enums import RobotState
from app.utils.location_utils import location_display_text


def get_robot(db: Session, robot_code: str = "AMR-001") -> Optional[Robot]:
    return db.query(Robot).filter(Robot.robot_code == robot_code).first()


def get_or_create_robot(db: Session, robot_code: str = "AMR-001") -> Robot:
    robot = get_robot(db, robot_code)
    if not robot:
        robot = Robot(robot_code=robot_code)
        db.add(robot)
        db.commit()
        db.refresh(robot)
    return robot


def update_robot_state(db: Session, robot_code: str, state: str) -> Robot:
    robot = get_or_create_robot(db, robot_code)
    robot.current_state = state
    robot.last_seen_at = datetime.now(timezone.utc)
    db.commit()
    db.refresh(robot)
    return robot


def update_robot_location(db: Session, robot_code: str, location_code: str) -> Robot:
    robot = get_or_create_robot(db, robot_code)
    location = db.query(Location).filter(Location.location_code == location_code).first()
    if location:
        robot.current_location_id = location.id
    robot.last_seen_at = datetime.now(timezone.utc)
    db.commit()
    db.refresh(robot)
    return robot


def update_robot_battery(db: Session, robot_code: str, battery_percent: float) -> Robot:
    robot = get_or_create_robot(db, robot_code)
    robot.battery_percent = battery_percent
    robot.last_seen_at = datetime.now(timezone.utc)
    db.commit()
    db.refresh(robot)
    return robot


def get_robot_status_dict(robot: Robot) -> dict:
    loc = robot.current_location
    display = None
    if loc:
        display = location_display_text(
            loc.location_type, loc.floor, loc.room, loc.bed, loc.display_name
        )
    return {
        "id": robot.id,
        "robot_code": robot.robot_code,
        "current_state": robot.current_state,
        "battery_percent": robot.battery_percent,
        "last_seen_at": robot.last_seen_at.isoformat() if robot.last_seen_at else None,
        "location_display": display,
        "current_location": {
            "id": loc.id,
            "location_code": loc.location_code,
            "display_name": loc.display_name,
        } if loc else None,
    }
