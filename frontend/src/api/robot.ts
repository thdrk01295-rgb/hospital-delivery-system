import { api } from './client'
import type { RobotStatus } from '@/types'

export function fetchRobotStatus() {
  return api.get<RobotStatus>('/robot/status')
}

/** Tablet UI: POST /robot/lock-command — no auth required. */
export function sendLockCommand(robotId: string, command: 'UNLOCK' | 'LOCK') {
  return api.post<{ status: string; payload: object }>('/robot/lock-command', {
    robot_id: robotId,
    command,
  })
}
