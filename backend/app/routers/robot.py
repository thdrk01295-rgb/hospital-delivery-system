from fastapi import APIRouter, Depends, HTTPException, Header, status
from pydantic import BaseModel
from sqlalchemy.orm import Session
from typing import Optional

from app.db.session import get_db
from app.schemas.robot import RobotStatusRead
from app.services.robot_service import get_or_create_robot, get_robot_status_dict
from app.services.auth_service import decode_token
from app.constants import mqtt_topics

router = APIRouter(prefix="/robot", tags=["robot"])


@router.get("/status", response_model=dict)
def robot_status(db: Session = Depends(get_db)):
    robot = get_or_create_robot(db)
    return get_robot_status_dict(robot)


class LockCommandRequest(BaseModel):
    robot_id: str
    task_id: int
    command: str  # UNLOCK | LOCK


@router.post("/lock-command", status_code=status.HTTP_200_OK)
def send_lock_command(
    body: LockCommandRequest,
    authorization: Optional[str] = Header(default=None),
):
    """
    Tablet UI endpoint (v3): sends server/lock_command to the robot.
    Called when the nurse/patient presses the unlock button on the tablet.
    Requires a valid nurse auth token.
    """
    if not authorization or not authorization.startswith("Bearer "):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Missing token")
    try:
        token_payload = decode_token(authorization.split(" ", 1)[1])
    except Exception:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid token")

    if token_payload.get("role") not in ("nurse", "patient"):
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Nurses and patients only")

    if body.command not in ("UNLOCK", "LOCK"):
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=f"Invalid command '{body.command}' — must be UNLOCK or LOCK",
        )

    from app.mqtt.client import publish
    payload = {
        "robot_id": body.robot_id,
        "task_id": body.task_id,
        "command": body.command,
    }
    publish(mqtt_topics.SERVER_LOCK_COMMAND, payload)
    return {"status": "published", "payload": payload}
