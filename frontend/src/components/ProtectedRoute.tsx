import { Navigate } from 'react-router-dom'
import { useNurseAuthStore }   from '@/store/nurseAuthStore'
import { usePatientAuthStore } from '@/store/patientAuthStore'

interface Props {
  children: React.ReactNode
}

export function NurseProtectedRoute({ children }: Props) {
  const token = useNurseAuthStore((s) => s.token)
  if (!token) return <Navigate to="/login/nurse" replace />
  return <>{children}</>
}

export function PatientProtectedRoute({ children }: Props) {
  const token = usePatientAuthStore((s) => s.token)
  if (!token) return <Navigate to="/login/patient" replace />
  return <>{children}</>
}
