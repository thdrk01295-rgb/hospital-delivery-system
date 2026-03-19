/**
 * App — route structure + protected routing + global WebSocket connection.
 *
 * Route map:
 *   /                        → redirect to /login/nurse
 *   /login/nurse             → LoginPage (nurse variant)
 *   /login/patient           → LoginPage (patient variant)
 *   /nurse/dashboard         → NurseDashboard    [nurse only]
 *   /nurse/robot             → RobotDashboard    [nurse only]
 *   /nurse/orders/new        → OrderCreate        [nurse only]
 *   /patient                 → PatientRequestPage [patient only]
 *
 * WebSocket:
 *   useWebSocket is mounted here once the user is logged in.
 *   It broadcasts events to Zustand stores consumed by all pages.
 *   The PatientScreenController (inside PatientRequestPage) applies Rule 0:
 *   robot state events are stored but only affect the patient screen when
 *   an active patient task exists.
 */
import { Routes, Route, Navigate } from 'react-router-dom'
import { useAuthStore }             from '@/store/authStore'
import { useWebSocket }             from '@/hooks/useWebSocket'
import { ProtectedRoute }           from '@/components/ProtectedRoute'
import { LoginPage }                from '@/pages/LoginPage'
import { NurseDashboard }           from '@/pages/nurse/NurseDashboard'
import { RobotDashboard }           from '@/pages/nurse/RobotDashboard'
import { OrderCreate }              from '@/pages/nurse/OrderCreate'
import { PatientRequestPage }       from '@/pages/patient/PatientRequestPage'

export default function App() {
  const { token } = useAuthStore()

  // Connect WebSocket for all authenticated users
  useWebSocket(Boolean(token))

  return (
    <Routes>
      {/* Root redirect */}
      <Route path="/" element={<Navigate to="/login/nurse" replace />} />

      {/* Auth */}
      <Route path="/login/nurse"   element={<LoginPage variant="nurse"   />} />
      <Route path="/login/patient" element={<LoginPage variant="patient" />} />

      {/* Nurse pages */}
      <Route
        path="/nurse/dashboard"
        element={<ProtectedRoute role="nurse"><NurseDashboard /></ProtectedRoute>}
      />
      <Route
        path="/nurse/robot"
        element={<ProtectedRoute role="nurse"><RobotDashboard /></ProtectedRoute>}
      />
      <Route
        path="/nurse/orders/new"
        element={<ProtectedRoute role="nurse"><OrderCreate /></ProtectedRoute>}
      />

      {/* Patient page */}
      <Route
        path="/patient"
        element={<ProtectedRoute role="patient"><PatientRequestPage /></ProtectedRoute>}
      />

      {/* Catch-all */}
      <Route path="*" element={<Navigate to="/" replace />} />
    </Routes>
  )
}
