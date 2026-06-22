"""
MQTT topic constants.  All topics are defined here — do not hardcode strings elsewhere.
"""

# ── Robot → Server ────────────────────────────────────────────────────────
ROBOT_STATUS = "robot/status"
ROBOT_LOCATION = "robot/location"   # Deprecated: no longer required by server contract.
                                     # Server infers location from robot/task_complete destination.
ROBOT_BATTERY = "robot/battery"
ROBOT_ERROR = "robot/error"
ROBOT_TASK_COMPLETE = "robot/task_complete"
ROBOT_LOCK_STATUS = "robot/lock_status"     # v3: lock/unlock status from robot compartment

# ── Server → Robot ────────────────────────────────────────────────────────
SERVER_TASK_ASSIGN = "server/task_assign"
SERVER_TASK_CANCEL = "server/task_cancel"
SERVER_TASK_FINISH = "server/task_finish"   # Patient clothing task completion signal
SERVER_EMERGENCY_CALL = "server/emergency_call"
SERVER_LOCK_COMMAND = "server/lock_command" # v3: tablet UI triggers UNLOCK via server
