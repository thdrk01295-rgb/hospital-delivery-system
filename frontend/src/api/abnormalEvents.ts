import { publicApi, nurseApi } from './client'
import type { AbnormalEvent } from '@/types'

/** Public — no auth required. */
export function fetchActiveAbnormalEvent() {
  return publicApi.get<AbnormalEvent | null>('/abnormal-events/active')
}

/** Nurse-only — backend enforces role=nurse. */
export function resolveAbnormalEvent(eventId: number) {
  return nurseApi.post<AbnormalEvent>(`/abnormal-events/${eventId}/resolve`)
}
