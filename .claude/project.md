# Vision AI Hospital Logistics AMR – Claude Code Project Guide

## 0. Purpose
This document defines the required architecture, domain rules, UI behavior, backend behavior, database scope, and implementation order for this project.
Claude Code must follow this document as the source of truth when generating code, folder structure, schema, APIs, MQTT topics, WebSocket events, and React UI behavior.
Do not invent a different domain model unless explicitly requested.
Do not simplify away required hospital logic.
Do not replace the specified stack with another stack unless explicitly requested.

---

## 1. Project Summary
This project is a hospital logistics AMR system.
The system consists of:
- an autonomous robot running on Ubuntu/ROS2
- a backend server
- a web dashboard for nurses/admin
- a web interface for patients
- real-time communication between robot, server, and web clients

The robot is responsible for moving between hospital locations and performing logistics-related delivery workflows.

The server is responsible for:
- receiving robot state
- managing task priority
- dispatching tasks
- storing records
- sending real-time updates to web clients
- controlling tablet/web screen rendering based on robot state

The web system is responsible for:
- nurse login and order creation
- patient login and clothing rental/return request
- dashboard monitoring
- completed task history
- inventory visibility
- robot current status visibility

---

## 2. Required Tech Stack
Claude Code must use the following stack unless explicitly told otherwise.

### Backend
- FastAPI
- SQLAlchemy
- Pydantic
- MQTT client for Mosquitto
- WebSocket support in FastAPI

### Frontend
- React
- TypeScript preferred
- simple and clean component structure
- real-time UI updates using WebSocket

### Database
- MSSQL

### Robot communication
- MQTT (Mosquitto broker)

### Deployment / Runtime assumptions
- Ubuntu-based environment
- robot side runs separately from backend/web
- backend and frontend can be developed independently but must follow the same domain model

---

## 3. High-Level System Architecture

System flow:
ROBOT (ROS2 / Ubuntu)
→ MQTT
SERVER (FastAPI)
→ WebSocket + REST API
WEB UI (React)

### Communication roles

#### Robot → Server
The robot sends:
- current robot state
- battery level
- current location
- task completion
- error status

#### Server → Robot
The server sends:
- assigned task
- emergency instruction if needed
- task cancel if needed

#### Server → Web
The server sends real-time updates:
- robot state update
- robot location update
- battery update
- task update
- inventory update

---

## 4. User Roles

This system has at least the following user roles.

### 4.1 Nurse
Nurse can:
- log in
- view dashboard
- create orders
- monitor progress
- see completed tasks
- see robot current state
- perform NFC-based workflow when robot arrives
- use emergency station call if implemented in dashboard

### 4.2 Patient
Patient can:
- log in
- request clothing rental
- request clothing return
- complete patient-side workflow when robot arrives

### 4.3 Admin / Monitoring Role
This can be merged with nurse dashboard if desired.
At minimum, dashboard must expose robot operational information and task progress.

---

## 5. Authentication Rules

### 5.1 Patient login
Patient login format is fixed:
- ID: `hospital`
- PW: patient's own bed number

Examples:
- ID: `hospital`
- PW: `11011`

### 5.2 Nurse login
Nurse login is fixed:
- ID: `hospital`
- PW: `worker`

Claude Code must implement these rules exactly unless explicitly asked to change them.

---

## 6. Hospital Location Model

## 6.1 Physical hospital structure
The hospital has:
- Floors: 1 to 4
- Rooms per floor: 8
- Beds per room: 6

Total:
- 32 rooms
- 192 beds

## 6.2 Bed code format
Each bed has a 5-digit code.
Format:
- floor + room + bed

Examples:
- 1st floor, room 101, bed 1 → `11011`
- 2nd floor, room 207, bed 4 → `22074`

Claude Code must preserve this format.

## 6.3 Non-bed locations
Additional hospital locations include:
- station
- laundry
- warehouse
- specimen_lab
- exam_A
- exam_B
- exam_C

These locations must be supported in DB, API, MQTT payloads, and UI.

## 6.4 Recommended location representation
Use a normalized location model.

Suggested fields:
- location_id
- location_type
- floor
- room
- bed
- display_name
- is_active

Where:
- bed locations can store floor/room/bed values
- non-bed locations can store null for floor/room/bed as needed

