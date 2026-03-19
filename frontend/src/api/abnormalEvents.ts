import { api } from './client'
import type { AbnormalEvent } from '@/types'

export function fetchActiveAbnormalEvent() {
  return api.get<AbnormalEvent | null>('/abnormal-events/active')
}

export function resolveAbnormalEvent(eventId: number) {
  return api.post<AbnormalEvent>(`/abnormal-events/${eventId}/resolve`)
}
