# Hospital Logistics AMR — MQTT Integration Spec

**Audience:** Robot firmware / embedded team  
**Source of truth:** `backend/app/mqtt/`, `backend/app/constants/mqtt_topics.py`, `backend/app/schemas/robot.py`, `backend/app/scheduler/dispatcher.py`  
**Last updated:** 2026-05-31

---

## 1. Overview

The server (FastAPI backend) and the AMR robot communicate exclusively over MQTT. The broker is Mosquitto (default: `localhost:1883`).

| Role | Responsibility |
|---|---|
| **Robot** | Publishes state, location, battery, error, and task-complete events |
| **Server** | Subscribes to all robot topics; publishes task assignments, cancellations, and emergency calls |

The server uses incoming MQTT messages to update its database and push real-time events to the nurse/patient dashboards via WebSocket. The robot uses incoming MQTT messages to know which task to execute next and when to stop or resume.

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
| `server/task_assign` | Assign the next task to the robot | ✅ Implemented |
| `server/task_cancel` | Cancel a task currently held by the robot | ⚠️ Topic defined, not yet published |
| `server/emergency_call` | Emergency STOP or RELEASE command | ✅ Implemented |

---

## 3. Payload Specifications

All payloads are JSON-encoded UTF-8 strings. All timestamps must be ISO 8601 with timezone (e.g. `"2026-05-31T09:00:00+00:00"`).

---

### `robot/status` — Robot → Server

**Server handler:** `_handle_robot_status` in `mqtt/handlers.py`  
**Server actions:**
- Updates `robots.current_state` in DB
- Broadcasts `robot_state_update` WebSocket event to dashboards
- If state is `LOW_BATTERY`: opens a `low_battery` abnormal event
- If state is `IDLE`: resolves open `error`/`low_battery` events, then triggers task dispatch

| Field | Type | Required | Notes |
|---|---|---|---|
| `robot_id` | string | ✅ | Must match `robots.robot_code` in DB — use `"AMR-001"` |
| `state` | string | ✅ | Must be a valid `RobotState` value (see §4) |
| `timestamp` | string (ISO 8601) | ✅ | Time of state change on robot side |

```json
{
  "robot_id": "AMR-001",
  "state": "IDLE",
  "timestamp": "2026-05-31T09:00:00+00:00"
}
```

---

### `robot/location` — Robot → Server

**Server handler:** `_handle_robot_location` in `mqtt/handlers.py`  
**Server actions:**
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
  "location_code": "STATION-01",
  "timestamp": "2026-05-31T09:01:00+00:00"
}
```

> **Important:** If `location_code` does not match any row in `locations`, the DB update is silently skipped. The robot must publish only codes that exist in the seeded `locations` table.

---

### `robot/battery` — Robot → Server

**Server handler:** `_handle_robot_battery` in `mqtt/handlers.py`  
**Server actions:**
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
  "timestamp": "2026-05-31T09:02:00+00:00"
}
```

> The server does **not** derive low-battery state from this percentage value. Low-battery state is triggered by `robot/status` with `state: "LOW_BATTERY"`, not by this topic.

---

### `robot/error` — Robot → Server

**Server handler:** `_handle_robot_error` in `mqtt/handlers.py`  
**Server actions:**
- Forces `robots.current_state = "ERROR"` in DB
- Opens an `error` abnormal event with the provided message (idempotent — duplicates are suppressed)
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
  "timestamp": "2026-05-31T09:03:00+00:00"
}
```

> To clear an error state, publish `robot/status` with `state: "IDLE"`. The server resolves all open `error` abnormal events automatically on IDLE.

---

### `robot/task_complete` — Robot → Server

**Server handler:** `_handle_task_complete` in `mqtt/handlers.py`  
**Server actions:**
- Sets `tasks.status = "COMPLETE"` and `tasks.completed_at = now()` for the given `task_id`
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
  "timestamp": "2026-05-31T09:10:00+00:00"
}
```

> **Patient clothing tasks:** For `patient_clothes_rental` and `patient_clothes_return`, task completion is triggered by the patient pressing a button in the web UI (`POST /tasks/{task_id}/complete`). The robot does **not** publish `robot/task_complete` for these task types.

---

### `server/task_assign` — Server → Robot

**Published by:** `dispatcher.py → _publish_task_assignment()`  
**Triggered when:** Robot publishes `robot/status` with `state: "IDLE"` and a PENDING task exists  
**Robot action:** Navigate to `destination`; publish state updates; publish `robot/task_complete` (or `robot/status IDLE`) when done

