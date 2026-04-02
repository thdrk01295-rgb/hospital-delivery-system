/**
 * Patient Request Page
 * ─────────────────────
 * Mount:
 *   GET /robot/status   → robotStore
 *   GET /tasks/patient/me → taskStore.patientActiveTask
 *     Identity is resolved from JWT on the server; no locationId sent by client.
 *
 * Live updates via WebSocket (managed by App.tsx):
 *   robot_state_update → robotStore → triggers screen re-evaluation
 *   task_status_update → taskStore  → triggers screen re-evaluation
 *   Robot state only affects this page when an active task exists (Rule 0).
 *
 * Screen mode is derived via resolvePatientScreenMode() on every render.
 */
import { useEffect, useState } from 'react'
import { useNavigate }          from 'react-router-dom'
import { fetchRobotStatus }     from '@/api/robot'
import { fetchPatientActiveTask, submitPatientClothingRequest, completeTask, cancelPatientTask } from '@/api/tasks'
import { useRobotStore }        from '@/store/robotStore'
import { useTaskStore }         from '@/store/taskStore'
import { useAuthStore }         from '@/store/authStore'
import { resolvePatientScreenMode } from './PatientScreenController'
import type { PatientClothingRequestCreate, RobotState } from '@/types'

// ── Sub-screens ───────────────────────────────────────────────────────────────

function ServiceSelection({ onRental, onReturn, loading }: {
  onRental: () => void
  onReturn: () => void
  loading: boolean
}) {
  return (
    <div style={centerBox}>
      <h2 style={{ marginBottom: '2rem' }}>의류 서비스를 선택하세요</h2>
      <div style={{ display: 'flex', gap: '1.5rem', flexWrap: 'wrap', justifyContent: 'center' }}>
        <ServiceButton label="옷 대여 신청" sublabel="Rental" color="#2980b9" onClick={onRental} disabled={loading} />
        <ServiceButton label="옷 반납 신청" sublabel="Return"  color="#27ae60" onClick={onReturn} disabled={loading} />
      </div>
    </div>
  )
}

function ServiceButton({ label, sublabel, color, onClick, disabled }: {
  label: string; sublabel: string; color: string; onClick: () => void; disabled: boolean
}) {
  return (
    <button
      onClick={onClick}
      disabled={disabled}
      style={{
        width: 180, height: 180, background: color, color: '#fff', border: 'none',
        borderRadius: 16, fontSize: '1.3rem', fontWeight: 700, cursor: 'pointer',
        display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center',
        gap: 8, opacity: disabled ? 0.6 : 1, boxShadow: '0 4px 12px rgba(0,0,0,0.15)',
      }}
    >
      <span>{label}</span>
      <span style={{ fontSize: '0.8rem', opacity: 0.85 }}>{sublabel}</span>
    </button>
  )
}

// Item selection shown after rental/return button pressed (before submission)
function ItemSelectForm({ mode, onSubmit, onCancel, submitting }: {
  mode: 'rental' | 'return'
  onSubmit: (top: boolean, bottom: boolean) => void
  onCancel: () => void
  submitting: boolean
}) {
  const [top,    setTop]    = useState(false)
  const [bottom, setBottom] = useState(false)

  const isRental = mode === 'rental'
  const title = isRental ? '의류 대여 신청' : '의류 반납 신청'
  const submitLabel = isRental ? '대여 신청' : '반납 신청'

  const selStyle = (active: boolean): React.CSSProperties => ({
    width: 130, height: 100, borderRadius: 12,
    border: active ? '3px solid #2980b9' : '2px solid #ccc',
    background: active ? '#eaf4fb' : '#f9f9f9', cursor: 'pointer',
    display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center',
    fontWeight: 700, fontSize: '1rem', gap: 4,
    color: active ? '#2980b9' : '#555', transition: 'all 0.15s',
  })

  return (
    <div style={centerBox}>
      <h2 style={{ marginBottom: '1.5rem' }}>{title}</h2>
      <div style={{ display: 'flex', gap: '1.5rem', marginBottom: '2rem' }}>
        <div style={selStyle(top)} onClick={() => setTop(!top)}>
          <span style={{ fontSize: '1.8rem' }}>👕</span>
          <span>상의</span>
          {top && <span style={{ fontSize: '0.75rem' }}>선택됨</span>}
        </div>
        <div style={selStyle(bottom)} onClick={() => setBottom(!bottom)}>
          <span style={{ fontSize: '1.8rem' }}>👖</span>
          <span>하의</span>
          {bottom && <span style={{ fontSize: '0.75rem' }}>선택됨</span>}
        </div>
      </div>
      <div style={{ display: 'flex', gap: 12 }}>
        <button onClick={onCancel} style={cancelBtn}>취소</button>
        <button
          onClick={() => onSubmit(top, bottom)}
          disabled={submitting || (!top && !bottom)}
          style={{ ...primaryBtn, opacity: (submitting || (!top && !bottom)) ? 0.5 : 1 }}
        >
          {submitting ? '신청 중...' : submitLabel}
        </button>
      </div>
    </div>
  )
}

