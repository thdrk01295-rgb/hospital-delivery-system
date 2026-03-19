import { Navigate } from 'react-router-dom'
import { useAuthStore } from '@/store/authStore'
import type { UserRole } from '@/types'

interface Props {
  children: React.ReactNode
  role: UserRole
}

export function ProtectedRoute({ children, role }: Props) {
  const { token, role: userRole } = useAuthStore()

  if (!token || userRole !== role) {
    return <Navigate to={role === 'nurse' ? '/login/nurse' : '/login/patient'} replace />
  }

  return <>{children}</>
}
