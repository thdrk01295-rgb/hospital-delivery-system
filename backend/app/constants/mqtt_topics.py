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

# ── Server → Robot ────────────────────────────────────────────────────────
SERVER_TASK_ASSIGN = "server/task_assign"
SERVER_TASK_CANCEL = "server/task_cancel"
SERVER_TASK_FINISH = "server/task_finish"   # Patient clothing task completion signal
SERVER_EMERGENCY_CALL = "server/emergency_call"
