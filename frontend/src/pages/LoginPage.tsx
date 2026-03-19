/**
 * Shared login page — renders nurse or patient variant based on `variant` prop.
 *
 * Nurse:   ID "hospital" / PW "worker"
 * Patient: ID "hospital" / PW <5-digit bed code>
 */
import { useState, FormEvent } from 'react'
import { useNavigate } from 'react-router-dom'
import { loginNurse, loginPatient } from '@/api/auth'
import { useAuthStore } from '@/store/authStore'

interface Props {
  variant: 'nurse' | 'patient'
}

export function LoginPage({ variant }: Props) {
  const [username, setUsername] = useState('hospital')
  const [password, setPassword] = useState('')
  const [error,    setError]    = useState<string | null>(null)
  const [loading,  setLoading]  = useState(false)
  const { setAuth } = useAuthStore()
  const navigate    = useNavigate()

  const title = variant === 'nurse' ? '간호사 로그인' : '환자 로그인'
  const hint  = variant === 'nurse' ? 'worker' : '침상 번호 (예: 11011)'

  async function handleSubmit(e: FormEvent) {
    e.preventDefault()
    setError(null)
    setLoading(true)
    try {
      const res = variant === 'nurse'
        ? await loginNurse(username, password)
        : await loginPatient(username, password)
      setAuth(res.access_token, res.role, res.bed_code ?? null)
      navigate(variant === 'nurse' ? '/nurse/dashboard' : '/patient', { replace: true })  // dashboard routes unchanged
    } catch (err: unknown) {
      setError(err instanceof Error ? err.message : '로그인 실패')
    } finally {
      setLoading(false)
    }
  }

  return (
    <div style={{
      minHeight: '100vh', display: 'flex', alignItems: 'center',
      justifyContent: 'center', background: '#f4f6f8',
    }}>
      <form onSubmit={handleSubmit} style={{
        background: '#fff', padding: '2.5rem 2rem', borderRadius: 10,
        boxShadow: '0 2px 12px rgba(0,0,0,0.1)', minWidth: 320,
      }}>
        <h2 style={{ margin: '0 0 1.5rem', textAlign: 'center' }}>
          🏥 {title}
        </h2>

        <label style={labelStyle}>
          아이디
          <input
            value={username}
            onChange={(e) => setUsername(e.target.value)}
            required
            style={inputStyle}
          />
        </label>

        <label style={labelStyle}>
          비밀번호 <span style={{ fontSize: '0.75rem', color: '#888' }}>({hint})</span>
          <input
            type="password"
            value={password}
            onChange={(e) => setPassword(e.target.value)}
            required
            style={inputStyle}
          />
        </label>

        {error && (
          <p style={{ color: '#c0392b', fontSize: '0.85rem', marginTop: 0 }}>{error}</p>
        )}

        <button
          type="submit"
          disabled={loading}
          style={{
            width: '100%', padding: '0.7rem', marginTop: '0.5rem',
            background: '#2980b9', color: '#fff', border: 'none',
            borderRadius: 6, fontSize: '1rem', cursor: 'pointer',
            opacity: loading ? 0.7 : 1,
          }}
        >
          {loading ? '로그인 중...' : '로그인'}
        </button>

        {variant === 'nurse' && (
          <p style={{ textAlign: 'center', marginTop: '1rem', fontSize: '0.85rem', color: '#888' }}>
            환자 로그인은{' '}
            <a href="/login/patient" style={{ color: '#2980b9' }}>여기</a>
          </p>
        )}
      </form>
    </div>
  )
}

const labelStyle: React.CSSProperties = {
  display: 'flex', flexDirection: 'column', marginBottom: '1rem',
  fontWeight: 600, fontSize: '0.9rem', color: '#333',
}
const inputStyle: React.CSSProperties = {
  marginTop: 4, padding: '0.5rem 0.7rem', border: '1px solid #ccc',
  borderRadius: 5, fontSize: '1rem',
}