| Field | Type | Notes |
|---|---|---|
| `task_id` | integer | Round-trip this value back in `robot/task_complete` |
| `task_type` | string | One of the `TaskType` values (see §4) |
| `origin` | string or null | `location_code` of departure point; null if robot has no recorded location |
| `destination` | string or null | `location_code` of delivery target; null for tasks without a fixed destination |
| `priority` | integer | Lower = higher priority (1 = emergency/battery) |

```json
{
  "task_id": 42,
  "task_type": "kit_delivery",
  "origin": "STATION-01",
  "destination": "11011",
  "priority": 3
}
```

---

### `server/task_cancel` — Server → Robot

**Status:** Topic string defined in `mqtt_topics.py`. **No publish call exists yet** in the current server code. When a nurse cancels a task via the web UI, the DB is updated but the robot is not notified over MQTT.

When implemented, the expected payload:

```json
{
  "task_id": 42
}
```

> ⚠️ Do not implement a handler for this topic yet. Confirm with the server team when the publish is added.

---

### `server/emergency_call` — Server → Robot

**Published by:** `routers/tasks.py → trigger_emergency()` and `release_emergency()`  
**Robot action:** On `"STOP"` — halt immediately, transition to `EMERGENCY` state, publish `robot/status { state: "EMERGENCY" }`. On `"RELEASE"` — transition to `IDLE`, publish `robot/status { state: "IDLE" }`.

The `command` field distinguishes STOP from RELEASE.

**STOP — published when nurse triggers emergency:**
```json
{
  "robot_id": "AMR-001",
  "command": "STOP"
}
```

**RELEASE — published when nurse releases emergency:**
```json
{
  "robot_id": "AMR-001",
  "command": "RELEASE"
}
```

See §6 for the full emergency call flow and task handling contract.

---

## 4. Required Conventions

### `robot_id` / `robot_code`

The server robot code is `"AMR-001"`. Source: `robot_service.py → get_or_create_robot(db, robot_code="AMR-001")`.

The server auto-creates the `robots` row on first MQTT message. The robot must use `"AMR-001"` as `robot_id` in all payloads.

---

### `location_code` matching rule

All location codes published on `robot/location` must exactly match a `location_code` value in the `locations` table. Non-bed locations use plain string codes:

| location_code | Location |
|---|---|
| `STATION-01` | 스테이션 |
| `LAUNDRY-01` | 세탁실 |
| `WAREHOUSE-01` | 창고 |
| `SPECIMEN-LAB` | 검체실 |
| `EXAM-A` | 검사실 A |
| `EXAM-B` | 검사실 B |
| `EXAM-C` | 검사실 C |

> Actual codes depend on your seeded DB. Query `GET /api/locations` to get the full list of active location codes.

Bed location codes are 5-digit strings: `str(floor) + str(floor*100 + room_num) + str(bed)`.

| Description | location_code |
|---|---|
| 1층 101호 1번 침상 | `11011` |
| 2층 304호 5번 침상 | `23045` |
| 4층 408호 6번 침상 | `44086` |

Source: `backend/app/utils/location_utils.py → build_bed_code()`

---

### `task_id` round-trip rule

The `task_id` in `robot/task_complete` must exactly match the integer received in `server/task_assign`. Mismatched IDs cause the task to remain PENDING indefinitely.

---

### `RobotState` valid values

Source: `backend/app/constants/enums.py`

| Value | Notes |
|---|---|
| `IDLE` | Triggers task dispatch |
| `MOVING` | Navigating |
| `ARRIVED` | At destination, waiting for NFC scan |
| `WAIT_NFC` | Waiting for NFC authentication |
| `AUTH_SUCCESS` | NFC auth passed |
| `AUTH_FAIL` | NFC auth failed |
| `DELIVERY_OPEN_NUR` | Compartment open — nurse interaction |
| `DELIVERY_OPEN_PAT` | Compartment open — patient interaction |
| `COMPLETE` | Task cycle complete |
| `LOW_BATTERY` | Battery low threshold crossed |
| `CHARGING_BATTERY` | Charging |
| `ERROR` | Device self-reported error |
| `EMERGENCY` | Externally triggered stop — distinct from ERROR |

**States that block task dispatch** (server will not send `server/task_assign` while robot is in any of these):

```
EMERGENCY, ERROR, LOW_BATTERY, CHARGING_BATTERY,
MOVING, ARRIVED, WAIT_NFC, AUTH_SUCCESS, AUTH_FAIL,
DELIVERY_OPEN_NUR, DELIVERY_OPEN_PAT
```

