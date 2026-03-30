from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    # Database
    DB_HOST: str = "localhost"
    DB_PORT: int = 1433
    DB_NAME: str = "hospital_amr"
    DB_USER: str = "sa"
    DB_PASSWORD: str = "YourPassword!"

    # Full DB URL. Defaults to a local SQLite file so the app works out-of-the-box
    # without any external database. Set this to a mssql+pyodbc:// URL (or any
    # SQLAlchemy URL) to use a different database.
    DATABASE_URL_OVERRIDE: str = "sqlite:///./hospital_amr.db"

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
        return self.DATABASE_URL_OVERRIDE

    class Config:
        env_file = ".env"
        case_sensitive = True


settings = Settings()
