"""
24 test cases for robot inventory service and related business logic.
"""
import pytest
from fastapi import HTTPException

from app.constants.enums import TaskType, TaskStatus, RequestedByRole
from app.models.robot_inventory import (
    RobotInventory,
    ROBOT_KIT_CAPACITY,
    ROBOT_CLOTHES_TOP_CAPACITY,
    ROBOT_CLOTHES_BOTTOM_CAPACITY,
)
from app.models.task import Task
from app.schemas.task import NurseOrderCreate, PatientClothingRequestCreate
from app.services.robot_inventory_service import (
    get_or_create_robot_inventory,
    validate_inventory_for_task_creation,
    apply_inventory_effect_for_completed_task,
)
from app.services.task_service import finalize_task_complete


# ── Helpers ──────────────────────────────────────────────────────────────────

def _pending_kit_delivery(robot_id):
    return Task(
        task_type=TaskType.KIT_DELIVERY,
        requested_by_role=RequestedByRole.NURSE,
        priority=3,
        status=TaskStatus.PENDING,
        assigned_robot_id=robot_id,
    )


def _pending_rental(robot_id, order_top=0, order_bottom=0):
    return Task(
        task_type=TaskType.PATIENT_CLOTHES_RENTAL,
        requested_by_role=RequestedByRole.PATIENT,
        priority=6,
        status=TaskStatus.PENDING,
        order_top=order_top,
        order_bottom=order_bottom,
    )


# ══════════════════════════════════════════════════════════════════════════════
# §3 — get_or_create_robot_inventory
# ══════════════════════════════════════════════════════════════════════════════

def test_get_or_create_returns_existing(db, robot_row, inv):
    """TC-01: returns existing row without creating a duplicate."""
    result = get_or_create_robot_inventory(db, robot_row.id)
    assert result.id == inv.id


def test_get_or_create_seeds_with_capacity(db, robot_row):
    """TC-02: creates a new row seeded to full capacity values."""
    result = get_or_create_robot_inventory(db, robot_row.id)
    assert result.kit_count == ROBOT_KIT_CAPACITY
    assert result.clothes_top_count == ROBOT_CLOTHES_TOP_CAPACITY
    assert result.clothes_bottom_count == ROBOT_CLOTHES_BOTTOM_CAPACITY


# ══════════════════════════════════════════════════════════════════════════════
# §6 — validate_inventory_for_task_creation
# ══════════════════════════════════════════════════════════════════════════════

def test_validate_kit_delivery_passes_with_stock(db, robot_row, inv):
    """TC-03: kit_delivery validation passes when kits are available."""
    validate_inventory_for_task_creation(db, robot_row.id, TaskType.KIT_DELIVERY)


def test_validate_kit_delivery_raises_409_no_stock(db, robot_row, inv):
    """TC-04: kit_delivery raises HTTP 409 when kit_count=0."""
    inv.kit_count = 0
    db.flush()
    with pytest.raises(HTTPException) as exc_info:
        validate_inventory_for_task_creation(db, robot_row.id, TaskType.KIT_DELIVERY)
    assert exc_info.value.status_code == 409


def test_validate_kit_delivery_raises_409_all_reserved_pending(db, robot_row, inv):
    """TC-05: kit_delivery raises 409 when all kits are reserved by PENDING tasks."""
    inv.kit_count = 1
    db.flush()
    t = _pending_kit_delivery(robot_row.id)
    db.add(t)
    db.flush()
    with pytest.raises(HTTPException) as exc_info:
        validate_inventory_for_task_creation(db, robot_row.id, TaskType.KIT_DELIVERY)
    assert exc_info.value.status_code == 409


def test_validate_kit_delivery_raises_409_all_reserved_dispatched(db, robot_row, inv):
    """TC-06: kit_delivery raises 409 when all kits are reserved by DISPATCHED tasks."""
    inv.kit_count = 1
    db.flush()
    t = _pending_kit_delivery(robot_row.id)
    t.status = TaskStatus.DISPATCHED
    db.add(t)
    db.flush()
    with pytest.raises(HTTPException) as exc_info:
        validate_inventory_for_task_creation(db, robot_row.id, TaskType.KIT_DELIVERY)
    assert exc_info.value.status_code == 409


def test_validate_kit_refill_skips_check(db, robot_row, inv):
    """TC-07: kit_refill has no inventory check (always passes)."""
    inv.kit_count = 0
    db.flush()
    validate_inventory_for_task_creation(db, robot_row.id, TaskType.KIT_REFILL)


def test_validate_rental_raises_409_no_top(db, robot_row, inv):
    """TC-08: patient_clothes_rental raises 409 when clothes_top_count=0 and order_top=1."""
    inv.clothes_top_count = 0
    db.flush()
    with pytest.raises(HTTPException) as exc_info:
        validate_inventory_for_task_creation(
            db, robot_row.id, TaskType.PATIENT_CLOTHES_RENTAL, order_top=1
        )
    assert exc_info.value.status_code == 409


