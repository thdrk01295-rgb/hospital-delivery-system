import asyncio
import logging

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from app.db.init_db import init_db
from app.routers import auth, locations, tasks, robot, inventory
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
app.include_router(auth.router,      prefix="/api")
app.include_router(locations.router, prefix="/api")
app.include_router(tasks.router,     prefix="/api")
app.include_router(robot.router,     prefix="/api")
app.include_router(inventory.router, prefix="/api")
app.include_router(ws_router)        # WebSocket at /ws


@app.on_event("startup")
async def on_startup():
    logger.info("Initializing database tables...")
    init_db()

    logger.info("Starting MQTT client...")
    set_event_loop(asyncio.get_event_loop())
    start_mqtt()

    logger.info("Hospital AMR backend is ready.")


@app.get("/health")
def health():
    return {"status": "ok"}
