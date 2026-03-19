import { create } from 'zustand'
import type { RobotStatus } from '@/types'

interface RobotState {
  robot: RobotStatus | null
  setRobot: (robot: RobotStatus) => void
  updateState: (data: Partial<RobotStatus>) => void
}

export const useRobotStore = create<RobotState>((set) => ({
  robot: null,

  setRobot(robot) {
    set({ robot })
  },

  updateState(data) {
    set((s) => ({
      robot: s.robot ? { ...s.robot, ...data } : (data as RobotStatus),
    }))
  },
}))