def test_validate_rental_raises_409_no_bottom(db, robot_row, inv):
    """TC-09: patient_clothes_rental raises 409 when clothes_bottom_count=0 and order_bottom=1."""
    inv.clothes_bottom_count = 0
    db.flush()
    with pytest.raises(HTTPException) as exc_info:
        validate_inventory_for_task_creation(
            db, robot_row.id, TaskType.PATIENT_CLOTHES_RENTAL, order_bottom=1
        )
    assert exc_info.value.status_code == 409


def test_validate_rental_passes_top_only_when_bottom_exhausted(db, robot_row, inv):
    """TC-10: order_bottom=0 skips the bottom check even when bottom stock is zero."""
    inv.clothes_bottom_count = 0
    db.flush()
    validate_inventory_for_task_creation(
        db, robot_row.id, TaskType.PATIENT_CLOTHES_RENTAL, order_top=1, order_bottom=0
    )


def test_validate_rental_raises_409_when_top_reserved(db, robot_row, inv):
    """TC-11: raises 409 for top when all tops are reserved by active rental tasks."""
    inv.clothes_top_count = 1
    db.flush()
    t = _pending_rental(robot_row.id, order_top=1)
    db.add(t)
    db.flush()
    with pytest.raises(HTTPException) as exc_info:
        validate_inventory_for_task_creation(
            db, robot_row.id, TaskType.PATIENT_CLOTHES_RENTAL, order_top=1
        )
    assert exc_info.value.status_code == 409


# ══════════════════════════════════════════════════════════════════════════════
# §5 — apply_inventory_effect_for_completed_task
# ══════════════════════════════════════════════════════════════════════════════

def test_apply_kit_delivery_decrements_kit(db, robot_row, inv):
    """TC-12: KIT_DELIVERY decrements kit_count by 1."""
    t = Task(task_type=TaskType.KIT_DELIVERY, requested_by_role=RequestedByRole.NURSE, priority=3, status=TaskStatus.COMPLETE)
    db.add(t)
    db.flush()
    before = inv.kit_count
    apply_inventory_effect_for_completed_task(db, robot_row.id, t)
    assert inv.kit_count == before - 1


def test_apply_kit_delivery_raises_on_negative(db, robot_row, inv):
    """TC-13: KIT_DELIVERY raises ValueError when kit_count=0."""
    inv.kit_count = 0
    db.flush()
    t = Task(task_type=TaskType.KIT_DELIVERY, requested_by_role=RequestedByRole.NURSE, priority=3, status=TaskStatus.COMPLETE)
    db.add(t)
    db.flush()
    with pytest.raises(ValueError, match="kit_count negative"):
        apply_inventory_effect_for_completed_task(db, robot_row.id, t)


def test_apply_kit_refill_resets_to_capacity(db, robot_row, inv):
    """TC-14: KIT_REFILL sets kit_count back to ROBOT_KIT_CAPACITY."""
    inv.kit_count = 2
    db.flush()
    t = Task(task_type=TaskType.KIT_REFILL, requested_by_role=RequestedByRole.NURSE, priority=5, status=TaskStatus.COMPLETE)
    db.add(t)
    db.flush()
    apply_inventory_effect_for_completed_task(db, robot_row.id, t)
    assert inv.kit_count == ROBOT_KIT_CAPACITY


def test_apply_clothes_refill_resets_to_capacity(db, robot_row, inv):
    """TC-15: CLOTHES_REFILL sets both top and bottom counts to capacity."""
    inv.clothes_top_count = 1
    inv.clothes_bottom_count = 2
    db.flush()
    t = Task(task_type=TaskType.CLOTHES_REFILL, requested_by_role=RequestedByRole.NURSE, priority=5, status=TaskStatus.COMPLETE)
    db.add(t)
    db.flush()
    apply_inventory_effect_for_completed_task(db, robot_row.id, t)
    assert inv.clothes_top_count == ROBOT_CLOTHES_TOP_CAPACITY
    assert inv.clothes_bottom_count == ROBOT_CLOTHES_BOTTOM_CAPACITY


def test_apply_rental_decrements_top(db, robot_row, inv):
    """TC-16: PATIENT_CLOTHES_RENTAL with order_top=1 decrements clothes_top_count."""
    before = inv.clothes_top_count
    t = Task(task_type=TaskType.PATIENT_CLOTHES_RENTAL, requested_by_role=RequestedByRole.PATIENT,
             priority=6, status=TaskStatus.COMPLETE, order_top=1, order_bottom=0)
    db.add(t)
    db.flush()
    apply_inventory_effect_for_completed_task(db, robot_row.id, t)
    assert inv.clothes_top_count == before - 1
    assert inv.clothes_bottom_count == ROBOT_CLOTHES_BOTTOM_CAPACITY


