from typing import Optional
from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    # Database
    DB_HOST: str = "localhost"
    DB_PORT: int = 1433
    DB_NAME: str = "hospital_amr"
    DB_USER: str = "sa"
    DB_PASSWORD: str = "YourPassword!"

    # Optional full URL override (useful for SQLite in dev/CI — overrides all DB_* fields)
    DATABASE_URL_OVERRIDE: Optional[str] = None

    # MQTT
    MQTT_HOST: str = "localhost"
    MQTT_PORT: int = 1883
    MQTT_CLIENT_ID: str = "hospital_amr_server"

    # JWT
    SECRET_KEY: str = "change-me-in-production"
    ALGORITHM: str = "HS256"
    ACCESS_TOKEN_EXPIRE_MINUTES: int = 480

    # App
    APP_HOST: str = "0.0.0.0"
    APP_PORT: int = 8000
    DEBUG: bool = True

    # Auth credentials (fixed by project spec)
    NURSE_LOGIN_ID: str = "hospital"
    NURSE_LOGIN_PW: str = "worker"
    PATIENT_LOGIN_ID: str = "hospital"

    # Inventory thresholds
    USED_CLOTHES_COLLECTION_THRESHOLD: int = 10
    CLOTHES_REFILL_LOW_THRESHOLD: int = 5

    # Low battery threshold (%)
    LOW_BATTERY_THRESHOLD: int = 20

    @property
    def DATABASE_URL(self) -> str:
        if self.DATABASE_URL_OVERRIDE:
            return self.DATABASE_URL_OVERRIDE
        return (
            f"mssql+pyodbc://{self.DB_USER}:{self.DB_PASSWORD}"
            f"@{self.DB_HOST}:{self.DB_PORT}/{self.DB_NAME}"
            "?driver=ODBC+Driver+17+for+SQL+Server"
        )

    class Config:
        env_file = ".env"
        case_sensitive = True


settings = Settings()
