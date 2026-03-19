# Hospital Logistics AMR System

A full-stack web application for managing an Autonomous Mobile Robot (AMR) that handles clothing rental, laundry collection, specimen transport, and emergency logistics within a hospital ward.

Nurses dispatch tasks and monitor the robot in real time. Patients request clothing rental or return directly from a bedside tablet. All state is synchronized across clients via WebSocket.

---

## Tech Stack

### Backend
| Layer | Technology |
|---|---|
| Framework | FastAPI (Python 3.11+) |
| ORM | SQLAlchemy 2.0 |
| Database | Microsoft SQL Server (MSSQL via pyodbc) |
| Migrations | Alembic |
| Auth | JWT (python-jose) |
| Real-time | WebSocket (FastAPI native) |
| Robot comms | MQTT (paho-mqtt) — broker not included |
| Config | pydantic-settings / `.env` |

### Frontend
| Layer | Technology |
|---|---|
| Framework | React 18 + TypeScript |
| Routing | React Router v6 |
| State | Zustand |
| Build / Dev | Vite 6 |
| HTTP | Native `fetch` with a thin wrapper (`api/client.ts`) |
| Real-time | Native `WebSocket` |

---

## Folder Structure

```
hospital-delivery-system/
├── backend/
│   ├── app/
│   │   ├── config/         # Settings (pydantic-settings, reads .env)
│   │   ├── constants/      # Enums, MQTT topic strings, WS event names
│   │   ├── db/             # SQLAlchemy engine + session factory
│   │   ├── models/         # ORM models: Robot, Task, Location, Inventory, AbnormalEvent
│   │   ├── mqtt/           # MQTT client + message handlers
│   │   ├── routers/        # FastAPI routers: auth, tasks, robot, locations, inventory, abnormal_events
│   │   ├── scheduler/      # Task dispatcher (assigns PENDING tasks to the robot)
│   │   ├── schemas/        # Pydantic request/response schemas
│   │   ├── services/       # Business logic: auth, robot, task, inventory, abnormal_event
│   │   ├── utils/          # Location display text helper
│   │   └── websocket/      # WS connection manager + router
│   ├── migrations/         # Alembic migration files
│   ├── seeds/              # Seed script for location data
│   ├── .env.example        # Environment variable template
│   ├── alembic.ini
│   └── requirements.txt
│
└── frontend/
    └── src/
        ├── api/            # HTTP modules: auth, tasks, robot, locations, inventory, abnormalEvents
        ├── components/     # Shared UI: ProtectedRoute, RobotStatusBanner, TaskCard, InventoryTable, BedSelector
        ├── hooks/          # useWebSocket — connects and routes WS events to Zustand stores
        ├── pages/
        │   ├── LoginPage.tsx
        │   ├── nurse/      # NurseDashboard, RobotDashboard, OrderCreate
        │   └── patient/    # PatientRequestPage, PatientScreenController
        ├── store/          # Zustand stores: auth, robot, task, inventory, abnormalEvent
        ├── types/          # Shared TypeScript types (mirrors backend schemas)
        └── App.tsx         # Route definitions + global WS initialization
```

---

## Running the Backend

### Prerequisites
- Python 3.11+
- Microsoft SQL Server (local or Docker) with ODBC Driver 17
- An MQTT broker (e.g. Mosquitto) — **optional** for UI-only demo

### 1. Configure environment

```bash
cd backend
cp .env.example .env
# Edit .env — set DB_HOST, DB_PASSWORD, SECRET_KEY at minimum
```

Key variables:

| Variable | Default | Notes |
|---|---|---|
| `DB_HOST` | `localhost` | SQL Server host |
| `DB_PORT` | `1433` | SQL Server port |
| `DB_NAME` | `hospital_amr` | Database name |
| `DB_USER` | `sa` | SQL Server user |
| `DB_PASSWORD` | `YourPassword!` | SQL Server password |
| `MQTT_HOST` | `localhost` | MQTT broker host |
| `SECRET_KEY` | `change-me-in-production` | JWT signing key — **change this** |
| `ACCESS_TOKEN_EXPIRE_MINUTES` | `480` | 8-hour sessions |

### 2. Install dependencies

```bash
python -m venv .venv
source .venv/bin/activate      # Windows: .venv\Scripts\activate
pip install -r requirements.txt
```

### 3. Create tables and seed locations

```bash
alembic upgrade head
python -m seeds.seed_locations
```

### 4. Start the server

```bash
uvicorn app.main:app --reload --port 8000
```

The API is now available at `http://localhost:8000`.
Interactive docs: `http://localhost:8000/docs`

---

## Running the Frontend

### Prerequisites
- Node.js 18+

### 1. Install dependencies

```bash
cd frontend
npm install
```

### 2. Start the dev server

```bash
npm run dev
```

Vite starts on `http://localhost:3000`.
All `/api/*` requests are proxied to `http://localhost:8000`.
The WebSocket at `/ws` is proxied to `ws://localhost:8000`.

### 3. Build for production

```bash
npm run build   # outputs to frontend/dist/
```

In production, serve `dist/` from a web server (e.g. nginx) and configure it to forward `/api` and `/ws` to the FastAPI backend.

---

## Demo Accounts

