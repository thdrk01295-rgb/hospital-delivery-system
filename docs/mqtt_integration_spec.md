# Hospital Logistics AMR — MQTT Integration Spec

**Audience:** Robot firmware / embedded team  
**Source of truth:** `backend/app/mqtt/`, `backend/app/constants/mqtt_topics.py`, `backend/app/schemas/robot.py`, `backend/app/scheduler/dispatcher.py`

---

## 1. Overview

The server (FastAPI backend) and the AMR robot communicate exclusively over MQTT. The broker is Mosquitto (default: `localhost:1883`).

| Role | Responsibility |
|---|---|
| **Robot** | Publishes state, location, battery, error, and task-complete events |
| **Server** | Subscribes to all robot topics; publishes task assignments, cancellations, and emergency calls |

The server uses incoming MQTT messages to update its database and push real-time events to the nurse/patient dashboards via WebSocket. The robot uses incoming MQTT messages to know which task to execute next.

---

## 2. Topic List

### Robot → Server

| Topic | Purpose |
|---|---|
| `robot/status` | Robot state changed (IDLE, MOVING, ARRIVED, etc.) |
| `robot/location` | Robot arrived at or departed from a known location |
| `robot/battery` | Battery level update |
| `robot/error` | Robot encountered an error |
| `robot/task_complete` | Robot finished executing an assigned task |

### Server → Robot

| Topic | Purpose | Status |
|---|---|---|
| `server/task_assign` | Assign the next task to the robot | ✅ Implemented and published |
| `server/task_cancel` | Cancel a task currently held by the robot | ⚠️ Topic defined, not yet published |
| `server/emergency_call` | Emergency station call override | ⚠️ Topic defined, not yet published |

---

## 3. Payload Specifications

All payloads are JSON-encoded UTF-8 strings. All timestamps must be ISO 8601 with timezone (e.g. `"2026-04-03T09:00:00+00:00"`).

---

### `robot/status` — Robot → Server

**Server handler:** `_handle_robot_status` in `mqtt/handlers.py`  
**Server action:**
- Updates `robots.current_state` in DB
- Broadcasts `robot_state_update` WebSocket event to dashboards
- If state is `LOW_BATTERY`: opens a `low_battery` abnormal event
- If state is `IDLE`: resolves open `error`/`low_battery` events, then triggers task dispatch

| Field | Type | Required | Notes |
|---|---|---|---|
| `robot_id` | string | ✅ | Must match `robots.robot_code` in DB (see §4) |
| `state` | string | ✅ | Must be a valid `RobotState` enum value (see §4) |
| `timestamp` | string (ISO 8601) | ✅ | Time of state change on robot side |

```json
{
  "robot_id": "AMR-001",
  "state": "IDLE",
  "timestamp": "2026-04-03T09:00:00+00:00"
}
```

---

### `robot/location` — Robot → Server

**Server handler:** `_handle_robot_location` in `mqtt/handlers.py`  
**Server action:**
- Looks up `location_code` in `locations` table
- Sets `robots.current_location_id` to the matched row's `id`
- Broadcasts `robot_location_update` WebSocket event

| Field | Type | Required | Notes |
|---|---|---|---|
| `robot_id` | string | ✅ | Must match `robots.robot_code` |
| `location_code` | string | ✅ | Must exactly match a `locations.location_code` value (see §4) |
| `timestamp` | string (ISO 8601) | ✅ | Time of location report |

```json
{
  "robot_id": "AMR-001",
  "location_code": "station",
  "timestamp": "2026-04-03T09:01:00+00:00"
}
```

> **Important:** If `location_code` does not match any row in `locations`, the DB update is silently skipped and `current_location_id` stays unchanged. The robot must publish only codes that exist in the seeded `locations` table.

---

### `robot/battery` — Robot → Server

**Server handler:** `_handle_robot_battery` in `mqtt/handlers.py`  
**Server action:**
- Updates `robots.battery_percent` in DB
- Broadcasts `robot_battery_update` WebSocket event

