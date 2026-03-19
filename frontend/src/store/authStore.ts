import { create } from 'zustand'
import type { UserRole } from '@/types'

interface AuthState {
  token: string | null
  role: UserRole | null
  bedCode: string | null   // patient only

  setAuth: (token: string, role: UserRole, bedCode: string | null) => void
  logout: () => void
}

export const useAuthStore = create<AuthState>((set) => ({
  token:   localStorage.getItem('amr_token'),
  role:    (localStorage.getItem('amr_role') as UserRole | null),
  bedCode: localStorage.getItem('amr_bed_code'),

  setAuth(token, role, bedCode) {
    localStorage.setItem('amr_token', token)
    localStorage.setItem('amr_role', role)
    if (bedCode) localStorage.setItem('amr_bed_code', bedCode)
    else         localStorage.removeItem('amr_bed_code')
    set({ token, role, bedCode })
  },

  logout() {
    localStorage.removeItem('amr_token')
    localStorage.removeItem('amr_role')
    localStorage.removeItem('amr_bed_code')
    set({ token: null, role: null, bedCode: null })
  },
}))
