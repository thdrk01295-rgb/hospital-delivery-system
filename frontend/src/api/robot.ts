import { api } from './client'
import type { RobotStatus } from '@/types'

export function fetchRobotStatus() {
  return api.get<RobotStatus>('/robot/status')
}
