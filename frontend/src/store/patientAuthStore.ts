import { create } from 'zustand'

const TOKEN_KEY = 'patient_access_token'
const BED_KEY   = 'patient_bed_code'

interface PatientAuthState {
  token: string | null
  bedCode: string | null
  setAuth: (token: string, bedCode: string) => void
  logout: () => void
}

export const usePatientAuthStore = create<PatientAuthState>((set) => ({
  token:   sessionStorage.getItem(TOKEN_KEY),
  bedCode: sessionStorage.getItem(BED_KEY),

  setAuth(token, bedCode) {
    sessionStorage.setItem(TOKEN_KEY, token)
    sessionStorage.setItem(BED_KEY, bedCode)
    set({ token, bedCode })
  },

  logout() {
    sessionStorage.removeItem(TOKEN_KEY)
    sessionStorage.removeItem(BED_KEY)
    set({ token: null, bedCode: null })
  },
}))
