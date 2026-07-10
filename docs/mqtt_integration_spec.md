# Hospital Logistics AMR — MQTT Integration Specification

**Audience:** Backend/server team, robot firmware / embedded team, server_bridge team  
**Scope:** MQTT contract between FastAPI server and AMR robot  
**Last updated:** 2026-06-01  
**Source:** Verified against backend source code

---

## 0. Revision Summary (v2 → v3 → v4)

1. `robot/location` removed from required contract — server infers location from `robot/task_complete` destination.
2. `robot/battery` is now the server-side trigger for low-battery handling (`battery_percent <= 20`).
3. `robot/status` is state-change notification, not periodic heartbeat.
4. `CHARGING_BATTERY` is the final enum spelling (`CHAGING_BATTERY` accepted as legacy only).
5. `server/task_assign` payload now includes `robot_id`.
6. `destination` is required (non-null) for all dispatched robot tasks.
7. `robot/status.timestamp` is optional; server uses receive time when omitted.
8. `robot/error.error_message` is the standard field; `error` accepted as fallback.
9. `server/task_finish` added for patient clothing task completion.
10. `server/task_cancel` publish now implemented in nurse/patient cancel flows.
11. Emergency STOP requeues interrupted task to PENDING (not CANCELLED).
12. `robot/status { state: "COMPLETE" }` does not finalize task DB status.
13. `server/task_assign` now includes `order_top`/`order_bottom` for `patient_clothes_rental` and `patient_clothes_return` tasks so the robot knows which compartment(s) to open.

---

## 1. System Overview

```
Robot (AMR-001)
    │
    │  MQTT  (broker: configurable, default port 1883)
    ▼
Server (FastAPI + MSSQL)
    │
    │  WebSocket  (ws://host/ws)
    ▼
Web Dashboard / Patient UI (React)
```

---

## 2. MQTT Topic List

### 2.1 Robot → Server

| Topic | Purpose | Required |
|---|---|---|
| `robot/status` | Robot state-change report | Yes |
| `robot/battery` | Battery percentage update; low-battery trigger | Required for low-battery auto-return |
| `robot/error` | Robot self-reported error | Yes |
| `robot/task_complete` | Task completion (non-patient tasks only) | Yes |
| `robot/location` | ~~Deprecated~~ | No longer required |

### 2.2 Server → Robot

| Topic | Purpose | Status |
|---|---|---|
| `server/task_assign` | Dispatch task to robot | ✅ Implemented |
| `server/task_cancel` | Cancel active task held by robot | ✅ Implemented |
| `server/task_finish` | Signal robot to clear patient clothing task | ✅ Implemented |
| `server/emergency_call` | Emergency STOP / RELEASE | ✅ Implemented |

---

## 3. Common Payload Rules

1. Payloads are JSON-encoded UTF-8 strings.
2. `robot_id` must be `"AMR-001"` (single-robot system).
3. `task_id` is an integer matching `tasks.id` in the server DB.
4. `location_code` strings must match `locations.location_code` in the server DB exactly.
5. `timestamp` is optional. When omitted, the server uses its receive time.
6. All enum/string values are case-sensitive.

---

## 4. RobotState Values

Valid values for `robot/status.state`:

| Value | Meaning |
|---|---|
| `IDLE` | Robot idle — triggers task dispatch |
| `MOVING` | Navigating |
| `ARRIVED` | Arrived at destination |
| `WAIT_NFC` | Waiting for NFC/user interaction |
| `AUTH_SUCCESS` | NFC authentication succeeded |
| `AUTH_FAIL` | NFC authentication failed |
| `DELIVERY_OPEN_NUR` | Nurse-side compartment open |
| `DELIVERY_OPEN_PAT` | Patient-side compartment open |
| `COMPLETE` | Task cycle complete (state notification only — does not finalize DB) |
| `LOW_BATTERY` | Low-battery state |
| `CHARGING_BATTERY` | Charging |
| `ERROR` | Self-reported device fault |
| `EMERGENCY` | Server-commanded emergency stop |

