import { create } from 'zustand'
import type { Task, TaskStatus } from '@/types'

interface TaskState {
  ongoingTasks: Task[]
  completedTasks: Task[]
  patientActiveTask: Task | null

  setOngoingTasks: (tasks: Task[]) => void
  setCompletedTasks: (tasks: Task[]) => void
  setPatientActiveTask: (task: Task | null) => void

  /** Called by WebSocket task_status_update. Updates or moves the task. */
  applyTaskUpdate: (updated: Task) => void
}

const ACTIVE_STATUSES: TaskStatus[] = ['PENDING', 'DISPATCHED', 'IN_PROGRESS']
const DONE_STATUSES:   TaskStatus[] = ['COMPLETE', 'CANCELLED', 'FAILED']

export const useTaskStore = create<TaskState>((set) => ({
  ongoingTasks:      [],
  completedTasks:    [],
  patientActiveTask: null,

  setOngoingTasks:      (tasks) => set({ ongoingTasks: tasks }),
  setCompletedTasks:    (tasks) => set({ completedTasks: tasks }),
  setPatientActiveTask: (task)  => set({ patientActiveTask: task }),

  applyTaskUpdate(updated) {
    set((s) => {
      // Update patient active task if it matches
      const newPatient =
        s.patientActiveTask?.id === updated.id ? updated : s.patientActiveTask

      if (ACTIVE_STATUSES.includes(updated.status)) {
        // Upsert into ongoing
        const exists = s.ongoingTasks.some((t) => t.id === updated.id)
        const ongoing = exists
          ? s.ongoingTasks.map((t) => (t.id === updated.id ? updated : t))
          : [updated, ...s.ongoingTasks]
        // Remove from completed if it somehow moved back
        const completed = s.completedTasks.filter((t) => t.id !== updated.id)
        return { ongoingTasks: ongoing, completedTasks: completed, patientActiveTask: newPatient }
      }

      if (DONE_STATUSES.includes(updated.status)) {
        // Move from ongoing → completed
        const ongoing   = s.ongoingTasks.filter((t) => t.id !== updated.id)
        const completed = [updated, ...s.completedTasks.filter((t) => t.id !== updated.id)]
        // Keep patientActiveTask set to the DONE task so PatientScreenController
        // can still reach COMPLETE screen (robot state = COMPLETE) via Rule 4.
        // PatientRequestPage's 3-second auto-reset calls setPatientActiveTask(null)
        // after the COMPLETE screen, which is the correct cleanup point.
        return { ongoingTasks: ongoing, completedTasks: completed, patientActiveTask: newPatient }
      }

      return {}
    })
  },
}))