Example display values:
- `1F / 101 / Bed 1`
- `2F / 207 / Bed 4`
- `Station`
- `Laundry`
- `Warehouse`
- `Specimen Lab`

---

## 7. Robot UI / Tablet Rendering Logic

The tablet/web screen shown to the user is controlled by robot/server state.
The backend must treat robot state as the main source for rendering screen mode.
A separate `screen_mode` field may be introduced if necessary for strict UI control, but the default design should follow the robot state model below.

## 7.1 Official state values
Use the following state values exactly as the current project spec defines them:
- IDLE
- MOVING
- ARRIVED
- WAIT_NFC
- AUTH_SUCCESS
- AUTH_FAIL
- DELIVERY_OPEN_NUR
- DELIVERY_OPEN_PAT
- COMPLETE
- LOW_BATTERY
- CHAGING_BATTERY
- ERROR

Important:
The spec currently uses `CHAGING_BATTERY` (spelling as provided).
Claude Code must keep this exact state name unless the project owner explicitly decides to rename it everywhere to `CHARGING_BATTERY`.
Do not silently change only one layer.

## 7.2 State → screen meaning

### IDLE
Default waiting screen.
Show a friendly idle screen.

### MOVING
Show: "이동 중..."

### ARRIVED
Show: "도착했습니다"

### WAIT_NFC
Show: "의료인님. 작업을 수행하려면 카드를 태그해주세요"

### AUTH_SUCCESS
Show: "인증되었습니다"

### AUTH_FAIL
Show: "인증 실패"

### DELIVERY_OPEN_NUR
Show: "락이 해제 되었습니다. 작업을 완료하시고 카드를 태그해주세요"

### DELIVERY_OPEN_PAT
Show:
- "의류 작업을 완료 후 완료 버튼을 눌러주세요"
- include patient-side 완료 button

### COMPLETE
Show: "처리 완료"

### LOW_BATTERY
Show: "충전이 필요합니다"
Behavior rule:
- when battery is 20% or lower, show this screen for 10 seconds every 5 minutes, then return to the previous/main screen flow

### CHAGING_BATTERY
Show: "충전중"

### ERROR
Show: "ERROR. 관리자에게 문의하세요"

---

## 8. Nurse Order Types
The nurse interface must support the following order categories:
- clothes_refill
- kit_delivery
- specimen_delivery
- logistics_delivery
- used_clothes_collection
- battery_low

Also account for emergency station call as part of abnormal / urgent handling.

Important:
The project owner stated that "device abnormal state" includes:
- battery low
- ERROR
- emergency station call from robot dashboard

Claude Code must reflect that in priority logic.

---

## 9. Task Priority Rules
The task system must use strict priority ordering.

Priority order:
1. device abnormal state
   includes:
   - battery low
   - ERROR
   - emergency station call
2. specimen delivery
3. kit delivery
4. logistics delivery
5. clothes refill
6. patient clothes delivery
   this refers to patient-initiated rental/return request
7. used clothes collection
   when collection bin reaches threshold, robot goes to laundry automatically

Claude Code must implement task scheduling using a priority-aware queue or equivalent scheduling logic.

---

## 10. Task Scheduler Requirements
The backend must include task scheduling behavior.

At minimum, implement these concepts:

### 10.1 Priority Queue
Tasks are stored and selected according to priority.

### 10.2 Task Dispatcher
When the robot is available for work, the server chooses the highest-priority valid task and sends it to the robot.

### 10.3 Robot State Check
The dispatcher must consider robot availability and safety-related robot state before assigning the next task.

Examples:
- do not assign normal task while robot is in ERROR
- do not assign normal task while battery issue is unresolved if project logic forbids it
- do not dispatch patient/nurse task during emergency abnormal handling

### 10.4 Automatic Trigger Cases
Support future or simple rule-based automation such as:
- used clothes collection triggered when collection count threshold is reached
- clothes refill triggered when clothing stock becomes low

This can start with simple backend logic and be expanded later.

---

## 11. Robot Location Display Rule
No graphical map is required.
Robot location should be displayed as the last arrived hospital location in text form.

Examples:
- `현재 위치: 2층 207호 4번 침상`
- `현재 위치: 세탁실`
- `현재 위치: 검체실`

This is not considered difficult and should be the default implementation.
Claude Code should implement location text formatting from location data.

---

