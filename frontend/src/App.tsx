/**
 * App — route structure + protected routing + global WebSocket connection.
 *
 * Route map:
 *   /                        → redirect to /login/nurse
 *   /login/nurse             → LoginPage (nurse variant)
 *   /login/patient           → LoginPage (patient variant)
 *   /nurse/dashboard         → NurseDashboard    [nurse only]
 *   /nurse/orders/new        → OrderCreate        [nurse only]
 *   /patient                 → PatientRequestPage [patient only]
 *   /tablet/:robotId         → TabletPage         (no auth)
 *
 * WebSocket:
 *   useWebSocket is mounted here once either role is authenticated.
 *   It broadcasts events to Zustand stores consumed by all pages.
 */
import { Routes, Route, Navigate }       from 'react-router-dom'
import { useNurseAuthStore }              from '@/store/nurseAuthStore'
import { usePatientAuthStore }            from '@/store/patientAuthStore'
import { useWebSocket }                   from '@/hooks/useWebSocket'
import { NurseProtectedRoute, PatientProtectedRoute } from '@/components/ProtectedRoute'
import { LoginPage }                      from '@/pages/LoginPage'
import { NurseDashboard }                 from '@/pages/nurse/NurseDashboard'
import { OrderCreate }                    from '@/pages/nurse/OrderCreate'
import { PatientRequestPage }             from '@/pages/patient/PatientRequestPage'
import { TabletPage }                     from '@/pages/tablet/TabletPage'

export default function App() {
  const nurseToken   = useNurseAuthStore((s) => s.token)
  const patientToken = usePatientAuthStore((s) => s.token)

  // Open WebSocket whenever at least one role is authenticated in this tab.
  useWebSocket(Boolean(nurseToken || patientToken))

  return (
    <Routes>
      <Route path="/" element={<Navigate to="/login/nurse" replace />} />

      <Route path="/login/nurse"   element={<LoginPage variant="nurse"   />} />
      <Route path="/login/patient" element={<LoginPage variant="patient" />} />

      <Route
        path="/nurse/dashboard"
        element={<NurseProtectedRoute><NurseDashboard /></NurseProtectedRoute>}
      />
      <Route
        path="/nurse/orders/new"
        element={<NurseProtectedRoute><OrderCreate /></NurseProtectedRoute>}
      />

      <Route
        path="/patient"
        element={<PatientProtectedRoute><PatientRequestPage /></PatientProtectedRoute>}
      />

      <Route path="/tablet/:robotId" element={<TabletPage />} />

      <Route path="*" element={<Navigate to="/" replace />} />
    </Routes>
  )
}