**Deprecated:** `CHAGING_BATTERY` — normalized to `CHARGING_BATTERY` by the server for backward compatibility. Remove from new robot code.

**Dispatch-blocking states** (server will not dispatch while robot is in any of these):
```
EMERGENCY, ERROR, LOW_BATTERY, CHARGING_BATTERY,
MOVING, ARRIVED, WAIT_NFC, AUTH_SUCCESS, AUTH_FAIL,
DELIVERY_OPEN_NUR, DELIVERY_OPEN_PAT
```

Only `IDLE` enables normal task dispatch.

**Low-battery exception:** While `LOW_BATTERY` blocks normal task dispatch, the server may dispatch exactly one system station-return task (`BATTERY_LOW` type, destination `STATION-01`) while the robot is in `LOW_BATTERY`. No other task may be dispatched until the condition is resolved.

---

## 5. TaskType Values

| Value | Priority | Initiated by | Notes |
|---|---|---|---|
| `emergency_call` | 1 | Nurse/admin | Not dispatched via `server/task_assign`; uses `server/emergency_call` |
| `battery_low` | 1 | System | Internal station-return task; dispatched when battery ≤ 20% |
| `specimen_delivery` | 2 | Nurse | Normal robot task |
| `kit_delivery` | 3 | Nurse | Normal robot task |
| `logistics_delivery` | 4 | Nurse | Normal robot task |
| `clothes_refill` | 5 | System/Nurse | Normal robot task |
| `patient_clothes_rental` | 6 | Patient | Robot must NOT publish `robot/task_complete` |
| `patient_clothes_return` | 6 | Patient | Robot must NOT publish `robot/task_complete` |
| `used_clothes_collection` | 7 | System/Nurse | Normal robot task |

---

## 6. TaskStatus Values

| Value | Meaning |
|---|---|
| `PENDING` | Created, waiting for dispatch |
| `DISPATCHED` | `server/task_assign` sent; robot executing |
| `IN_PROGRESS` | Robot actively executing |
| `COMPLETE` | Task completed successfully |
| `CANCELLED` | Cancelled by nurse/patient |
| `FAILED` | Failed during execution |

---

## 7. Payload Specifications

### 7.1 `robot/status` — Robot → Server

**Server action:** Update `robots.current_state`, broadcast `robot_state_update`.  
On `IDLE`: resolve open `error`/`low_battery` events, trigger `maybe_dispatch()`.

| Field | Type | Required | Notes |
|---|---|---|---|
| `robot_id` | string | Yes | Must be `"AMR-001"` |
| `state` | string | Yes | Must be a final RobotState value |
| `task_id` | integer | No | Include when a task is active or relevant |
| `timestamp` | string ISO 8601 | No | Server uses receive time if omitted |

```json
{ "robot_id": "AMR-001", "state": "MOVING", "task_id": 42 }
```

Minimum:
```json
{ "robot_id": "AMR-001", "state": "IDLE" }
```

> `robot/status { state: "COMPLETE" }` is a state notification only. Task DB finalization happens from `robot/task_complete` (normal tasks) or the patient web endpoint (patient tasks).

---

### 7.2 `robot/location` — Deprecated / Non-Required

**Status:** No longer required. The server now infers robot location from `robot/task_complete` destination.  
The handler is retained for backward compatibility only. Remove from new robot/server_bridge code.

---

### 7.3 `robot/battery` — Robot → Server

**Server action:** Update `robots.battery_percent`, broadcast `robot_battery_update`.  
If `battery_percent <= 20`: trigger low-battery handling (see §9).

| Field | Type | Required | Notes |
|---|---|---|---|
| `robot_id` | string | Yes | Must be `"AMR-001"` |
| `battery_percent` | float | Yes | Range `0.0`–`100.0` |
| `timestamp` | string ISO 8601 | No | Server uses receive time if omitted |

```json
{ "robot_id": "AMR-001", "battery_percent": 73.5 }
```

