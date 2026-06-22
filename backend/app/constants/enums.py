"""
Centralized enums for the hospital AMR system.
Do not scatter these values across the codebase — import from here.
"""
from enum import Enum


class RobotState(str, Enum):
    """Official robot state values."""
    IDLE = "IDLE"
    MOVING = "MOVING"
    ARRIVED = "ARRIVED"
    WAIT_UNLOCK = "WAIT_UNLOCK"           # v3: replaces WAIT_NFC/AUTH_SUCCESS/AUTH_FAIL
    DELIVERY_OPEN_NUR = "DELIVERY_OPEN_NUR"
    DELIVERY_OPEN_PAT = "DELIVERY_OPEN_PAT"
    COMPLETE = "COMPLETE"
    LOW_BATTERY = "LOW_BATTERY"
    CHARGING_BATTERY = "CHARGING_BATTERY"
    ERROR = "ERROR"
    EMERGENCY = "EMERGENCY"               # externally triggered stop (nurse/admin); distinct from ERROR


class LocationType(str, Enum):
    BED = "bed"
    STATION = "station"
    LAUNDRY = "laundry"
    WAREHOUSE = "warehouse"
    SPECIMEN_LAB = "specimen_lab"
    EXAM_A = "exam_A"
    EXAM_B = "exam_B"
    EXAM_C = "exam_C"


class TaskType(str, Enum):
    # Nurse-initiated
    CLOTHES_REFILL = "clothes_refill"
    KIT_DELIVERY = "kit_delivery"
    SPECIMEN_DELIVERY = "specimen_delivery"
    LOGISTICS_DELIVERY = "logistics_delivery"
    USED_CLOTHES_COLLECTION = "used_clothes_collection"
    BATTERY_LOW = "battery_low"
    EMERGENCY_CALL = "emergency_call"
    # Patient-initiated
    PATIENT_CLOTHES_RENTAL = "patient_clothes_rental"
    PATIENT_CLOTHES_RETURN = "patient_clothes_return"


class TaskStatus(str, Enum):
    PENDING = "PENDING"
    DISPATCHED = "DISPATCHED"
    IN_PROGRESS = "IN_PROGRESS"
    COMPLETE = "COMPLETE"
    CANCELLED = "CANCELLED"
    FAILED = "FAILED"


class RequestedByRole(str, Enum):
    NURSE = "nurse"
    PATIENT = "patient"
    SYSTEM = "system"


# ── Priority map (lower number = higher priority) ──────────────────────────
# Priority 1 tasks are device-abnormal: battery_low, ERROR, emergency_call.
TASK_PRIORITY: dict[str, int] = {
    TaskType.BATTERY_LOW:              1,
    TaskType.EMERGENCY_CALL:           1,
    TaskType.SPECIMEN_DELIVERY:        2,
    TaskType.KIT_DELIVERY:             3,
    TaskType.LOGISTICS_DELIVERY:       4,
    TaskType.CLOTHES_REFILL:           5,
    TaskType.PATIENT_CLOTHES_RENTAL:   6,
    TaskType.PATIENT_CLOTHES_RETURN:   6,
    TaskType.USED_CLOTHES_COLLECTION:  7,
}

# Robot states that block normal task dispatch
BLOCKING_ROBOT_STATES: set[str] = {
    RobotState.ERROR,
    RobotState.EMERGENCY,
    RobotState.LOW_BATTERY,
    RobotState.CHARGING_BATTERY,
    RobotState.MOVING,
    RobotState.WAIT_UNLOCK,
    RobotState.DELIVERY_OPEN_NUR,
    RobotState.DELIVERY_OPEN_PAT,
    RobotState.ARRIVED,
    RobotState.COMPLETE,
}

# Robot states considered "abnormal" (device error category)
ABNORMAL_ROBOT_STATES: set[str] = {
    RobotState.ERROR,
    RobotState.LOW_BATTERY,
}