| Field | Type | Required | Notes |
|---|---|---|---|
| `robot_id` | string | ✅ | Must match `robots.robot_code` |
| `battery_percent` | float | ✅ | Range `0.0`–`100.0` |
| `timestamp` | string (ISO 8601) | ✅ | |

```json
{
  "robot_id": "AMR-001",
  "battery_percent": 73.5,
  "timestamp": "2026-04-03T09:02:00+00:00"
}
```

> The server-side low-battery threshold is `LOW_BATTERY_THRESHOLD = 20` (configurable via `.env`). The server does **not** watch this field for threshold crossing — threshold logic is triggered by `robot/status` with state `LOW_BATTERY`, not by this topic directly.

---

### `robot/error` — Robot → Server

**Server handler:** `_handle_robot_error` in `mqtt/handlers.py`  
**Server action:**
- Sets `robots.current_state = ERROR` in DB
- Opens an `error` abnormal event with the provided message
- Broadcasts `robot_state_update` and `abnormal_event_update` WebSocket events

| Field | Type | Required | Notes |
|---|---|---|---|
| `robot_id` | string | ✅ | Must match `robots.robot_code` |
| `error_message` | string | ✅ | Human-readable description stored in `abnormal_events.note` |
| `timestamp` | string (ISO 8601) | ✅ | |

```json
{
  "robot_id": "AMR-001",
  "error_message": "Motor controller fault on right wheel",
  "timestamp": "2026-04-03T09:03:00+00:00"
}
```

> To clear an error state, publish `robot/status` with `state: "IDLE"`. The server will resolve all open `error` abnormal events automatically.

---

### `robot/task_complete` — Robot → Server

**Server handler:** `_handle_task_complete` in `mqtt/handlers.py`  
**Server action:**
- Sets `tasks.status = COMPLETE` and `tasks.completed_at = now()` for the given `task_id`
- Broadcasts `task_status_update` WebSocket event

| Field | Type | Required | Notes |
|---|---|---|---|
| `robot_id` | string | ✅ | Must match `robots.robot_code` |
| `task_id` | integer | ✅ | Must match the `tasks.id` received in the prior `server/task_assign` |
| `timestamp` | string (ISO 8601) | ✅ | |

```json
{
  "robot_id": "AMR-001",
  "task_id": 42,
  "timestamp": "2026-04-03T09:10:00+00:00"
}
```

---

### `server/task_assign` — Server → Robot

**Published by:** `dispatcher.py` → `_publish_task_assignment()`  
**Triggered when:** Robot publishes `robot/status` with `state: "IDLE"` and a PENDING task exists  
**Robot action:** Execute the task; navigate to `destination`; publish state updates during travel; publish `robot/task_complete` when done

| Field | Type | Notes |
|---|---|---|
| `task_id` | integer | Round-trip this value back in `robot/task_complete` |
| `task_type` | string | One of the `TaskType` enum values (see §4) |
| `origin` | string or null | `location_code` of departure point; may be null if robot has no recorded location |
| `destination` | string or null | `location_code` of target; may be null in rare cases |
| `priority` | integer | Lower = higher priority (1 = emergency) |

```json
{
  "task_id": 42,
  "task_type": "kit_delivery",
  "origin": "station",
  "destination": "11011",
  "priority": 3
}
```

---

### `server/task_cancel` — Server → Robot

**Status:** Topic string defined in `mqtt_topics.py`. **No publish call exists yet** in the current server code. The server does not currently notify the robot when a task is cancelled by a nurse.

When implemented, the expected shape (based on the task model) would be:

```json
{
  "task_id": 42
}
```

> ⚠️ Do not treat this as a guaranteed contract yet. Confirm with the server team when this publish is added.

---

### `server/emergency_call` — Server → Robot

**Status:** Topic string defined in `mqtt_topics.py`. **No publish call exists yet** in the current server code.

When implemented, the expected shape would be:

