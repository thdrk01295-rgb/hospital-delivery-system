"""
WebSocket event type constants.  Shared with the frontend — keep in sync.
"""

ROBOT_STATE_UPDATE = "robot_state_update"
ROBOT_LOCATION_UPDATE = "robot_location_update"
ROBOT_BATTERY_UPDATE = "robot_battery_update"
TASK_STATUS_UPDATE = "task_status_update"
INVENTORY_UPDATE = "inventory_update"
ABNORMAL_EVENT_UPDATE = "abnormal_event_update"
LOCK_STATUS_UPDATE = "lock_status_update"         # v3: compartment lock/unlock events
TASK_ROUTE_STAGE_UPDATE = "task_route_stage_update"  # route stage advances (origin→destination)
