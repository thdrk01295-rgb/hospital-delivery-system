/**
 * PatientScreenController
 * ────────────────────────
 * Pure function — no React state. Takes current task + robot state and
 * returns the PatientScreenMode to render.
 *
 * Rules (from approved Step 6, final amendment):
 *
 *   Rule 0 (gate): If no active task → SERVICE_SELECTION always.
 *                  Robot state is IGNORED when no active task exists.
 *
 *   Rule 1 (high): Robot state is ERROR | LOW_BATTERY | CHAGING_BATTERY
 *                  → ROBOT_STATUS
 *
 *   Rule 2:        Robot state = DELIVERY_OPEN_PAT → DELIVERY_ACTION
 *   Rule 3:        Robot state = ARRIVED           → ROBOT_ARRIVED
 *   Rule 4:        Robot state = COMPLETE          → COMPLETE (caller resets after 3s)
 *
 *   Rule 5 (low):  Task status PENDING | DISPATCHED | IN_PROGRESS
 *                  + task_type = patient_clothes_rental → RENTAL_CONFIRM
 *                  + task_type = patient_clothes_return → RETURN_CONFIRM
 */
import type { Task, RobotState, PatientScreenMode } from '@/types'

const ABNORMAL_STATES: RobotState[] = ['ERROR', 'LOW_BATTERY', 'CHAGING_BATTERY']
const ACTIVE_STATUSES = ['PENDING', 'DISPATCHED', 'IN_PROGRESS'] as const

export function resolvePatientScreenMode(
  activeTask: Task | null,
  robotState: RobotState | null,
): PatientScreenMode {
  // Rule 0 — no active task → SERVICE_SELECTION regardless of robot state
  if (!activeTask) return 'SERVICE_SELECTION'

  // Rules 1–4 require a robot state
  if (robotState) {
    // Rule 1 — abnormal states override everything
    if ((ABNORMAL_STATES as RobotState[]).includes(robotState)) return 'ROBOT_STATUS'

    // Rule 2
    if (robotState === 'DELIVERY_OPEN_PAT') return 'DELIVERY_ACTION'

    // Rule 3
    if (robotState === 'ARRIVED') return 'ROBOT_ARRIVED'

    // Rule 4
    if (robotState === 'COMPLETE') return 'COMPLETE'
  }

  // Rule 5 — task status drives confirm screens
  if ((ACTIVE_STATUSES as readonly string[]).includes(activeTask.status)) {
    return activeTask.task_type === 'patient_clothes_rental'
      ? 'RENTAL_CONFIRM'
      : 'RETURN_CONFIRM'
  }

  return 'SERVICE_SELECTION'
}
