"""
Inventory service — manages clothing stock per location.
Broadcasts inventory_update over WebSocket after every mutation.
"""
import asyncio
import logging

from sqlalchemy.orm import Session

from app.models.inventory import ClothingInventory

logger = logging.getLogger(__name__)


def get_all_inventory(db: Session) -> list[ClothingInventory]:
    return db.query(ClothingInventory).join(ClothingInventory.location).all()


def get_inventory_by_location(db: Session, location_id: int) -> ClothingInventory | None:
    return (
        db.query(ClothingInventory)
        .filter(ClothingInventory.location_id == location_id)
        .first()
    )


def upsert_inventory(db: Session, location_id: int,
                     clean_count: int, used_count: int) -> ClothingInventory:
    inv = get_inventory_by_location(db, location_id)
    if inv:
        inv.clean_count = clean_count
        inv.used_count = used_count
    else:
        inv = ClothingInventory(
            location_id=location_id,
            clean_count=clean_count,
            used_count=used_count,
        )
        db.add(inv)
    db.commit()
    db.refresh(inv)

    _broadcast_inventory_update(inv)
    return inv


def _broadcast_inventory_update(inv: ClothingInventory) -> None:
    """
    Fire-and-forget broadcast of inventory_update over WebSocket.
    Safe to call from both async (router) and sync (service/scheduler) contexts.
    """
    from app.websocket.manager import ws_manager
    from app.constants import ws_events

    payload = {
        "location_id": inv.location_id,
        "clean_count": inv.clean_count,
        "used_count": inv.used_count,
    }

    try:
        loop = asyncio.get_running_loop()
        loop.create_task(ws_manager.broadcast(ws_events.INVENTORY_UPDATE, payload))
    except RuntimeError:
        # Called from a sync/MQTT-thread context — schedule on the running loop
        try:
            loop = asyncio.get_event_loop()
            if loop.is_running():
                asyncio.run_coroutine_threadsafe(
                    ws_manager.broadcast(ws_events.INVENTORY_UPDATE, payload), loop
                )
        except Exception as e:
            logger.warning(f"inventory_update broadcast skipped: {e}")