function ConfirmScreen({ title }: { title: string }) {
  const [cancelling, setCancelling] = useState(false)
  const setPatientActiveTask = useTaskStore((s) => s.setPatientActiveTask)

  async function handleCancel() {
    setCancelling(true)
    try {
      await cancelPatientTask()
      setPatientActiveTask(null)
    } catch (err: unknown) {
      alert(err instanceof Error ? err.message : '취소 실패')
      setCancelling(false)
    }
  }

  return (
    <div style={centerBox}>
      <div style={{ fontSize: '4rem', marginBottom: '1rem' }}>🤖</div>
      <h2>{title}</h2>
      <p style={{ color: '#666', marginTop: 8 }}>
        로봇이 곧 도착합니다. 가급적 자리에서 기다려 주세요. 사정에 따라 시간이 소요될 수 있습니다.
      </p>
      <button
        onClick={handleCancel}
        disabled={cancelling}
        style={{ ...cancelBtn, marginTop: '1.5rem', opacity: cancelling ? 0.6 : 1 }}
      >
        {cancelling ? '취소 중...' : '신청 취소'}
      </button>
    </div>
  )
}

function RobotArrivedScreen() {
  return (
    <div style={{ ...centerBox, background: '#eafaf1' }}>
      <div style={{ fontSize: '4rem', marginBottom: '1rem' }}>✅</div>
      <h2 style={{ color: '#27ae60' }}>도착했습니다</h2>
      <p style={{ color: '#555' }}>잠시만 기다려 주세요</p>
    </div>
  )
}

function DeliveryActionScreen({ taskId }: { taskId: number }) {
  const [completing, setCompleting] = useState(false)
  const setPatientActiveTask = useTaskStore((s) => s.setPatientActiveTask)

  async function handleComplete() {
    setCompleting(true)
    try {
      const updated = await completeTask(taskId)
      setPatientActiveTask(updated)
    } catch (err: unknown) {
      alert(err instanceof Error ? err.message : '완료 처리 실패')
      setCompleting(false)
    }
  }

  return (
    <div style={{ ...centerBox, background: '#e8f8f5' }}>
      <div style={{ fontSize: '3rem', marginBottom: '1rem' }}>📦</div>
      <h2 style={{ color: '#16a085', marginBottom: '0.5rem' }}>
        의류 작업을 완료 후 완료 버튼을 눌러주세요
      </h2>
      <p style={{ color: '#555', marginBottom: '2rem' }}>작업을 마친 후 아래 완료 버튼을 눌러주세요.</p>
      <button
        onClick={handleComplete}
        disabled={completing}
        style={{ ...primaryBtn, fontSize: '1.3rem', padding: '1rem 3rem', opacity: completing ? 0.7 : 1 }}
      >
        {completing ? '처리 중...' : '완료'}
      </button>
    </div>
  )
}

function CompleteScreen() {
  return (
    <div style={{ ...centerBox, background: '#eafaf1' }}>
      <div style={{ fontSize: '4rem', marginBottom: '1rem' }}>🎉</div>
      <h2 style={{ color: '#27ae60' }}>처리 완료</h2>
      <p style={{ color: '#666' }}>잠시 후 초기 화면으로 돌아갑니다</p>
    </div>
  )
}

function RobotStatusScreen({ state }: { state: RobotState }) {
  const MSGS: Partial<Record<RobotState, string>> = {
    ERROR:           'ERROR. 관리자에게 문의하세요',
    LOW_BATTERY:     '충전이 필요합니다',
    CHAGING_BATTERY: '충전중',
  }
  return (
    <div style={{ ...centerBox, background: '#fdedec' }}>
      <div style={{ fontSize: '3rem', marginBottom: '1rem' }}>⚠️</div>
      <h2 style={{ color: '#c0392b' }}>{MSGS[state] ?? state}</h2>
    </div>
  )
}

// ── Main page component ───────────────────────────────────────────────────────

type ItemFormMode = 'rental' | 'return' | null

