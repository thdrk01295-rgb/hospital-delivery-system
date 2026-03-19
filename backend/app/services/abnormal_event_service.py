"""
Abnormal event service.
Manages the lifecycle of device-abnormal events:
  error, low_battery, emergency_call.

Only one unresolved event is expected at a time.
"""
from datetime import datetime, timezone
from typing import Optional

from sqlalchemy.orm import Session

from app.models.abnormal_event import AbnormalEvent


ABNORMAL_EVENT_TYPES = frozenset({"error", "low_battery", "emergency_call"})


def open_event(db: Session, event_type: str,
               related_task_id: Optional[int] = None,
               note: Optional[str] = None) -> AbnormalEvent:
    """
    Records a new abnormal event.
    If an unresolved event of the same type already exists, returns it unchanged
    (idempotent — avoids duplicate events for sustained error states).
    """
    existing = (
        db.query(AbnormalEvent)
        .filter(
            AbnormalEvent.event_type == event_type,
            AbnormalEvent.resolved_at.is_(None),
        )
        .first()
    )
    if existing:
        return existing

    event = AbnormalEvent(
        event_type=event_type,
        related_task_id=related_task_id,
        note=note,
    )
    db.add(event)
    db.commit()
    db.refresh(event)
    return event


def resolve_event(db: Session, event_id: int) -> Optional[AbnormalEvent]:
    """Marks a specific event as resolved."""
    event = db.query(AbnormalEvent).filter(AbnormalEvent.id == event_id).first()
    if not event or event.resolved_at is not None:
        return event
    event.resolved_at = datetime.now(timezone.utc)
    db.commit()
    db.refresh(event)
    return event


def resolve_all_by_type(db: Session, event_type: str) -> list[AbnormalEvent]:
    """Resolves all open events of a given type (called when robot recovers)."""
    open_events = (
        db.query(AbnormalEvent)
        .filter(
            AbnormalEvent.event_type == event_type,
            AbnormalEvent.resolved_at.is_(None),
        )
        .all()
    )
    now = datetime.now(timezone.utc)
    for ev in open_events:
        ev.resolved_at = now
    db.commit()
    return open_events


def get_active_event(db: Session) -> Optional[AbnormalEvent]:
    """Returns the current unresolved abnormal event (highest priority / most recent)."""
    return (
        db.query(AbnormalEvent)
        .filter(AbnormalEvent.resolved_at.is_(None))
        .order_by(AbnormalEvent.occurred_at.desc())
        .first()
    )


def get_event_history(db: Session, limit: int = 50) -> list[AbnormalEvent]:
    return (
        db.query(AbnormalEvent)
        .order_by(AbnormalEvent.occurred_at.desc())
        .limit(limit)
        .all()
    )
