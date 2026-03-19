/**
 * Thin fetch wrapper.
 * Automatically attaches Bearer token from localStorage and throws on non-2xx.
 */

const BASE = '/api'

function getToken(): string | null {
  return localStorage.getItem('amr_token')
}

async function request<T>(
  method: string,
  path: string,
  body?: unknown,
): Promise<T> {
  const headers: Record<string, string> = {
    'Content-Type': 'application/json',
  }
  const token = getToken()
  if (token) headers['Authorization'] = `Bearer ${token}`

  const res = await fetch(`${BASE}${path}`, {
    method,
    headers,
    body: body !== undefined ? JSON.stringify(body) : undefined,
  })

  if (!res.ok) {
    const detail = await res.json().catch(() => ({ detail: res.statusText }))
    throw new Error(detail?.detail ?? `HTTP ${res.status}`)
  }

  // 204 No Content or empty body
  const text = await res.text()
  return text ? (JSON.parse(text) as T) : (null as T)
}

export const api = {
  get:    <T>(path: string)              => request<T>('GET',    path),
  post:   <T>(path: string, body?: unknown) => request<T>('POST',   path, body),
  put:    <T>(path: string, body?: unknown) => request<T>('PUT',    path, body),
  delete: <T>(path: string)              => request<T>('DELETE', path),
}