export function PatientRequestPage() {
  const navigate   = useNavigate()
  const { logout, bedCode } = useAuthStore()
  const { robot, setRobot } = useRobotStore()
  const { patientActiveTask, setPatientActiveTask } = useTaskStore()

  const [itemFormMode, setItemFormMode] = useState<ItemFormMode>(null)
  const [submitting,   setSubmitting]   = useState(false)

  // Initial REST hydration on mount
  useEffect(() => {
    Promise.all([
      fetchRobotStatus(),
      fetchPatientActiveTask(),
    ]).then(([robotData, task]) => {
      setRobot(robotData)
      setPatientActiveTask(task)
    }).catch(console.error)
  }, [])

  // Auto-reset COMPLETE → SERVICE_SELECTION after 3 s
  const screenMode = resolvePatientScreenMode(
    patientActiveTask,
    (robot?.current_state ?? null) as RobotState | null,
  )
  useEffect(() => {
    if (screenMode !== 'COMPLETE') return
    const id = setTimeout(() => setPatientActiveTask(null), 3000)
    return () => clearTimeout(id)
  }, [screenMode])

  async function submitRequest(type: 'patient_clothes_rental' | 'patient_clothes_return', top: boolean, bottom: boolean) {
    setSubmitting(true)
    const body: PatientClothingRequestCreate = {
      task_type:    type,
      order_top:    top    ? 1 : undefined,
      order_bottom: bottom ? 1 : undefined,
    }
    try {
      const task = await submitPatientClothingRequest(body)
      setPatientActiveTask(task)
      setItemFormMode(null)
    } catch (err: unknown) {
      alert(err instanceof Error ? err.message : '신청 실패')
    } finally {
      setSubmitting(false)
    }
  }

  function handleLogout() {
    logout()
    navigate('/login/patient', { replace: true })
  }

  // ── Render screen based on mode ──────────────────────────────────────────
  let screen: React.ReactNode

  if (itemFormMode === 'rental') {
    screen = (
      <ItemSelectForm
        mode="rental"
        onSubmit={(t, b) => submitRequest('patient_clothes_rental', t, b)}
        onCancel={() => setItemFormMode(null)}
        submitting={submitting}
      />
    )
  } else if (itemFormMode === 'return') {
    screen = (
      <ItemSelectForm
        mode="return"
        onSubmit={(t, b) => submitRequest('patient_clothes_return', t, b)}
        onCancel={() => setItemFormMode(null)}
        submitting={submitting}
      />
    )
  } else {
    switch (screenMode) {
      case 'SERVICE_SELECTION':
        screen = (
          <ServiceSelection
            onRental={() => setItemFormMode('rental')}
            onReturn={() => setItemFormMode('return')}
            loading={submitting}
          />
        )
        break
      case 'RENTAL_CONFIRM':
        screen = <ConfirmScreen title="대여 신청 접수됨" />
        break
      case 'RETURN_CONFIRM':
        screen = <ConfirmScreen title="반납 신청 접수됨" />
        break
      case 'ROBOT_ARRIVED':
        screen = <RobotArrivedScreen />
        break
      case 'DELIVERY_ACTION':
        screen = <DeliveryActionScreen taskId={patientActiveTask!.id} />
        break
      case 'COMPLETE':
        screen = <CompleteScreen />
        break
      case 'ROBOT_STATUS':
        screen = <RobotStatusScreen state={robot!.current_state} />
        break
    }
  }

  return (
    <div style={{ fontFamily: 'sans-serif', minHeight: '100vh', background: '#f0f4f8', display: 'flex', flexDirection: 'column' }}>
      <header style={{ background: '#2c3e50', color: '#fff', padding: '0.7rem 1.2rem', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <strong>🏥 환자 서비스</strong>
        <div style={{ display: 'flex', gap: 12, alignItems: 'center', fontSize: '0.9rem' }}>
          {bedCode && <span>침상: {bedCode}</span>}
          <button
            onClick={handleLogout}
            style={{ background: 'transparent', border: '1px solid #e74c3c', color: '#e74c3c', borderRadius: 4, padding: '0.2rem 0.7rem', cursor: 'pointer' }}
          >
            로그아웃
          </button>
        </div>
      </header>

      <main style={{ flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
        {screen}
      </main>
    </div>
  )
}

// ── Shared styles ─────────────────────────────────────────────────────────────

const centerBox: React.CSSProperties = {
  display: 'flex', flexDirection: 'column', alignItems: 'center',
  justifyContent: 'center', textAlign: 'center', padding: '3rem 2rem',
  borderRadius: 16, minWidth: 320,
}

const primaryBtn: React.CSSProperties = {
  background: '#2980b9', color: '#fff', border: 'none', borderRadius: 8,
  padding: '0.8rem 2rem', fontSize: '1rem', fontWeight: 700, cursor: 'pointer',
}

const cancelBtn: React.CSSProperties = {
  background: '#ecf0f1', color: '#555', border: '1px solid #ccc', borderRadius: 8,
  padding: '0.8rem 2rem', fontSize: '1rem', cursor: 'pointer',
}
