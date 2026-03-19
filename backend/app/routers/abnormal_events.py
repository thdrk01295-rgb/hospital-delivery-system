from fastapi import APIRouter, Depends, Header, HTTPException, status
from sqlalchemy.orm import Session
from typing import Optional

from app.db.session import get_db
from app.schemas.abnormal_event import AbnormalEventRead
from app.services.abnormal_event_service import (
    get_active_event,
    get_event_history,
    resolve_event,
)
from app.services.auth_service import decode_token
from app.websocket.manager import ws_manager
from app.constants import ws_events

router = APIRouter(prefix="/abnormal-events", tags=["abnormal-events"])


def _require_nurse(authorization: Optional[str]) -> None:
    if not authorization or not authorization.startswith("Bearer "):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Missing token")
    try:
        payload = decode_token(authorization.split(" ", 1)[1])
    except Exception:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid token")
    if payload.get("role") != "nurse":
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Nurses only")


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
async def resolve_abnormal_event(
    event_id: int,
    authorization: Optional[str] = Header(default=None),
    db: Session = Depends(get_db),
):
    _require_nurse(authorization)
    event = resolve_event(db, event_id)
    if not event:
        raise HTTPException(status_code=404, detail="Abnormal event not found")
    # Broadcast to all connected clients so every nurse tab clears its banner
    await ws_manager.broadcast(
        ws_events.ABNORMAL_EVENT_UPDATE,
        {"event_type": event.event_type, "event_id": event.id, "active": False},
    )
    return event
