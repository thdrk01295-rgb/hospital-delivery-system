"""
MQTT topic constants.  All topics are defined here — do not hardcode strings elsewhere.
"""

# ── Robot → Server ────────────────────────────────────────────────────────
ROBOT_STATUS = "robot/status"
ROBOT_LOCATION = "robot/location"
ROBOT_BATTERY = "robot/battery"
ROBOT_ERROR = "robot/error"
ROBOT_TASK_COMPLETE = "robot/task_complete"

# ── Server → Robot ────────────────────────────────────────────────────────
SERVER_TASK_ASSIGN = "server/task_assign"
SERVER_TASK_CANCEL = "server/task_cancel"
SERVER_EMERGENCY_CALL = "server/emergency_call"