Only `IDLE` enables dispatch.

---

### `TaskType` valid values

Source: `backend/app/constants/enums.py`

| Value | Priority | Initiated by |
|---|---|---|
| `emergency_call` | 1 | Nurse |
| `battery_low` | 1 | System |
| `specimen_delivery` | 2 | Nurse |
| `kit_delivery` | 3 | Nurse |
| `logistics_delivery` | 4 | Nurse |
| `clothes_refill` | 5 | System/Nurse |
| `patient_clothes_rental` | 6 | Patient |
| `patient_clothes_return` | 6 | Patient |
| `used_clothes_collection` | 7 | System/Nurse |

---

### `AbnormalEventType` valid values

Source: `backend/app/services/abnormal_event_service.py`

| Value | Triggered by | Resolved by |
|---|---|---|
| `error` | `robot/error` MQTT message | Robot sends `state: "IDLE"` |
| `low_battery` | `robot/status` with `state: "LOW_BATTERY"` | Robot sends `state: "IDLE"` |
| `emergency_call` | Nurse calls `POST /tasks/nurse/emergency` | Nurse calls `POST /tasks/nurse/emergency/release` |

`emergency_call` events are **never** auto-resolved on IDLE — they require an explicit nurse release action.

---

## 5. TaskStatus Values

| Value | Meaning |
|---|---|
| `PENDING` | Created, waiting for dispatch |
| `DISPATCHED` | `server/task_assign` sent; robot not yet moving |
| `IN_PROGRESS` | Robot is executing the task |
| `COMPLETE` | Task finished successfully |
| `CANCELLED` | Cancelled by nurse or patient |
| `FAILED` | Task failed |

---

## 6. Emergency Call Contract (Finalized)

### 6.1 Flow Overview

```
Normal operation
      │
      │  Nurse presses Emergency button
      │  → POST /tasks/nurse/emergency
      ▼
Server publishes  server/emergency_call  { "command": "STOP" }
      │
      ▼
Robot stops immediately
Robot transitions to EMERGENCY state
Robot publishes  robot/status  { "state": "EMERGENCY" }
      │  (EMERGENCY is in BLOCKING_ROBOT_STATES — no dispatch while in this state)
      │
      │  Nurse presses Release button
      │  → POST /tasks/nurse/emergency/release
      ▼
Server publishes  server/emergency_call  { "command": "RELEASE" }
      │
      ▼
Robot transitions to IDLE
Robot publishes  robot/status  { "state": "IDLE" }
      │
      ▼
Server receives IDLE → resolves error/low_battery events → maybe_dispatch()
Normal dispatch resumes (requeued task is highest priority)
```

---

### 6.2 Server Actions on STOP (`POST /tasks/nurse/emergency`)

1. Creates an `emergency_call` task with `status = PENDING`, `priority = 1`.
2. Finds the currently active task (`status = DISPATCHED` or `IN_PROGRESS`) and **resets it to `PENDING`** — it is NOT cancelled; it will be re-dispatched after release.
   - `assigned_robot_id` → NULL
   - `started_at` → NULL
3. Opens an `emergency_call` AbnormalEvent.
4. Broadcasts `task_status_update` (requeued task) and `abnormal_event_update` via WebSocket.
5. Publishes `server/emergency_call { "robot_id": "AMR-001", "command": "STOP" }`.

---

### 6.3 Server Actions on RELEASE (`POST /tasks/nurse/emergency/release`)

1. Publishes `server/emergency_call { "robot_id": "AMR-001", "command": "RELEASE" }`.
2. Cancels all remaining `emergency_call` tasks with `status = PENDING`.
3. Resolves the open `emergency_call` AbnormalEvent.
4. Broadcasts `task_status_update` for each cancelled task and `abnormal_event_update` via WebSocket.

After release, dispatch does **not** happen immediately. It is triggered naturally when the robot transitions to `IDLE` and publishes `robot/status { state: "IDLE" }`.

---

### 6.4 Task Handling Summary

| Task status at time of STOP | Action |
|---|---|
| `DISPATCHED` or `IN_PROGRESS` | Reset to `PENDING` — will be re-dispatched after release |
| `PENDING` (non-emergency tasks) | Left untouched — remains in queue |
| `PENDING` (the new emergency_call task) | Created on STOP; cancelled on RELEASE |

---

### 6.5 Robot Requirements for Emergency

