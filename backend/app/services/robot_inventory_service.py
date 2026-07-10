"""
Service for managing per-robot item inventory (kits, clothes).
Inventory is updated atomically with task completion via finalize_task_complete.
"""
import logging
from typing import Optional

from sqlalchemy.orm import Session

from app.models.robot_inventory import (
    RobotInventory,
    ROBOT_KIT_CAPACITY,
    ROBOT_CLOTHES_TOP_CAPACITY,
    ROBOT_CLOTHES_BOTTOM_CAPACITY,
)
from app.models.task import Task
from app.constants.enums import TaskType

logger = logging.getLogger(__name__)


def get_or_create_robot_inventory(db: Session, robot_id: int) -> RobotInventory:
    inv = db.query(RobotInventory).filter(RobotInventory.robot_id == robot_id).first()
    if not inv:
        inv = RobotInventory(robot_id=robot_id)
        db.add(inv)
        db.flush()
        logger.info(f"[inventory] Created new RobotInventory for robot_id={robot_id}")
    return inv


def update_robot_inventory_on_complete(db: Session, robot_id: int, task: Task) -> None:
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
        inv.kit_count = max(0, inv.kit_count - 1)
        logger.info(f"[inventory] robot_id={robot_id} KIT_DELIVERY → kit_count={inv.kit_count}")

    elif task.task_type == TaskType.PATIENT_CLOTHES_RENTAL:
        if task.order_top == 1:
            inv.clothes_top_count = max(0, inv.clothes_top_count - 1)
        if task.order_bottom == 1:
            inv.clothes_bottom_count = max(0, inv.clothes_bottom_count - 1)
        logger.info(
            f"[inventory] robot_id={robot_id} PATIENT_CLOTHES_RENTAL → "
            f"top={inv.clothes_top_count} bottom={inv.clothes_bottom_count}"
        )
