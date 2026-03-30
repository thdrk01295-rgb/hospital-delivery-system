import asyncio
import logging

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from app.routers import auth, locations, tasks, robot, inventory, abnormal_events
from app.websocket.router import router as ws_router
from app.mqtt.client import start_mqtt
from app.mqtt.handlers import set_event_loop

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

app = FastAPI(
    title="Hospital Logistics AMR API",
    version="1.0.0",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],   # tighten in production
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# ── Routers ──────────────────────────────────────────────────────────────────
app.include_router(auth.router,             prefix="/api")
app.include_router(locations.router,        prefix="/api")
app.include_router(tasks.router,            prefix="/api")
app.include_router(robot.router,            prefix="/api")
app.include_router(inventory.router,        prefix="/api")
app.include_router(abnormal_events.router,  prefix="/api")
app.include_router(ws_router)               # WebSocket at /ws


@app.on_event("startup")
async def on_startup():
    import os
    from app.config.settings import settings

    db_url = settings.DATABASE_URL
    logger.info(f"DATABASE_URL: {db_url}")
    if db_url.startswith("sqlite"):
        db_path = db_url.replace("sqlite:///", "")
        logger.info(f"SQLite file (absolute): {os.path.abspath(db_path)}")

    # Ensure tables exist (dev convenience; in production run alembic upgrade head first)
    try:
        from app.db.init_db import create_all_tables
        create_all_tables()
        logger.info("DB tables verified.")
    except Exception as exc:
        logger.warning(f"DB table init skipped: {exc}")

    # Seed fixed locations — idempotent, skips rows that already exist
    try:
        from seeds.seed_locations import seed_all
        from app.db.session import SessionLocal
        from app.models.location import Location
        seed_all()
        db = SessionLocal()
        count = db.query(Location).count()
        db.close()
        logger.info(f"Location seed complete. Total locations in DB: {count}")
    except Exception as exc:
        logger.warning(f"Location seeding skipped: {exc}")

    logger.info("Starting MQTT client...")
    set_event_loop(asyncio.get_event_loop())
    start_mqtt()

    logger.info("Hospital AMR backend is ready.")


@app.get("/health")
def health():
    return {"status": "ok"}
