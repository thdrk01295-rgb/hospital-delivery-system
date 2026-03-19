import { api } from './client'
import type { Task, NurseOrderCreate, PatientClothingRequestCreate } from '@/types'

// Nurse
export function createNurseOrder(body: NurseOrderCreate) {
  return api.post<Task>('/tasks/nurse/order', body)
}

export function triggerEmergency() {
  return api.post<Task>('/tasks/nurse/emergency')
}

// Patient
export function fetchPatientActiveTask() {
  return api.get<Task | null>('/tasks/patient/me')
}

export function submitPatientClothingRequest(body: PatientClothingRequestCreate) {
  return api.post<Task>('/tasks/patient/request', body)
}

export function completeTask(taskId: number) {
  return api.post<Task>(`/tasks/${taskId}/complete`)
}

// Shared
export function fetchOngoingTasks() {
  return api.get<Task[]>('/tasks/ongoing')
}

export function fetchCompletedTasks() {
  return api.get<Task[]>('/tasks/completed')
}