| Role | URL | Username | Password |
|---|---|---|---|
| Nurse | `/login/nurse` | `hospital` | `worker` |
| Patient | `/login/patient` | `hospital` | 5-digit bed code (e.g. `11011`) |

Bed codes follow the format `FRRB` where F = floor (1–4), RR = room (01–08 relative to floor), B = bed (1–6).
Examples: `11011` = floor 1, room 101, bed 1 · `20312` = floor 2, room 203, bed 2.

---

## Key User Flows

### Nurse

**Login → Dashboard**
Log in at `/login/nurse`. The dashboard loads the current robot state, battery level, location, full inventory, and both ongoing and completed task lists. All data updates live via WebSocket.

**Create an order**
Click **오더 생성** to open the order form. Select a task type (kit delivery, specimen, clothes refill, etc.), choose origin and destination (fixed location or 3-step bed selector), add clothing quantities if relevant, and submit. The new task appears immediately in the dashboard.

**Emergency call**
Click **긴급 호출** on the dashboard. The backend creates a priority-1 emergency task, opens an abnormal event, and pushes a red banner to every connected nurse tab via WebSocket.

**Resolve a banner**
Click **해제** on the red banner. The event is resolved in the database and a WebSocket broadcast clears the banner on all connected nurse tabs simultaneously.

**Robot Dashboard**
Navigate to **로봇 대시보드** for a full-screen monitoring view: colour-coded driving state, current position, live radar log of state transitions, E-stop button, and sensor/camera placeholders.

### Patient

**Login → Service selection**
Log in at `/login/patient` with a bed code. The page shows **옷 대여 신청** (Rental) and **옷 반납 신청** (Return) buttons.

**Request rental or return**
Tap a button, enter clothing quantities (top, bottom, bedding, other), and confirm. The screen switches to a waiting confirmation ("로봇이 곧 도착합니다").

**Robot arrival and delivery**
When the robot reaches the bed (MQTT → `ARRIVED` state), the screen updates automatically. When the robot opens its compartment (`DELIVERY_OPEN_PAT` state), the patient sees the delivery action screen with a **완료** (Complete) button.

**Complete delivery**
Tap **완료**. The task is marked complete on the server and a `task_status_update` is broadcast. The robot transitions to `COMPLETE` state, the patient sees a completion screen for 3 seconds, then returns to the service selection screen automatically.

---

## WebSocket Event Reference

All events follow the envelope `{ "event": "<type>", "data": { ... } }`.

| Event | Triggered by | Received by |
|---|---|---|
| `robot_state_update` | MQTT `robot/status` | All clients |
| `robot_location_update` | MQTT `robot/location` | All clients |
| `robot_battery_update` | MQTT `robot/battery` | All clients |
| `task_status_update` | Task complete (patient or MQTT) | All clients |
| `inventory_update` | `PUT /api/inventory/{id}` | All clients |
| `abnormal_event_update` | Emergency call, robot error/low-battery, banner resolve | All clients |

---

## Known Limitations

| Area | Detail |
|---|---|
| **Inventory real-time sync** | Inventory is fetched once on page load. The `inventory_update` WS event is wired on the frontend but the backend has no automatic mutation on task completion — no clothing counts are deducted or updated when a delivery finishes. |
| **Duplicate patient tasks** | A patient with an active task can submit a second request. The backend will create a second task without rejecting the duplicate. |
| **Token expiry** | Expired JWTs cause API calls to return 401, but the frontend has no auto-logout or token-refresh flow. The user sees an error message and must log in again manually. |
| **MQTT required for robot state** | Robot state transitions (ARRIVED, DELIVERY_OPEN_PAT, COMPLETE, etc.) are driven entirely by MQTT messages from the physical robot. Without a connected robot or an MQTT simulator, these screens cannot be reached in a live demo. State can be simulated by publishing directly to the MQTT broker or hitting the backend's internal service functions via the `/docs` UI. |
| **Single robot** | The scheduler and all UI assume exactly one robot (`AMR-001`). Multi-robot support would require significant schema and UI changes. |
| **Sensor node data** | The RobotDashboard sensor grid (LiDAR, IMU, Encoder, Ultrasonic, Motor L/R) shows static placeholder values. Live values require ROS2 topic integration. |
| **No camera stream** | The remote view panel in RobotDashboard is a placeholder. |

---

## Future Work

| Priority | Item |
|---|---|
| High | Add duplicate-task guard on `POST /tasks/patient/request` — reject if the patient already has an active task |
| High | Token expiry handling — auto-logout on 401 responses + optional silent refresh |
| High | Automatic inventory adjustment on task completion (deduct clothes delivered, increment used count) |
| Medium | Multi-robot support — extend the scheduler, robot model, and dashboard to handle a fleet |
| Medium | ROS2 bridge — pipe live sensor data (LiDAR, IMU, motor status) into the sensor node panel |
| Medium | Camera stream integration in the RobotDashboard remote view panel |
| Medium | Role-hardened endpoints — add nurse auth checks to `GET /tasks/ongoing`, `GET /tasks/completed`, and `GET /inventory` |
| Low | Task cancellation flow — allow nurses to cancel PENDING tasks from the dashboard |
| Low | Pagination for completed tasks (currently capped at 50) |
| Low | Push notifications or audio alert when the robot arrives at a patient bed |
| Low | Dark mode and responsive layout for larger ward displays |