## 12. Nurse Dashboard Requirements
The nurse dashboard must include the following information.

### 12.1 Top / summary area
Show:
- battery
- current time
- current date
- robot location

### 12.2 Inventory area
Display clothing inventory / refill-related information as defined by the project owner.

### 12.3 Task progress area
Display current ongoing tasks.

### 12.4 Completed task area
Display completed tasks.

### 12.5 Emergency action
If emergency station call button is included by project owner, include it in dashboard and connect it to urgent task flow.

Important:
Do not remove existing dashboard elements already defined by the project owner's sketch.

---

## 13. Order Creation UI Rules
The order creation screen must support both normal locations and bed selection.

### 13.1 Destination / origin dropdown logic
Dropdown includes:
- station
- laundry
- warehouse
- specimen_lab
- exam_A
- exam_B
- exam_C
- bed

### 13.2 Bed selected → 3-step selector
If the user selects `bed`, show:
1. floor selector
2. room selector
3. bed selector

Then automatically generate the 5-digit bed code.

Example flow:
- floor = 2
- room = 207
- bed = 4
- generated code = `22074`

This logic should be reused wherever bed location is needed.

---

## 14. Patient Interface Rules
Patient login uses:
- ID = `hospital`
- PW = own bed number

After login, show:
- 옷 대여(신청)
- 옷 반납(신청)

Important:
These actions are themselves robot call actions.
Do not add a separate "call robot" step unless explicitly requested later.

Expected flow:
1. patient logs in
2. patient chooses rental or return
3. backend creates appropriate task
4. robot comes to patient
5. state becomes ARRIVED
6. state becomes DELIVERY_OPEN_PAT
7. patient performs clothing action
8. patient presses 완료 button
9. workflow ends in COMPLETE

Claude Code must preserve this interaction logic.

---

## 15. MQTT Design Requirements
Broker: Mosquitto

Claude Code must implement MQTT integration in backend.
At minimum, define topic structure clearly and keep it centralized as constants/config.

Suggested topic groups:

### 15.1 Robot publishes to server
- `robot/status`
- `robot/location`
- `robot/battery`
- `robot/error`
- `robot/task_complete`

### 15.2 Server publishes to robot
- `server/task_assign`
- `server/task_cancel`
- `server/emergency_call`

Claude Code may refine payload structure, but must not drift from this system intent.

### 15.3 Example payload principles
Robot status payload should include at least:
- robot_id
- state
- timestamp

Robot location payload should include at least:
- robot_id
- location_id or resolved location code
- timestamp

Battery payload should include at least:
- robot_id
- battery_percent
- timestamp

Task assignment payload should include at least:
- task_id
- task_type
- origin
- destination
- priority

All payload schemas should be documented in code.

---

## 16. WebSocket Design Requirements
The backend must provide real-time updates to React clients via WebSocket.

Suggested WebSocket event categories:
- robot_state_update
- robot_location_update
- robot_battery_update
- task_status_update
- inventory_update

Claude Code should implement a simple and maintainable event broadcasting structure.
Do not over-engineer with unnecessary abstractions unless needed.

---

## 17. REST API Scope
FastAPI must expose REST APIs for at least the following categories.

### 17.1 Auth
- nurse login
- patient login

### 17.2 Locations
- fetch available location list
- fetch bed selection metadata if needed

### 17.3 Orders / Tasks
- create nurse order
- create patient clothing request
- fetch ongoing tasks
- fetch completed tasks
- fetch task detail
- trigger emergency call if applicable

### 17.4 Robot
- fetch robot current state
- fetch robot current location text
- fetch battery status

### 17.5 Inventory
- fetch inventory status
- update inventory if workflow requires it

Exact route names may be designed by Claude Code, but they must remain clean and domain-based.

---

## 18. Database Requirements
Database: MSSQL

Claude Code must design the schema first before implementing most business logic.

At minimum, create tables equivalent to the following domains.

### 18.1 users or auth model
Can be minimal because login rules are fixed.
May be hardcoded initially if requested by owner, but schema should still be clean if expanded later.

### 18.2 locations
Suggested fields:
- id
- location_code
- location_type
- floor
- room
- bed
- display_name
- created_at
- updated_at

### 18.3 robots
Suggested fields:
- id
- robot_code
- current_state
- current_location_id
- battery_percent
- last_seen_at
- created_at
- updated_at

