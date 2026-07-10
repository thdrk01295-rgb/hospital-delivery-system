"""
Service for managing per-robot item inventory (kits, clothing).

Three principal operations:
  get_or_create_robot_inventory — fetch (or auto-create) the inventory row
  validate_inventory_for_task_creation — HTTP 409 if stock is insufficient or
      already over-allocated by pending tasks
  apply_inventory_effect_for_completed_task — apply exactly-once mutations;
      raises ValueError if a count would go negative (caller must roll back)
"""
import logging
from typing import Optional

from fastapi import HTTPException, status
from sqlalchemy.orm import Session

from app.models.robot_inventory import (
    RobotInventory,
    ROBOT_KIT_CAPACITY,
    ROBOT_CLOTHES_TOP_CAPACITY,
    ROBOT_CLOTHES_BOTTOM_CAPACITY,
)
from app.models.task import Task
from app.constants.enums import TaskType, TaskStatus

logger = logging.getLogger(__name__)

# Statuses that mean a task holds a reservation on inventory
_ACTIVE_STATUSES = (TaskStatus.PENDING, TaskStatus.DISPATCHED, TaskStatus.IN_PROGRESS)


def get_or_create_robot_inventory(db: Session, robot_id: int) -> RobotInventory:
    inv = db.query(RobotInventory).filter(RobotInventory.robot_id == robot_id).first()
    if not inv:
        inv = RobotInventory(
            robot_id=robot_id,
            kit_count=ROBOT_KIT_CAPACITY,
            clothes_top_count=ROBOT_CLOTHES_TOP_CAPACITY,
            clothes_bottom_count=ROBOT_CLOTHES_BOTTOM_CAPACITY,
        )
        db.add(inv)
        db.flush()
        logger.info(f"[inventory] Created RobotInventory for robot_id={robot_id} (seeded to capacity)")
    return inv


def validate_inventory_for_task_creation(
    db: Session,
    robot_id: int,
    task_type: str,
    order_top: Optional[int] = None,
    order_bottom: Optional[int] = None,
) -> None:
    """
    Raises HTTP 409 Conflict when the robot lacks sufficient inventory to fulfil
    the new task, accounting for items already reserved by active (pending /
    dispatched / in-progress) tasks.

    Only kit_delivery and patient_clothes_rental consume inventory at creation.
    All other task types pass through without a check.
    """
    inv = get_or_create_robot_inventory(db, robot_id)

    if task_type == TaskType.KIT_DELIVERY:
        # Count active kit_delivery tasks that are already consuming a kit
        reserved = (
            db.query(Task)
            .filter(
                Task.task_type == TaskType.KIT_DELIVERY,
                Task.status.in_(_ACTIVE_STATUSES),
            )
            .count()
        )
        available = inv.kit_count - reserved
        if available < 1:
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail=(
                    f"Insufficient kits: {inv.kit_count} loaded, "
                    f"{reserved} already reserved by active tasks"
                ),
            )

    elif task_type == TaskType.PATIENT_CLOTHES_RENTAL:
        if order_top == 1:
            reserved_top = (
                db.query(Task)
                .filter(
                    Task.task_type == TaskType.PATIENT_CLOTHES_RENTAL,
                    Task.order_top == 1,
                    Task.status.in_(_ACTIVE_STATUSES),
                )
                .count()
            )
            available_top = inv.clothes_top_count - reserved_top
            if available_top < 1:
                raise HTTPException(
                    status_code=status.HTTP_409_CONFLICT,
                    detail=(
                        f"Insufficient tops: {inv.clothes_top_count} loaded, "
                        f"{reserved_top} already reserved by active tasks"
                    ),
                )

        if order_bottom == 1:
            reserved_bottom = (
                db.query(Task)
                .filter(
                    Task.task_type == TaskType.PATIENT_CLOTHES_RENTAL,
                    Task.order_bottom == 1,
                    Task.status.in_(_ACTIVE_STATUSES),
                )
                .count()
            )
            available_bottom = inv.clothes_bottom_count - reserved_bottom
            if available_bottom < 1:
                raise HTTPException(
                    status_code=status.HTTP_409_CONFLICT,
                    detail=(
                        f"Insufficient bottoms: {inv.clothes_bottom_count} loaded, "
                        f"{reserved_bottom} already reserved by active tasks"
                    ),
                )


def apply_inventory_effect_for_completed_task(
    db: Session, robot_id: int, task: Task
) -> RobotInventory:
    """
    Apply exactly-once inventory mutations for a task that just reached COMPLETE.

    Raises ValueError if any count would go negative — the caller must NOT commit
    and should roll back the enclosing transaction.

    Returns the updated (but not yet committed) RobotInventory object.
    """
    inv = get_or_create_robot_inventory(db, robot_id)

    if task.task_type == TaskType.KIT_REFILL:
        inv.kit_count = ROBOT_KIT_CAPACITY
        logger.info(f"[inventory] robot_id={robot_id} KIT_REFILL → kit_count={inv.kit_count}")

    elif task.task_type == TaskType.CLOTHES_REFILL:
        inv.clothes_top_count    = ROBOT_CLOTHES_TOP_CAPACITY
        inv.clothes_bottom_count = ROBOT_CLOTHES_BOTTOM_CAPACITY
        logger.info(
            f"[inventory] robot_id={robot_id} CLOTHES_REFILL → "
            f"top={inv.clothes_top_count} bottom={inv.clothes_bottom_count}"
        )

    elif task.task_type == TaskType.KIT_DELIVERY:
        if inv.kit_count < 1:
            raise ValueError(
                f"Inventory integrity error: KIT_DELIVERY task_id={task.id} "
                f"would make kit_count negative (current={inv.kit_count})"
            )
        inv.kit_count -= 1
        logger.info(f"[inventory] robot_id={robot_id} KIT_DELIVERY → kit_count={inv.kit_count}")

    elif task.task_type == TaskType.PATIENT_CLOTHES_RENTAL:
        if task.order_top == 1:
            if inv.clothes_top_count < 1:
                raise ValueError(
                    f"Inventory integrity error: PATIENT_CLOTHES_RENTAL task_id={task.id} "
                    f"would make clothes_top_count negative (current={inv.clothes_top_count})"
                )
            inv.clothes_top_count -= 1
        if task.order_bottom == 1:
            if inv.clothes_bottom_count < 1:
                raise ValueError(
                    f"Inventory integrity error: PATIENT_CLOTHES_RENTAL task_id={task.id} "
                    f"would make clothes_bottom_count negative (current={inv.clothes_bottom_count})"
                )
            inv.clothes_bottom_count -= 1
        logger.info(
            f"[inventory] robot_id={robot_id} PATIENT_CLOTHES_RENTAL → "
            f"top={inv.clothes_top_count} bottom={inv.clothes_bottom_count}"
        )

    # All other task types: no inventory change.

    return inv


def robot_inventory_ws_payload(inv: RobotInventory) -> dict:
    """Build the inventory_update WebSocket payload for a robot inventory row."""
    return {
        "type": "robot",
        "kit_count":            inv.kit_count,
        "clothes_top_count":    inv.clothes_top_count,
        "clothes_bottom_count": inv.clothes_bottom_count,
    }
