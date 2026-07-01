"""
Shared in-memory store for per-task lock phases (v4 Lock Completion Guard).

Imported by handlers.py, robot.py, and tasks.py so that none of them has to
import from each other. The store itself has no app dependencies — no circular
import risk at any level.

Lifecycle:
  WAIT_UNLOCK received  → clear (new unlock cycle begins)
  UNLOCK+OPENED         → set "OPENED"
  LOCK+LOCKED (after OPENED only) → set "RELOCKED"
  Task terminal / requeue → clear

TODO: persist to DB if server-restart resilience is required.
"""

_phases: dict[int, str] = {}


def get_task_lock_phase(task_id: int) -> str | None:
    return _phases.get(task_id)


def set_task_lock_phase(task_id: int, phase: str) -> None:
    _phases[task_id] = phase


def clear_task_lock_phase(task_id: int) -> None:
    _phases.pop(task_id, None)