```json
{
  "task_id": 7,
  "task_type": "emergency_call",
  "priority": 1
}
```

> ⚠️ Same caveat as `server/task_cancel` — confirm when implemented.

---

## 4. Required Conventions

### `robot_id` / `robot_code`

The default robot code used by the server is `"AMR-001"`. This is the value in `robot_service.py`:

```python
def get_or_create_robot(db, robot_code="AMR-001") -> Robot
```

The server will auto-create a `robots` row on first MQTT message if none exists. The robot must use `"AMR-001"` as its `robot_id` in all payloads unless the server team changes the default.

---

### `location_code` matching rule

All location codes published on `robot/location` must exactly match a `location_code` value in the `locations` table. Non-bed locations use plain string codes:

| location_code | Location |
|---|---|
| `station` | 스테이션 |
| `laundry` | 세탁실 |
| `warehouse` | 창고 |
| `specimen_lab` | 검체실 |
| `exam_A` | 검사실 A |
| `exam_B` | 검사실 B |
| `exam_C` | 검사실 C |

Bed location codes are 5-digit strings built as: `floor + room + bed` (no separators, no zero-padding).

Examples:

| Description | location_code |
|---|---|
| 1층 101호 1번 침상 | `11011` |
| 2층 304호 5번 침상 | `23045` |
| 4층 408호 6번 침상 | `44086` |

The pattern is: `str(floor) + str(floor*100 + room_num) + str(bed)`. Source: `backend/app/utils/location_utils.py → build_bed_code()`.

---

### `task_id` round-trip rule

When the robot publishes `robot/task_complete`, the `task_id` must exactly match the integer `task_id` received in `server/task_assign`. The server looks up the task by this ID to mark it complete. Mismatched or stale IDs will cause the task to remain PENDING indefinitely.

---

### `RobotState` valid values

Source: `backend/app/constants/enums.py`

```
IDLE
MOVING
ARRIVED
WAIT_NFC
AUTH_SUCCESS
AUTH_FAIL
DELIVERY_OPEN_NUR
DELIVERY_OPEN_PAT
COMPLETE
LOW_BATTERY
CHAGING_BATTERY     ← note: intentional spec spelling
ERROR
```

The state `IDLE` is the trigger for task dispatch. The states `DELIVERY_OPEN_NUR` and `DELIVERY_OPEN_PAT` trigger UI changes on nurse and patient dashboards respectively.

---

### `TaskType` valid values

Source: `backend/app/constants/enums.py`

```
clothes_refill
kit_delivery
specimen_delivery
logistics_delivery
used_clothes_collection
battery_low
emergency_call
patient_clothes_rental
patient_clothes_return
```

---

### Battery convention

- Publish `robot/battery` for periodic level updates
- Publish `robot/status` with `state: "LOW_BATTERY"` when the robot's own threshold is crossed — this is what triggers the server's abnormal event, **not** the battery percentage value

---

## 5. Implementation Status

| Feature | Status |
|---|---|
| Server subscribes to all 5 robot→server topics | ✅ Complete |
| `robot/status` handler → DB update + WS broadcast | ✅ Complete |
| `robot/location` handler → DB update + WS broadcast | ✅ Complete |
| `robot/battery` handler → DB update + WS broadcast | ✅ Complete |
| `robot/error` handler → DB update + abnormal event + WS broadcast | ✅ Complete |
| `robot/task_complete` handler → task COMPLETE + WS broadcast | ✅ Complete |
| `server/task_assign` published on robot IDLE | ✅ Complete |
| Auto-dispatch: picks highest-priority PENDING task when robot goes IDLE | ✅ Complete |
| Auto-task creation on inventory threshold crossing | ✅ Complete |
| `server/task_cancel` publish when nurse cancels a task | ⚠️ Topic defined, not yet published |
| `server/emergency_call` publish when nurse triggers emergency | ⚠️ Topic defined, not yet published |

---

## 6. Pre-MQTT Assumptions

