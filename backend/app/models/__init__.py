# Import all models so SQLAlchemy's mapper registry is populated when this package is imported.
from app.models.location import Location
from app.models.robot import Robot
from app.models.task import Task
from app.models.inventory import ClothingInventory

__all__ = ["Location", "Robot", "Task", "ClothingInventory"]
