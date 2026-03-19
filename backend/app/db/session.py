from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker

from app.config.settings import settings

_url = settings.DATABASE_URL
if _url.startswith("sqlite"):
    # SQLite does not support pool_size / max_overflow / pool_pre_ping
    engine = create_engine(_url, echo=settings.DEBUG,
                           connect_args={"check_same_thread": False})
else:
    engine = create_engine(_url, echo=settings.DEBUG,
                           pool_pre_ping=True, pool_size=10, max_overflow=20)

SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)


def get_db():
    """FastAPI dependency — yields a DB session and closes it after use."""
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()
