import { publicApi } from './client'
import type { TokenPayload } from '@/types'

export function loginNurse(username: string, password: string) {
  return publicApi.post<TokenPayload>('/auth/nurse/login', { username, password })
}

export function loginPatient(username: string, password: string) {
  return publicApi.post<TokenPayload>('/auth/patient/login', { username, password })
}