---

### 7.4 `robot/error` — Robot → Server

**Server action:** Set robot state to `ERROR`, open `error` AbnormalEvent, broadcast `robot_state_update` and `abnormal_event_update`.

| Field | Type | Required | Notes |
|---|---|---|---|
| `robot_id` | string | Yes | Must be `"AMR-001"` |
| `error_message` | string | Yes | Final standard field |
| `error` | string | No | Fallback for older clients; used if `error_message` absent |
| `task_id` | integer | No | Include if error is tied to an active task |
| `timestamp` | string ISO 8601 | No | Server uses receive time if omitted |

```json
{
  "robot_id": "AMR-001",
  "error_message": "Navigation failed in state: MOVING_TO_DESTINATION",
  "task_id": 42
}
```

To clear error state: publish `robot/status { "state": "IDLE" }` — server auto-resolves open `error` events.

---

### 7.5 `robot/task_complete` — Robot → Server

**Server action:**
1. Set `tasks.status = COMPLETE`, record `completed_at`.
2. Update `robots.current_location_id` from the completed task's `destination_location_id`.
3. Broadcast `task_status_update` and `robot_location_update` (if location changed).

| Field | Type | Required | Notes |
|---|---|---|---|
| `robot_id` | string | Yes | Must be `"AMR-001"` |
| `task_id` | integer | Yes | Must match `server/task_assign.task_id` |
| `timestamp` | string ISO 8601 | No | Server uses receive time if omitted |

```json
{ "robot_id": "AMR-001", "task_id": 42 }
```

> **Patient task rule:** Do NOT publish `robot/task_complete` for `patient_clothes_rental` or `patient_clothes_return`. Patient task completion is handled by the patient web UI → `server/task_finish`.

---

### 7.6 `server/task_assign` — Server → Robot

**Published when:** Robot transitions to `IDLE` and a `PENDING` task with a valid destination exists.

| Field | Type | Required | Notes |
|---|---|---|---|
| `robot_id` | string | Yes | `"AMR-001"` — for server_bridge filtering |
| `task_id` | integer | Yes | Round-trip back in `robot/task_complete` |
| `task_type` | string | Yes | Must be a valid `TaskType` |
| `origin` | string or null | No | `null` = skip origin, go directly to destination |
| `destination` | string | Yes | Must not be null; exact `locations.location_code` |
| `priority` | integer | Yes | Lower = higher priority |
| `order_top` | integer or null | Only for patient clothing tasks | `1` = top selected, `null` = not selected. Present only when `task_type` is `patient_clothes_rental` or `patient_clothes_return`. |
| `order_bottom` | integer or null | Only for patient clothing tasks | `1` = bottom selected, `null` = not selected. Present only when `task_type` is `patient_clothes_rental` or `patient_clothes_return`. |

**Standard task example:**
```json
{
  "robot_id": "AMR-001",
  "task_id": 42,
  "task_type": "kit_delivery",
  "origin": "STATION-01",
  "destination": "11011",
  "priority": 3
}
```

**Patient clothing task example (top + bottom selected):**
```json
{
  "robot_id": "AMR-001",
  "task_id": 45,
  "task_type": "patient_clothes_rental",
  "origin": null,
  "destination": "11011",
  "priority": 6,
  "order_top": 1,
  "order_bottom": 1
}
```

**Patient clothing task example (top only selected):**
```json
{
  "robot_id": "AMR-001",
  "task_id": 46,
  "task_type": "patient_clothes_return",
  "origin": null,
  "destination": "11011",
  "priority": 6,
  "order_top": 1,
  "order_bottom": null
}
```

> `order_top` and `order_bottom` are binary flags (`1` = selected, `null` = not selected), not quantities. The robot should open the corresponding compartment(s) based on these flags.

---

### 7.7 `server/task_cancel` — Server → Robot

Published when a nurse or patient cancels a task that is DISPATCHED or IN_PROGRESS. Also published during low-battery handling when requeuing an active task.