def test_apply_rental_decrements_bottom(db, robot_row, inv):
    """TC-17: PATIENT_CLOTHES_RENTAL with order_bottom=1 decrements clothes_bottom_count."""
    before = inv.clothes_bottom_count
    t = Task(task_type=TaskType.PATIENT_CLOTHES_RENTAL, requested_by_role=RequestedByRole.PATIENT,
             priority=6, status=TaskStatus.COMPLETE, order_top=0, order_bottom=1)
    db.add(t)
    db.flush()
    apply_inventory_effect_for_completed_task(db, robot_row.id, t)
    assert inv.clothes_bottom_count == before - 1


def test_apply_rental_raises_on_top_negative(db, robot_row, inv):
    """TC-18: PATIENT_CLOTHES_RENTAL raises ValueError when top would go negative."""
    inv.clothes_top_count = 0
    db.flush()
    t = Task(task_type=TaskType.PATIENT_CLOTHES_RENTAL, requested_by_role=RequestedByRole.PATIENT,
             priority=6, status=TaskStatus.COMPLETE, order_top=1, order_bottom=0)
    db.add(t)
    db.flush()
    with pytest.raises(ValueError, match="clothes_top_count negative"):
        apply_inventory_effect_for_completed_task(db, robot_row.id, t)


def test_apply_rental_raises_on_bottom_negative(db, robot_row, inv):
    """TC-19: PATIENT_CLOTHES_RENTAL raises ValueError when bottom would go negative."""
    inv.clothes_bottom_count = 0
    db.flush()
    t = Task(task_type=TaskType.PATIENT_CLOTHES_RENTAL, requested_by_role=RequestedByRole.PATIENT,
             priority=6, status=TaskStatus.COMPLETE, order_top=0, order_bottom=1)
    db.add(t)
    db.flush()
    with pytest.raises(ValueError, match="clothes_bottom_count negative"):
        apply_inventory_effect_for_completed_task(db, robot_row.id, t)


def test_apply_no_effect_for_other_types(db, robot_row, inv):
    """TC-20: task types with no inventory effect leave counts unchanged."""
    before_kit = inv.kit_count
    before_top = inv.clothes_top_count
    t = Task(task_type=TaskType.SPECIMEN_DELIVERY, requested_by_role=RequestedByRole.NURSE,
             priority=2, status=TaskStatus.COMPLETE)
    db.add(t)
    db.flush()
    apply_inventory_effect_for_completed_task(db, robot_row.id, t)
    assert inv.kit_count == before_kit
    assert inv.clothes_top_count == before_top


# ══════════════════════════════════════════════════════════════════════════════
# §8 — finalize_task_complete idempotency
# ══════════════════════════════════════════════════════════════════════════════

def test_finalize_sets_inventory_applied(db, robot_row, inv):
    """TC-21: finalize_task_complete sets inventory_applied=True."""
    t = Task(task_type=TaskType.KIT_DELIVERY, requested_by_role=RequestedByRole.NURSE,
             priority=3, status=TaskStatus.DISPATCHED, assigned_robot_id=robot_row.id)
    db.add(t)
    db.commit()
    db.refresh(t)
    result = finalize_task_complete(db, t.id, robot_id=robot_row.id)
    assert result.inventory_applied is True
    assert result.status == TaskStatus.COMPLETE


def test_finalize_is_idempotent(db, robot_row, inv):
    """TC-22: second finalize_task_complete call returns same task without re-applying inventory."""
    inv.kit_count = ROBOT_KIT_CAPACITY
    t = Task(task_type=TaskType.KIT_DELIVERY, requested_by_role=RequestedByRole.NURSE,
             priority=3, status=TaskStatus.DISPATCHED, assigned_robot_id=robot_row.id)
    db.add(t)
    db.commit()
    db.refresh(t)

    result1 = finalize_task_complete(db, t.id, robot_id=robot_row.id)
    kit_after_first = db.query(
        __import__("app.models.robot_inventory", fromlist=["RobotInventory"]).RobotInventory
    ).filter_by(robot_id=robot_row.id).first().kit_count

    result2 = finalize_task_complete(db, t.id, robot_id=robot_row.id)
    kit_after_second = db.query(
        __import__("app.models.robot_inventory", fromlist=["RobotInventory"]).RobotInventory
    ).filter_by(robot_id=robot_row.id).first().kit_count

    assert result1.id == result2.id
    assert kit_after_first == kit_after_second  # no double-deduction


# ══════════════════════════════════════════════════════════════════════════════
# §9 — Originless task contract (schema validation)
# ══════════════════════════════════════════════════════════════════════════════

def test_nurse_order_kit_delivery_rejects_origin():
    """TC-23: NurseOrderCreate raises ValidationError for kit_delivery with origin_location_id."""
    with pytest.raises(Exception):
        NurseOrderCreate(
            task_type=TaskType.KIT_DELIVERY,
            origin_location_id=1,
            destination_location_id=2,
        )


def test_nurse_order_kit_refill_rejects_origin():
    """TC-24: NurseOrderCreate raises ValidationError for kit_refill with origin_location_id."""
    with pytest.raises(Exception):
        NurseOrderCreate(
            task_type=TaskType.KIT_REFILL,
            origin_location_id=1,
        )
