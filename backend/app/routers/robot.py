from fastapi import APIRouter, Depends, HTTPException, status
from pydantic import BaseModel
from sqlalchemy.orm import Session
from typing import Optional

from app.db.session import get_db
from app.schemas.robot import RobotStatusRead
from app.services.robot_service import get_or_create_robot, get_robot_status_dict
from app.constants import mqtt_topics
from app.constants.enums import RobotState, TaskStatus

router = APIRouter(prefix="/robot", tags=["robot"])


@router.get("/status", response_model=dict)
def robot_status(db: Session = Depends(get_db)):
    robot = get_or_create_robot(db)
    return get_robot_status_dict(robot)


class LockCommandRequest(BaseModel):
    robot_id: str
    task_id: Optional[int] = None  # optional; validated against active task when provided
    command: str                    # UNLOCK | LOCK


@router.post("/lock-command", status_code=status.HTTP_200_OK)
def send_lock_command(
    body: LockCommandRequest,
    db: Session = Depends(get_db),
):
    """
    Robot-mounted tablet endpoint (v3): sends server/lock_command to the robot.
    No user auth required — the tablet acts in the context of the robot it is paired with.

    Validations:
      - command must be UNLOCK or LOCK
      - robot_id must identify a known robot
      - UNLOCK is rejected unless robot is in WAIT_UNLOCK state
      - if task_id is provided it must match the robot's current active task
    """
    if body.command not in ("UNLOCK", "LOCK"):
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=f"Invalid command '{body.command}' — must be UNLOCK or LOCK",
        )

    # Validate robot exists
    from app.models.robot import Robot as RobotModel
    robot = db.query(RobotModel).filter(RobotModel.robot_code == body.robot_id).first()
    if not robot:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"Robot '{body.robot_id}' not found",
        )

    # UNLOCK is only valid when the robot is waiting for compartment unlock
    if body.command == "UNLOCK" and robot.current_state != RobotState.WAIT_UNLOCK:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail=(
                f"UNLOCK rejected — robot '{body.robot_id}' is in state "
                f"'{robot.current_state}', expected WAIT_UNLOCK"
            ),
        )

    # LOCK is only valid when the compartment is open (delivery states)
    delivery_open_states = {RobotState.DELIVERY_OPEN_NUR, RobotState.DELIVERY_OPEN_PAT}
    if body.command == "LOCK" and robot.current_state not in delivery_open_states:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail=(
                f"LOCK rejected — robot '{body.robot_id}' is in state "
                f"'{robot.current_state}', expected DELIVERY_OPEN_NUR or DELIVERY_OPEN_PAT"
            ),
        )

    # Resolve the robot's current active task
    from app.models.task import Task
    active_task = (
        db.query(Task)
        .filter(
            Task.assigned_robot_id == robot.id,
            Task.status.in_([TaskStatus.DISPATCHED, TaskStatus.IN_PROGRESS]),
        )
        .order_by(Task.created_at.desc())
        .first()
    )

    # If caller supplied task_id, verify it matches the active task
    if body.task_id is not None:
        active_id = active_task.id if active_task else None
        if active_id != body.task_id:
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail=(
                    f"task_id={body.task_id} does not match robot's current active task "
                    f"(active={active_id})"
                ),
            )

    resolved_task_id = active_task.id if active_task else body.task_id
    reason = "tablet_unlock" if body.command == "UNLOCK" else "tablet_lock"

    from app.mqtt.client import publish
    mqtt_payload = {
        "robot_id": body.robot_id,
        "task_id": resolved_task_id,
        "command": body.command,
        "reason": reason,
    }
    publish(mqtt_topics.SERVER_LOCK_COMMAND, mqtt_payload)
    return {"status": "published", "payload": mqtt_payload}


class CompleteTaskRequest(BaseModel):
    robot_id: str


@router.post("/complete-task", status_code=status.HTTP_200_OK)
async def complete_task_from_tablet(
    body: CompleteTaskRequest,
    db: Session = Depends(get_db),
):
    """
    Robot-mounted tablet endpoint: marks the active task COMPLETE and publishes
    server/task_finish with source="tablet_ui". No user auth required.

    Validations:
      - robot_id must identify a known robot
      - robot must be in DELIVERY_OPEN_NUR or DELIVERY_OPEN_PAT
      - robot must have an active (DISPATCHED/IN_PROGRESS) task
    """
    from app.models.robot import Robot as RobotModel
    from app.models.task import Task
    from app.mqtt.client import publish
    from app.websocket.manager import ws_manager
    from app.constants import ws_events
    from app.schemas.task import TaskRead
    from app.services.task_service import update_task_status

    robot = db.query(RobotModel).filter(RobotModel.robot_code == body.robot_id).first()
    if not robot:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"Robot '{body.robot_id}' not found",
        )

    delivery_open_states = {RobotState.DELIVERY_OPEN_NUR, RobotState.DELIVERY_OPEN_PAT}
    if robot.current_state not in delivery_open_states:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail=(
                f"Cannot complete — robot '{body.robot_id}' is in state "
                f"'{robot.current_state}', expected DELIVERY_OPEN_NUR or DELIVERY_OPEN_PAT"
            ),
        )

    active_task = (
        db.query(Task)
        .filter(
            Task.assigned_robot_id == robot.id,
            Task.status.in_([TaskStatus.DISPATCHED, TaskStatus.IN_PROGRESS]),
        )
        .order_by(Task.created_at.desc())
        .first()
    )
    if not active_task:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail=f"No active task found for robot '{body.robot_id}'",
        )

    # v4 Lock Completion Guard: compartment must have been opened AND re-locked
    from app.mqtt.handlers import _task_lock_phases
    if _task_lock_phases.get(active_task.id) != "RELOCKED":
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail="잠금버튼을 눌러주세요",
        )

    task = update_task_status(db, active_task.id, TaskStatus.COMPLETE)
    task_dict = TaskRead.model_validate(task).model_dump(mode="json")
    await ws_manager.broadcast(ws_events.TASK_STATUS_UPDATE, task_dict)

    publish(mqtt_topics.SERVER_TASK_FINISH, {
        "robot_id": body.robot_id,
        "task_id": active_task.id,
        "source": "tablet_ui",
    })

    _task_lock_phases.pop(active_task.id, None)

    return {"status": "completed", "task_id": active_task.id}