### 18.4 tasks / orders
Suggested fields:
- id
- task_type
- origin_location_id
- destination_location_id
- patient_bed_code nullable
- requested_by_role
- priority
- status
- assigned_robot_id nullable
- created_at
- started_at nullable
- completed_at nullable
- note nullable

### 18.5 task_history
Optional separate history table if needed for auditing.

### 18.6 clothing_inventory
Suggested fields:
- id
- location_id
- clean_count
- used_count
- updated_at

### 18.7 emergency_events
Optional but recommended if emergency station call is tracked separately.

Claude Code should use SQLAlchemy models and migrations or a migration-ready structure.

---

## 19. Frontend Requirements
React UI should be divided into clear feature areas.

Suggested page groups:

### 19.1 Login pages
- nurse login
- patient login

### 19.2 Nurse dashboard
- robot summary
- inventory
- ongoing tasks
- completed tasks
- emergency action if needed

### 19.3 Order creation page
- dropdown for predefined locations
- bed selector flow
- order detail fields
- submission result feedback

### 19.4 Patient page
- clothes rental request
- clothes return request
- workflow progress screen
- 완료 button in DELIVERY_OPEN_PAT state

### 19.5 Shared state handling
- receive backend state through WebSocket
- render current robot-related screen appropriately

Keep the UI implementation practical and close to the project owner's sketch.
Do not redesign away from the draft without explicit instruction.

---

## 20. Coding Rules for Claude Code

Claude Code must follow these rules.

### 20.1 Implement in a design-first order
Do not start by generating random UI or server code without data model clarity.

### 20.2 Work in this order
1. define folder structure
2. define DB schema
3. define domain models / enums
4. define MQTT topic constants and payload schemas
5. define FastAPI routes and services
6. implement scheduler logic
7. implement WebSocket broadcasting
8. implement React pages/components
9. connect frontend to API/WebSocket
10. add seed/init data for locations

### 20.3 Keep constants centralized
Centralize:
- state enums
- order type enums
- priority rules
- location type enums
- MQTT topic constants
- WebSocket event names

### 20.4 Avoid hidden assumptions
If some detail is ambiguous, generate code in a way that is easy to modify later.
Do not hardcode business rules in scattered places.

### 20.5 Prefer maintainability
Use:
- routers
- services
- models
- schemas
- config/constants

Do not place all logic in one file.

---

## 21. Required Seed / Initial Data
Claude Code should prepare seed or initialization data for locations.

At minimum:
- all fixed non-bed locations
- all 192 bed locations

This should preferably be script-generated rather than manually typed one by one.
The generator must follow the official bed-code rule.

---

## 22. Important Domain Notes

### 22.1 Elevator control
Elevator control is not part of this implementation.
Do not add elevator integration logic.

### 22.2 Robot current location
Current location is displayed as text based on the last arrived location.
No map UI required.

### 22.3 Patient request meaning
Patient's clothing rental/return action is already the robot call action.
Do not split it into unnecessary extra steps.

### 22.4 Device abnormal state
This must include:
- battery low
- ERROR
- emergency station call

Do not forget this in scheduling or dashboard logic.

---

## 23. Recommended Initial Folder Structure
Claude Code may refine this, but should stay close to this idea.

```
backend/
  app/
    main.py
    config/
    constants/
    db/
    models/
    schemas/
    routers/
    services/
    mqtt/
    websocket/
    scheduler/
    utils/
  requirements.txt

frontend/
  src/
    api/
    components/
    pages/
    hooks/
    store/
    types/
    utils/

.claude/
  project.md
```

---

## 24. First Implementation Goal
Claude Code should first generate:
1. database schema and SQLAlchemy models
2. initial location seed generator
3. FastAPI app skeleton
4. MQTT integration skeleton
5. WebSocket broadcasting skeleton
6. React page skeletons for:
   - nurse login
   - patient login
   - nurse dashboard
   - order creation
   - patient request page

Only after that should full business logic be expanded.

---

## 25. Final Instruction
When generating code for this project, always preserve:
- the official state names
- the official login rules
- the official location format
- the official task priority order
- the owner's UI flow logic
- the chosen stack (FastAPI / React / MSSQL / MQTT / WebSocket)

If anything is unclear, choose the implementation that is easiest to maintain and easiest for the project owner to modify later.
