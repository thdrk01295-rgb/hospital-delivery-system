import { create } from 'zustand'

const KEY = 'nurse_access_token'

interface NurseAuthState {
  token: string | null
  setToken: (token: string) => void
  logout: () => void
}

export const useNurseAuthStore = create<NurseAuthState>((set) => ({
  token: sessionStorage.getItem(KEY),

  setToken(token) {
    sessionStorage.setItem(KEY, token)
    set({ token })
  },

  logout() {
    sessionStorage.removeItem(KEY)
    set({ token: null })
  },
}))