| Field | Type | Required | Notes |
|---|---|---|---|
| `robot_id` | string | Yes | Must be `"AMR-001"` |
| `task_id` | integer | Yes | Task to cancel |

```json
{ "robot_id": "AMR-001", "task_id": 42 }
```

**Robot behavior:**
1. If `task_id` matches active task: cancel navigation goal, clear active task.
2. Publish `robot/status { "state": "IDLE" }`.

---

### 7.8 `server/task_finish` — Server → Robot

Published after patient presses the web completion button for a patient clothing task.

| Field | Type | Required | Notes |
|---|---|---|---|
| `robot_id` | string | Yes | Must be `"AMR-001"` |
| `task_id` | integer | Yes | Patient clothing task that was completed |

```json
{ "robot_id": "AMR-001", "task_id": 43 }
```

**Robot behavior:**
1. Process only if active task exists and `task_id` matches.
2. Clear active task and patient interaction state.
3. Publish `robot/status { "state": "IDLE" }`.

---

### 7.9 `server/emergency_call` — Server → Robot

STOP and RELEASE use the same topic, differentiated by `command`.

**STOP:**
```json
{ "robot_id": "AMR-001", "command": "STOP" }
```

**RELEASE:**
```json
{ "robot_id": "AMR-001", "command": "RELEASE" }
```

| command | Robot action |
|---|---|
| `STOP` | Cancel nav goal, issue immediate velocity/motor stop (zero `cmd_vel`), clear active task, publish `robot/status { state: "EMERGENCY" }` |
| `RELEASE` | Exit EMERGENCY, publish `robot/status { state: "IDLE" }` |

---

## 8. Low-Battery Handling Contract

**Trigger:** Server receives `robot/battery` with `battery_percent <= 20`.

**Server actions (idempotent):**
1. Update `robots.battery_percent`.
2. Broadcast `robot_battery_update`.
3. If robot state is not already `LOW_BATTERY` or `CHARGING_BATTERY`: set `robots.current_state = LOW_BATTERY`, broadcast `robot_state_update`.
4. Open (or return existing) `low_battery` AbnormalEvent, broadcast `abnormal_event_update`.
5. If robot has an active `DISPATCHED`/`IN_PROGRESS` task: publish `server/task_cancel` for it, reset task to `PENDING`.
6. If no existing `battery_low` task in `PENDING`/`DISPATCHED`/`IN_PROGRESS`: create a `battery_low` task with `destination = STATION-01`, mark `DISPATCHED`, publish `server/task_assign`.

**Idempotency guarantees:**
- No duplicate `low_battery` AbnormalEvents.
- No duplicate station-return tasks while one is already pending/active.
- `CHARGING_BATTERY` state is not overridden.

**Resolution:** When robot publishes `robot/status { state: "IDLE" }`, server resolves all open `low_battery` events and triggers normal dispatch.

---

## 9. Emergency Call Contract

### 9.1 STOP flow

1. Nurse presses E-Stop → `POST /tasks/nurse/emergency`.
2. Server creates `emergency_call` task (priority 1), opens `emergency_call` AbnormalEvent.
3. Server requeues any `DISPATCHED`/`IN_PROGRESS` task back to `PENDING`.
4. Server publishes `server/emergency_call { "command": "STOP" }`.
5. Robot: cancel nav goal, issue immediate velocity/motor stop, clear active task, publish `robot/status { "state": "EMERGENCY" }`.

### 9.2 Server task handling on STOP

| Task status at STOP | Action |
|---|---|
| `PENDING` | Unchanged |
| `DISPATCHED` | Reset to `PENDING` |
| `IN_PROGRESS` | Reset to `PENDING` |
| `COMPLETE` / `CANCELLED` / `FAILED` | Unchanged |

### 9.3 RELEASE flow

