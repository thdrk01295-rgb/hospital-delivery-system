"""
Shared in-memory store for per-task route stage (origin / destination stop tracking).

Imported by robot.py, dispatcher.py, handlers.py, and tasks.py so that none of
them imports from each other. The store has no app dependencies — no circular
import risk at any level.

Lifecycle:
  Task dispatched with origin    → set "origin"   (two-stop task)
  Task dispatched without origin → set "destination" (single-stop task, goes straight to destination)
  Origin Complete (tablet)       → set "destination"
  Destination Complete / task terminal (cancel/fail/emergency) → clear

get_current_stop defaults to "origin" so a server restart mid-task is safe:
the worst outcome is an extra origin-complete publish, not a premature FINISH_TASK.

TODO: persist to DB if server-restart resilience is required.
"""

_stops: dict[int, str] = {}  # task_id -> "origin" | "destination"


def get_current_stop(task_id: int) -> str:
    """Returns "origin" when no entry exists (safe default after server restart)."""
    return _stops.get(task_id, "origin")


def set_current_stop(task_id: int, stop: str) -> None:
    _stops[task_id] = stop


def clear_current_stop(task_id: int) -> None:
    _stops.pop(task_id, None)
