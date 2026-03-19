import { api } from './client'
import type { TokenPayload } from '@/types'

export function loginNurse(username: string, password: string) {
  return api.post<TokenPayload>('/auth/nurse/login', { username, password })
}

export function loginPatient(username: string, password: string) {
  return api.post<TokenPayload>('/auth/patient/login', { username, password })
}