1. Nurse presses Release → `POST /tasks/nurse/emergency/release`.
2. Server publishes `server/emergency_call { "command": "RELEASE" }`.
3. Server cancels remaining `PENDING` `emergency_call` tasks, resolves `emergency_call` AbnormalEvent.
4. Robot exits EMERGENCY, publishes `robot/status { "state": "IDLE" }`.
5. Server receives `IDLE` → triggers `maybe_dispatch()`.

---

## 10. Patient Clothing Task Flow

For `patient_clothes_rental` and `patient_clothes_return`:

1. Robot receives `server/task_assign` — includes `order_top` and `order_bottom` flags.
2. Robot checks `order_top`/`order_bottom` to determine which compartment(s) to prepare.
3. Robot navigates to destination (patient's bed).
4. Robot opens patient compartment(s), publishes `robot/status { "state": "DELIVERY_OPEN_PAT" }`.
5. Patient presses completion button → `POST /tasks/{task_id}/complete`.
6. Server sets `tasks.status = COMPLETE`, broadcasts `task_status_update`.
7. Server publishes `server/task_finish { "robot_id": "AMR-001", "task_id": <id> }`.
8. Robot clears active task, publishes `robot/status { "state": "IDLE" }`.

**Robot must NOT publish `robot/task_complete` for patient clothing tasks.**

**Compartment flag semantics:**
- `order_top: 1` → top clothing compartment required
- `order_top: null` → top compartment not required (omit/skip)
- `order_bottom: 1` → bottom clothing compartment required
- `order_bottom: null` → bottom compartment not required (omit/skip)
- At least one of `order_top` or `order_bottom` will be `1`; the server does not validate this constraint but the patient UI enforces it.

---

## 11. AbnormalEvent Types

| Type | Trigger | Resolution |
|---|---|---|
| `error` | `robot/error` MQTT message | Robot sends `robot/status { state: "IDLE" }` |
| `low_battery` | `robot/battery` with `battery_percent <= 20` | Robot sends `robot/status { state: "IDLE" }` |
| `emergency_call` | Nurse presses E-Stop | Nurse presses Release → `POST /tasks/nurse/emergency/release` |

`emergency_call` is never auto-resolved on IDLE — explicit release is required.

---

## 12. location_code Contract

Source of truth: `locations.location_code` column in the server DB.

**Non-bed location codes:**

| location_code | Location |
|---|---|
| `STATION-01` | Nursing station |
| `LAUNDRY-01` | Laundry room |
| `WAREHOUSE-01` | Warehouse |
| `SPECIMEN-LAB` | Specimen laboratory |
| `EXAM-A` | Examination room A |
| `EXAM-B` | Examination room B |
| `EXAM-C` | Examination room C |

**Bed location code format:** `str(floor) + str(floor*100 + room) + str(bed)`  
Examples: `11011` (1층 101호 1번), `33045` (3층 304호 5번), `44086` (4층 408호 6번)

> Do not use robot-local aliases. Robot `locations.yaml` keys must exactly match server DB `location_code` values.

---

## 13. Implementation Status

| Item | Status |
|---|---|
| `CHARGING_BATTERY` enum spelling | ✅ Updated backend/frontend/docs |
| Legacy `CHAGING_BATTERY` normalizer | ✅ In `_handle_robot_status` |
| `server/task_assign.robot_id` | ✅ Added to dispatcher payload |
| `destination` non-null enforcement | ✅ Dispatcher skips tasks with null destination |
| `robot/status.timestamp` optional | ✅ Schema updated |
| `robot/error.error_message` + fallback `error` | ✅ Schema updated with model_validator |
| `server/emergency_call` STOP/RELEASE | ✅ Implemented |
| Emergency active task requeue | ✅ DISPATCHED/IN_PROGRESS → PENDING |
| `server/task_cancel` publish | ✅ Nurse cancel, patient cancel, low-battery requeue |
| `server/task_finish` publish | ✅ Patient web completion endpoint |
| `robot/location` removal | ✅ Marked deprecated; handler retained for compat |
| `robot/battery` low-battery trigger | ✅ `battery_percent <= 20` handler |
| Low-battery idempotency | ✅ Duplicate event/task prevention |
| Low-battery active task requeue | ✅ Requeue + `server/task_cancel` |
| Low-battery station-return dispatch | ✅ `BATTERY_LOW` task → `STATION-01` |
| `robot/task_complete` → robot location inference | ✅ Updates `robots.current_location_id` from task destination |
| `robot/status COMPLETE` not finalizing task | ✅ No task finalization in status handler |
| Patient task: `server/task_finish` on completion | ✅ Published after `POST /tasks/{id}/complete` |
| `server/task_assign` clothing flags for patient tasks | ✅ `order_top`/`order_bottom` included for `patient_clothes_rental` and `patient_clothes_return` |

---

## 14. Copy-Paste Test Messages

```bash
# Robot IDLE — triggers task dispatch
mosquitto_pub -h localhost -p 1883 -t "robot/status" -m '{"robot_id":"AMR-001","state":"IDLE"}'

# Robot state updates
mosquitto_pub -h localhost -p 1883 -t "robot/status" -m '{"robot_id":"AMR-001","state":"MOVING","task_id":42}'
mosquitto_pub -h localhost -p 1883 -t "robot/status" -m '{"robot_id":"AMR-001","state":"DELIVERY_OPEN_PAT","task_id":42}'
mosquitto_pub -h localhost -p 1883 -t "robot/status" -m '{"robot_id":"AMR-001","state":"CHARGING_BATTERY"}'
mosquitto_pub -h localhost -p 1883 -t "robot/status" -m '{"robot_id":"AMR-001","state":"EMERGENCY"}'

# Battery — normal
mosquitto_pub -h localhost -p 1883 -t "robot/battery" -m '{"robot_id":"AMR-001","battery_percent":73.5}'

# Battery — triggers LOW_BATTERY handling and station-return dispatch
mosquitto_pub -h localhost -p 1883 -t "robot/battery" -m '{"robot_id":"AMR-001","battery_percent":20.0}'

# Robot self-reported error
mosquitto_pub -h localhost -p 1883 -t "robot/error" -m '{"robot_id":"AMR-001","error_message":"Navigation failed","task_id":42}'

# Task complete (non-patient task)
mosquitto_pub -h localhost -p 1883 -t "robot/task_complete" -m '{"robot_id":"AMR-001","task_id":42}'
```

Monitor server→robot messages:
```bash
mosquitto_sub -h localhost -p 1883 -t "server/#" -v
```

Expected server→robot messages:
```json
{"robot_id":"AMR-001","task_id":42,"task_type":"kit_delivery","origin":"STATION-01","destination":"11011","priority":3}
{"robot_id":"AMR-001","task_id":42}
{"robot_id":"AMR-001","task_id":43}
{"robot_id":"AMR-001","command":"STOP"}
{"robot_id":"AMR-001","command":"RELEASE"}
```

---

## 15. Key Source Files

| File | What to verify |
|---|---|
| `backend/app/constants/enums.py` | `RobotState`, `TaskType`, `TaskStatus`, blocking states |
| `backend/app/constants/mqtt_topics.py` | All topic constants; `robot/location` deprecated |
| `backend/app/mqtt/handlers.py` | All inbound handlers; `_handle_low_battery`; location inference in `_handle_task_complete` |
| `backend/app/schemas/robot.py` | Optional timestamps; `task_id`; `error` fallback |
| `backend/app/scheduler/dispatcher.py` | `robot_id` in payload; null destination guard |
| `backend/app/routers/tasks.py` | Nurse cancel; patient complete → `server/task_finish`; cancel → `server/task_cancel` |
| `backend/app/services/task_service.py` | `requeue_task` |
| `backend/app/services/abnormal_event_service.py` | Idempotent `open_event`; `resolve_all_by_type` |
| `frontend/src/types/index.ts` | `RobotState` union: `CHARGING_BATTERY`, `EMERGENCY` |