- On `command: "STOP"`: halt all movement immediately, transition to `EMERGENCY` state, publish `robot/status { state: "EMERGENCY" }`.
- On `command: "RELEASE"`: resume from stopped position, transition to `IDLE`, publish `robot/status { state: "IDLE" }`.
- `EMERGENCY` state is a **server-commanded stop** — distinct from `ERROR` (self-reported fault). The robot should not conflate them.

---

## 7. Implementation Status

| Feature | Status |
|---|---|
| Server subscribes to all 5 robot→server topics | ✅ Complete |
| `robot/status` handler → DB update + WS broadcast | ✅ Complete |
| `robot/location` handler → DB update + WS broadcast | ✅ Complete |
| `robot/battery` handler → DB update + WS broadcast | ✅ Complete |
| `robot/error` handler → DB update + abnormal event + WS broadcast | ✅ Complete |
| `robot/task_complete` handler → task COMPLETE + WS broadcast | ✅ Complete |
| `server/task_assign` published on robot IDLE | ✅ Complete |
| Auto-dispatch: highest-priority PENDING task on robot IDLE | ✅ Complete |
| Auto-task creation on inventory threshold crossing | ✅ Complete |
| `server/emergency_call` STOP on nurse emergency trigger | ✅ Complete |
| `server/emergency_call` RELEASE on nurse emergency release | ✅ Complete |
| Interrupted task requeued to PENDING on STOP | ✅ Complete |
| `server/task_cancel` publish when nurse cancels a task | ⚠️ Topic defined, not yet published |

---

## 8. Test Messages (Copy-Paste Ready)

Use these with `mosquitto_pub` or any MQTT client.

```bash
# robot goes IDLE — triggers task dispatch if PENDING tasks exist
mosquitto_pub -h localhost -p 1883 -t "robot/status" -m '{"robot_id":"AMR-001","state":"IDLE","timestamp":"2026-05-31T09:00:00+00:00"}'

# robot moving
mosquitto_pub -h localhost -p 1883 -t "robot/status" -m '{"robot_id":"AMR-001","state":"MOVING","timestamp":"2026-05-31T09:01:00+00:00"}'

# robot arrived at nursing station
mosquitto_pub -h localhost -p 1883 -t "robot/location" -m '{"robot_id":"AMR-001","location_code":"STATION-01","timestamp":"2026-05-31T09:01:30+00:00"}'

# robot arrived at bed 1층-101호-1번
mosquitto_pub -h localhost -p 1883 -t "robot/location" -m '{"robot_id":"AMR-001","location_code":"11011","timestamp":"2026-05-31T09:04:00+00:00"}'

# battery update
mosquitto_pub -h localhost -p 1883 -t "robot/battery" -m '{"robot_id":"AMR-001","battery_percent":68.0,"timestamp":"2026-05-31T09:05:00+00:00"}'

# robot self-reported error
mosquitto_pub -h localhost -p 1883 -t "robot/error" -m '{"robot_id":"AMR-001","error_message":"Motor controller fault on right wheel","timestamp":"2026-05-31T09:06:00+00:00"}'

# task complete (replace task_id with actual value from server/task_assign)
mosquitto_pub -h localhost -p 1883 -t "robot/task_complete" -m '{"robot_id":"AMR-001","task_id":42,"timestamp":"2026-05-31T09:10:00+00:00"}'

# robot enters EMERGENCY state (in response to server/emergency_call STOP)
mosquitto_pub -h localhost -p 1883 -t "robot/status" -m '{"robot_id":"AMR-001","state":"EMERGENCY","timestamp":"2026-05-31T09:11:00+00:00"}'
```

Monitor server→robot messages:

```bash
mosquitto_sub -h localhost -p 1883 -t "server/#" -v
```

---

## 9. Key Source Files

| File | What to read |
|---|---|
| `backend/app/constants/mqtt_topics.py` | All topic strings |
| `backend/app/mqtt/handlers.py` | Inbound payload handling and DB side-effects |
| `backend/app/schemas/robot.py` | Pydantic schemas — authoritative field names and types |
| `backend/app/scheduler/dispatcher.py` | `server/task_assign` payload construction |
| `backend/app/services/robot_service.py` | `robot_id` → DB row mapping; `get_or_create_robot` |
| `backend/app/constants/enums.py` | All valid `RobotState`, `TaskType`, `TaskStatus` values |
| `backend/app/routers/tasks.py` | Emergency STOP/RELEASE endpoint implementations |
| `backend/app/utils/location_utils.py` | `build_bed_code()` — bed location_code generation |