**One robot:** The dispatcher assumes a single robot (`db.query(Robot).first()`). Multi-robot is not supported in the current dispatch logic.

**No robot row at startup:** `get_or_create_robot()` will auto-create the row on first MQTT message. Before that, `GET /api/robot/status` returns a default/null state.

**Null robot location:** Before the robot publishes its first `robot/location` message, `robots.current_location_id` is `NULL`. Patient task creation uses this field as the task's `origin_location_id` — it will be stored as `NULL` until the robot has reported its first location. This is safe and expected pre-MQTT.

**Task dispatch prerequisite:** The robot must publish `robot/status` with `state: "IDLE"` to trigger the dispatcher. Tasks sitting as `PENDING` will not be sent until an IDLE message is received.

---

## 7. Test Messages (Copy-Paste Ready)

Use these with `mosquitto_pub` or any MQTT client. Replace broker address as needed.

```bash
# robot/status — robot is IDLE (triggers task dispatch)
mosquitto_pub -h localhost -p 1883 -t "robot/status" -m '{
  "robot_id": "AMR-001",
  "state": "IDLE",
  "timestamp": "2026-04-03T09:00:00+00:00"
}'

# robot/status — robot is moving
mosquitto_pub -h localhost -p 1883 -t "robot/status" -m '{
  "robot_id": "AMR-001",
  "state": "MOVING",
  "timestamp": "2026-04-03T09:01:00+00:00"
}'

# robot/location — robot arrived at station
mosquitto_pub -h localhost -p 1883 -t "robot/location" -m '{
  "robot_id": "AMR-001",
  "location_code": "station",
  "timestamp": "2026-04-03T09:01:30+00:00"
}'

# robot/location — robot arrived at a bed
mosquitto_pub -h localhost -p 1883 -t "robot/location" -m '{
  "robot_id": "AMR-001",
  "location_code": "11011",
  "timestamp": "2026-04-03T09:04:00+00:00"
}'

# robot/battery — periodic battery report
mosquitto_pub -h localhost -p 1883 -t "robot/battery" -m '{
  "robot_id": "AMR-001",
  "battery_percent": 68.0,
  "timestamp": "2026-04-03T09:05:00+00:00"
}'

# robot/error — motor fault
mosquitto_pub -h localhost -p 1883 -t "robot/error" -m '{
  "robot_id": "AMR-001",
  "error_message": "Motor controller fault on right wheel",
  "timestamp": "2026-04-03T09:06:00+00:00"
}'

# robot/task_complete — task 42 finished
mosquitto_pub -h localhost -p 1883 -t "robot/task_complete" -m '{
  "robot_id": "AMR-001",
  "task_id": 42,
  "timestamp": "2026-04-03T09:10:00+00:00"
}'
```

Expected outbound from server (subscribe to verify):

```bash
# Subscribe to all server→robot topics
mosquitto_sub -h localhost -p 1883 -t "server/#" -v

# Expected server/task_assign payload when a PENDING task exists and robot goes IDLE:
# server/task_assign
{
  "task_id": 42,
  "task_type": "kit_delivery",
  "origin": "station",
  "destination": "11011",
  "priority": 3
}
```

> `server/task_cancel` and `server/emergency_call` are not yet published by the server. Subscribe to them to verify once that publish is implemented.

---

## 8. Key Source Files

| File | What to read |
|---|---|
| `backend/app/constants/mqtt_topics.py` | All topic strings |
| `backend/app/mqtt/handlers.py` | Exact inbound payload handling and DB side-effects |
| `backend/app/schemas/robot.py` | Pydantic schemas — authoritative field names and types |
| `backend/app/scheduler/dispatcher.py` | `server/task_assign` payload construction |
| `backend/app/services/robot_service.py` | How `robot_id` maps to DB row; `get_or_create_robot` |
| `backend/app/constants/enums.py` | All valid `RobotState` and `TaskType` values |
| `backend/app/utils/location_utils.py` | `build_bed_code()` — bed location_code generation rule |
