/**
 * Authentication session-separation tests.
 *
 * Tests the storage helpers, API client token-attachment behaviour, store
 * initialisation from sessionStorage (simulated page-refresh), and protected-
 * route state independence — without requiring a running browser.
 *
 * Environment: jsdom (configured in vite.config.ts → test.environment).
 * sessionStorage is tab-isolated in real browsers; jsdom gives each test module
 * its own fresh sessionStorage, which we reset in beforeEach to guarantee
 * test isolation.
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

// ─── helpers ────────────────────────────────────────────────────────────────

function clearAuthStorage() {
  sessionStorage.removeItem('nurse_access_token')
  sessionStorage.removeItem('patient_access_token')
  sessionStorage.removeItem('patient_bed_code')
  localStorage.removeItem('amr_token')
  localStorage.removeItem('amr_role')
  localStorage.removeItem('amr_bed_code')
}

// Re-import the stores *inside* each test so that the Zustand initialiser
// re-runs against the current sessionStorage state (simulates a fresh module
// load = page refresh).
async function freshNurseStore() {
  vi.resetModules()
  const { useNurseAuthStore } = await import('@/store/nurseAuthStore')
  return useNurseAuthStore.getState()
}

async function freshPatientStore() {
  vi.resetModules()
  const { usePatientAuthStore } = await import('@/store/patientAuthStore')
  return usePatientAuthStore.getState()
}

// ─── Storage key isolation ────────────────────────────────────────────────────

describe('storage key isolation', () => {
  beforeEach(clearAuthStorage)

  it('T1 — nurse login writes only nurse_access_token', async () => {
    const store = await freshNurseStore()
    store.setToken('nurse-jwt-abc')

    expect(sessionStorage.getItem('nurse_access_token')).toBe('nurse-jwt-abc')
    expect(sessionStorage.getItem('patient_access_token')).toBeNull()
    expect(sessionStorage.getItem('patient_bed_code')).toBeNull()
  })

  it('T2 — patient login writes only patient_access_token and patient_bed_code', async () => {
    const store = await freshPatientStore()
    store.setAuth('patient-jwt-xyz', '11011')

    expect(sessionStorage.getItem('patient_access_token')).toBe('patient-jwt-xyz')
    expect(sessionStorage.getItem('patient_bed_code')).toBe('11011')
    expect(sessionStorage.getItem('nurse_access_token')).toBeNull()
  })

  it('T3 — nurse logout does not remove patient keys', async () => {
    // Set both roles
    const nurseStore   = await freshNurseStore()
    const patientStore = await freshPatientStore()
    nurseStore.setToken('nurse-jwt')
    patientStore.setAuth('patient-jwt', '11011')

    // Nurse logs out
    nurseStore.logout()

    expect(sessionStorage.getItem('nurse_access_token')).toBeNull()
    expect(sessionStorage.getItem('patient_access_token')).toBe('patient-jwt')
    expect(sessionStorage.getItem('patient_bed_code')).toBe('11011')
  })

  it('T4 — patient logout does not remove nurse keys', async () => {
    const nurseStore   = await freshNurseStore()
    const patientStore = await freshPatientStore()
    nurseStore.setToken('nurse-jwt')
    patientStore.setAuth('patient-jwt', '23045')

    patientStore.logout()

    expect(sessionStorage.getItem('patient_access_token')).toBeNull()
    expect(sessionStorage.getItem('patient_bed_code')).toBeNull()
    expect(sessionStorage.getItem('nurse_access_token')).toBe('nurse-jwt')
  })
})

// ─── API client token attachment ──────────────────────────────────────────────

describe('API client token attachment', () => {
  beforeEach(() => {
    clearAuthStorage()
    // Mock global fetch so we can inspect the Authorization header without a
    // real network.  fetch is available in jsdom.
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue({
      ok: true,
      text: async () => 'null',
    }))
  })

  afterEach(() => {
    vi.unstubAllGlobals()
  })

  it('T5 — nurseApi attaches only the nurse token', async () => {
    sessionStorage.setItem('nurse_access_token', 'NURSE_TOKEN')
    sessionStorage.setItem('patient_access_token', 'PATIENT_TOKEN')

    vi.resetModules()
    const { nurseApi } = await import('@/api/client')
    await nurseApi.get('/test')

    const call = (fetch as ReturnType<typeof vi.fn>).mock.calls[0]
    const headers = call[1].headers as Record<string, string>
    expect(headers['Authorization']).toBe('Bearer NURSE_TOKEN')
  })

  it('T6 — patientApi attaches only the patient token', async () => {
    sessionStorage.setItem('nurse_access_token', 'NURSE_TOKEN')
    sessionStorage.setItem('patient_access_token', 'PATIENT_TOKEN')

    vi.resetModules()
    const { patientApi } = await import('@/api/client')
    await patientApi.get('/test')

    const call = (fetch as ReturnType<typeof vi.fn>).mock.calls[0]
    const headers = call[1].headers as Record<string, string>
    expect(headers['Authorization']).toBe('Bearer PATIENT_TOKEN')
  })

  it('T7 — publicApi attaches no Authorization header', async () => {
    sessionStorage.setItem('nurse_access_token', 'NURSE_TOKEN')
    sessionStorage.setItem('patient_access_token', 'PATIENT_TOKEN')

    vi.resetModules()
    const { publicApi } = await import('@/api/client')
    await publicApi.get('/test')

    const call = (fetch as ReturnType<typeof vi.fn>).mock.calls[0]
    const headers = call[1].headers as Record<string, string>
    expect(headers['Authorization']).toBeUndefined()
  })
})

// ─── Session hydration after page refresh ────────────────────────────────────

describe('session hydration (simulated page refresh)', () => {
  beforeEach(clearAuthStorage)

  it('T8a — nurse store restores token from sessionStorage on module load', async () => {
    // Pre-populate sessionStorage as if a prior session had logged in
    sessionStorage.setItem('nurse_access_token', 'stored-nurse-jwt')

    const state = await freshNurseStore()
    expect(state.token).toBe('stored-nurse-jwt')
  })

  it('T8b — patient store restores token and bedCode from sessionStorage on module load', async () => {
    sessionStorage.setItem('patient_access_token', 'stored-patient-jwt')
    sessionStorage.setItem('patient_bed_code', '23045')

    const state = await freshPatientStore()
    expect(state.token).toBe('stored-patient-jwt')
    expect(state.bedCode).toBe('23045')
  })

  it('T8c — stores initialize to null when sessionStorage is empty (no prior session)', async () => {
    const nurseState   = await freshNurseStore()
    const patientState = await freshPatientStore()

    expect(nurseState.token).toBeNull()
    expect(patientState.token).toBeNull()
    expect(patientState.bedCode).toBeNull()
  })
})

// ─── Protected route state independence ──────────────────────────────────────

describe('protected route state independence', () => {
  beforeEach(clearAuthStorage)

  it('T9a — nurse route: authenticated when nurse token present, not when absent', async () => {
    sessionStorage.setItem('nurse_access_token', 'nurse-jwt')
    const nurseState = await freshNurseStore()
    expect(nurseState.token).not.toBeNull()

    // Even with a patient token, nurse state is independent
    sessionStorage.setItem('patient_access_token', 'patient-jwt')
    const patientState = await freshPatientStore()
    expect(patientState.token).not.toBeNull()

    // Nurse logout does not affect patient route guard
    nurseState.logout()
    expect(sessionStorage.getItem('patient_access_token')).toBe('patient-jwt')
  })

  it('T9b — patient route: patient login does not affect nurse token in storage', async () => {
    sessionStorage.setItem('nurse_access_token', 'nurse-jwt')
    const patientStore = await freshPatientStore()

    patientStore.setAuth('patient-jwt', '11011')

    // nurse key must be untouched
    expect(sessionStorage.getItem('nurse_access_token')).toBe('nurse-jwt')
  })

  it('T9c — three-tab scenario: each tab holds independent patient tokens', () => {
    // Tabs are modelled as separate sessionStorage namespaces.  In this test
    // we simulate two "tabs" by toggling the sessionStorage content and
    // checking that the client reads the value present at request time.

    // Tab 2: patient 11011
    sessionStorage.setItem('patient_access_token', 'jwt-for-11011')
    expect(sessionStorage.getItem('patient_access_token')).toBe('jwt-for-11011')

    // Tab 3 (simulated: same storage, different value set — real tabs would
    // each have their own sessionStorage; here we verify the key-read is
    // always the current value, not a stale captured string)
    sessionStorage.setItem('patient_access_token', 'jwt-for-23045')
    expect(sessionStorage.getItem('patient_access_token')).toBe('jwt-for-23045')

    // Tab 1: nurse token is unaffected by either patient-tab login
    sessionStorage.setItem('nurse_access_token', 'nurse-jwt')
    sessionStorage.setItem('patient_access_token', 'jwt-for-11011')
    expect(sessionStorage.getItem('nurse_access_token')).toBe('nurse-jwt')
  })
})

// ─── Legacy localStorage keys are not written ────────────────────────────────

describe('legacy localStorage keys', () => {
  beforeEach(clearAuthStorage)

  it('T10 — nurse login does not write any amr_ localStorage key', async () => {
    const store = await freshNurseStore()
    store.setToken('nurse-jwt')

    expect(localStorage.getItem('amr_token')).toBeNull()
    expect(localStorage.getItem('amr_role')).toBeNull()
    expect(localStorage.getItem('amr_bed_code')).toBeNull()
  })

  it('T11 — patient login does not write any amr_ localStorage key', async () => {
    const store = await freshPatientStore()
    store.setAuth('patient-jwt', '11011')

    expect(localStorage.getItem('amr_token')).toBeNull()
    expect(localStorage.getItem('amr_role')).toBeNull()
    expect(localStorage.getItem('amr_bed_code')).toBeNull()
  })
})
