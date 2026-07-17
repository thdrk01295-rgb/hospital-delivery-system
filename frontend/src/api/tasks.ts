import { nurseApi, patientApi, publicApi } from './client'
import type { Task, NurseOrderCreate, PatientClothingRequestCreate } from '@/types'

// ── Nurse endpoints (nurse JWT required) ─────────────────────────────────────

export function createNurseOrder(body: NurseOrderCreate) {
  return nurseApi.post<Task>('/tasks/nurse/order', body)
}

export function cancelNurseTask(taskId: number) {
  return nurseApi.post<Task>(`/tasks/nurse/cancel/${taskId}`)
}

export function triggerEmergency() {
  return nurseApi.post<Task>('/tasks/nurse/emergency')
}

export function releaseEmergency() {
  return nurseApi.post<{ status: string }>('/tasks/nurse/emergency/release')
}

// ── Patient endpoints (patient JWT required) ──────────────────────────────────

export function fetchPatientActiveTask() {
  return patientApi.get<Task | null>('/tasks/patient/me')
}

export function submitPatientClothingRequest(body: PatientClothingRequestCreate) {
  return patientApi.post<Task>('/tasks/patient/request', body)
}

export function completeTask(taskId: number) {
  return patientApi.post<Task>(`/tasks/${taskId}/complete`)
}

export function cancelPatientTask() {
  return patientApi.post<Task>('/tasks/patient/cancel')
}

// ── Public endpoints (no auth required by backend) ───────────────────────────

export function fetchOngoingTasks() {
  return publicApi.get<Task[]>('/tasks/ongoing')
}

export function fetchCompletedTasks() {
  return publicApi.get<Task[]>('/tasks/completed')
}
