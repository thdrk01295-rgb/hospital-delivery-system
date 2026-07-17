import { publicApi } from './client'
import type { RobotStatus } from '@/types'

export function fetchRobotStatus() {
  return publicApi.get<RobotStatus>('/robot/status')
}

/** Tablet UI: POST /robot/lock-command — no auth required. */
export function sendLockCommand(robotId: string, command: 'UNLOCK' | 'LOCK') {
  return publicApi.post<{ status: string; payload: object }>('/robot/lock-command', {
    robot_id: robotId,
    command,
  })
}

export interface CompleteTaskResult {
  status: string
  task_id: number
  stop_type: 'origin' | 'destination'
  is_final: boolean
  next_action: 'MOVE_TO_DESTINATION' | 'FINISH_TASK'
}

/** Tablet UI: POST /robot/complete-task — origin-stop or destination-stop completion. */
export function completeRobotTask(robotId: string) {
  return publicApi.post<CompleteTaskResult>('/robot/complete-task', { robot_id: robotId })
}
