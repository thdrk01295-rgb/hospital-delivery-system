/**
 * Role-separated fetch clients.
 *
 * publicApi  — no Authorization header (public endpoints)
 * nurseApi   — reads nurse_access_token from sessionStorage
 * patientApi — reads patient_access_token from sessionStorage
 *
 * Tokens are read at request time so that a login or logout occurring between
 * two requests in the same render cycle is reflected immediately.
 */

const BASE = '/api'

function getNurseToken():   string | null { return sessionStorage.getItem('nurse_access_token') }
function getPatientToken(): string | null { return sessionStorage.getItem('patient_access_token') }

async function request<T>(
  method: string,
  path: string,
  body?: unknown,
  getToken?: () => string | null,
): Promise<T> {
  const headers: Record<string, string> = {
    'Content-Type': 'application/json',
  }
  const token = getToken?.() ?? null
  if (token) headers['Authorization'] = `Bearer ${token}`

  const res = await fetch(`${BASE}${path}`, {
    method,
    headers,
    body: body !== undefined ? JSON.stringify(body) : undefined,
  })

  if (!res.ok) {
    const errBody = await res.json().catch(() => ({ detail: res.statusText }))
    const raw = errBody?.detail
    // FastAPI 422 returns detail as an array of validation error objects
    const msg = Array.isArray(raw)
      ? (raw[0]?.msg ?? raw[0]?.message ?? `HTTP ${res.status}`)
      : (raw ?? `HTTP ${res.status}`)
    throw new Error(typeof msg === 'string' ? msg : JSON.stringify(msg))
  }

  // 204 No Content or empty body
  const text = await res.text()
  return text ? (JSON.parse(text) as T) : (null as T)
}

function makeClient(getToken: () => string | null) {
  return {
    get:    <T>(path: string)                   => request<T>('GET',    path, undefined, getToken),
    post:   <T>(path: string, body?: unknown)   => request<T>('POST',   path, body,      getToken),
    put:    <T>(path: string, body?: unknown)   => request<T>('PUT',    path, body,      getToken),
    delete: <T>(path: string)                   => request<T>('DELETE', path, undefined, getToken),
  }
}

/** No Authorization header. Use for login endpoints and public data reads. */
export const publicApi  = makeClient(() => null)

/** Attaches the nurse JWT from sessionStorage['nurse_access_token']. */
export const nurseApi   = makeClient(getNurseToken)

/** Attaches the patient JWT from sessionStorage['patient_access_token']. */
export const patientApi = makeClient(getPatientToken)
