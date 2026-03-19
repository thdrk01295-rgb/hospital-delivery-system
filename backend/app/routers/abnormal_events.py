from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session
from typing import Optional

from app.db.session import get_db
from app.schemas.abnormal_event import AbnormalEventRead
from app.services.abnormal_event_service import (
    get_active_event,
    get_event_history,
    resolve_event,
)
from app.websocket.manager import ws_manager
from app.constants import ws_events

router = APIRouter(prefix="/abnormal-events", tags=["abnormal-events"])


@router.get("/active", response_model=Optional[AbnormalEventRead])
def active_abnormal_event(db: Session = Depends(get_db)):
    """
    Returns the current unresolved abnormal event, or null if none exists.
    The nurse dashboard polls/subscribes to this to drive the emergency banner.
    """
    return get_active_event(db)


@router.get("/history", response_model=list[AbnormalEventRead])
def abnormal_event_history(db: Session = Depends(get_db)):
    return get_event_history(db)


@router.post("/{event_id}/resolve", response_model=AbnormalEventRead)
async def resolve_abnormal_event(event_id: int, db: Session = Depends(get_db)):
    event = resolve_event(db, event_id)
    if not event:
        raise HTTPException(status_code=404, detail="Abnormal event not found")
    # Broadcast to all connected clients so every nurse tab clears its banner
    await ws_manager.broadcast(
        ws_events.ABNORMAL_EVENT_UPDATE,
        {"event_type": event.event_type, "event_id": event.id, "active": False},
    )
    return event
