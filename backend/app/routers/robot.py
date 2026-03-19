from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from app.db.session import get_db
from app.schemas.robot import RobotStatusRead
from app.services.robot_service import get_or_create_robot, get_robot_status_dict

router = APIRouter(prefix="/robot", tags=["robot"])


@router.get("/status", response_model=dict)
def robot_status(db: Session = Depends(get_db)):
    robot = get_or_create_robot(db)
    return get_robot_status_dict(robot)
