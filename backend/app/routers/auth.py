from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy.orm import Session

from app.db.session import get_db
from app.schemas.auth import LoginRequest, TokenResponse
from app.services.auth_service import authenticate_nurse, authenticate_patient

router = APIRouter(prefix="/auth", tags=["auth"])


@router.post("/nurse/login", response_model=TokenResponse)
def nurse_login(body: LoginRequest, db: Session = Depends(get_db)):
    token = authenticate_nurse(body.username, body.password)
    if not token:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid credentials")
    return TokenResponse(access_token=token, role="nurse")


@router.post("/patient/login", response_model=TokenResponse)
def patient_login(body: LoginRequest, db: Session = Depends(get_db)):
    result = authenticate_patient(body.username, body.password, db)
    if not result:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED,
                            detail="Invalid credentials or bed not found")
    token, bed_code = result
    return TokenResponse(access_token=token, role="patient", bed_code=bed_code)
