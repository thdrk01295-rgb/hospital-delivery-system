"""
Authentication service.
Login rules are fixed by the project spec:
  Nurse  — ID: "hospital"  PW: "worker"
  Patient — ID: "hospital"  PW: <5-digit bed code>
"""
from datetime import datetime, timedelta, timezone

from jose import jwt
from sqlalchemy.orm import Session

from app.config.settings import settings
from app.models.location import Location
from app.constants.enums import LocationType


def _create_token(data: dict, expires_minutes: int) -> str:
    payload = data.copy()
    payload["exp"] = datetime.now(timezone.utc) + timedelta(minutes=expires_minutes)
    return jwt.encode(payload, settings.SECRET_KEY, algorithm=settings.ALGORITHM)


def authenticate_nurse(username: str, password: str) -> str | None:
    """Returns a JWT token if credentials are valid, else None."""
    if username == settings.NURSE_LOGIN_ID and password == settings.NURSE_LOGIN_PW:
        token = _create_token({"sub": "nurse", "role": "nurse"},
                               settings.ACCESS_TOKEN_EXPIRE_MINUTES)
        return token
    return None


def authenticate_patient(username: str, password: str, db: Session) -> tuple[str, str] | None:
    """
    Verifies that the password is a valid bed code and returns (token, bed_code).
    Returns None if invalid.
    """
    if username != settings.PATIENT_LOGIN_ID:
        return None

    bed_code = password
    location = db.query(Location).filter(
        Location.location_code == bed_code,
        Location.location_type == LocationType.BED,
        Location.is_active == True,
    ).first()

    if not location:
        return None

    token = _create_token(
        {"sub": bed_code, "role": "patient", "bed_code": bed_code},
        settings.ACCESS_TOKEN_EXPIRE_MINUTES,
    )
    return token, bed_code


def decode_token(token: str) -> dict:
    return jwt.decode(token, settings.SECRET_KEY, algorithms=[settings.ALGORITHM])
