/**
 * Tablet Page — robot-mounted tablet UI at /tablet/:robotId
 *
 * No auth required. The tablet is robot-context based: it knows only
 * the robotId from the URL and all actions are gated on robot state.
 *
 * State → Action mapping:
 *   WAIT_UNLOCK              → Unlock button (POST /robot/lock-command UNLOCK)
 *   DELIVERY_OPEN_NUR/PAT    → Complete button (POST /robot/lock-command LOCK)
 *   LOW_BATTERY/ERROR/EMERG  → Warning screen, no action
 *   All other states         → State display only
 *
 * WebSocket events consumed locally (not via global useWebSocket hook):
 *   robot_state_update, robot_battery_update, robot_location_update
 *   task_status_update (to show active task info)
 *   lock_status_update (to drive the unlock loading state)
 */
import { useEffect, useRef, useState } from 'react'
import { useParams }                    from 'react-router-dom'
import { fetchRobotStatus, sendLockCommand } from '@/api/robot'
import { fetchOngoingTasks }            from '@/api/tasks'
import { ROBOT_STATE_LABELS, ROBOT_STATE_STYLE } from '@/constants/robotStateLabels'
import type { RobotState, RobotStatus, Task, WsMessage, WsLockStatusUpdate } from '@/types'

// ── Constants ─────────────────────────────────────────────────────────────────

type LockPhase = 'idle' | 'unlocking' | 'locking'

const WARNING_STATES = new Set<RobotState>(['LOW_BATTERY', 'ERROR', 'EMERGENCY'])
const OPEN_STATES    = new Set<RobotState>(['DELIVERY_OPEN_NUR', 'DELIVERY_OPEN_PAT'])
const RESET_STATES   = new Set<RobotState>(['IDLE', 'MOVING', 'COMPLETE'])

const WARNING_MSGS: Partial<Record<RobotState, string>> = {
  LOW_BATTERY: '배터리가 부족합니다. 충전 스테이션으로 복귀 중입니다.',
  ERROR:       'ERROR. 관리자에게 문의하세요.',
  EMERGENCY:   '비상 정지 상태입니다. 관리자의 해제를 기다리세요.',
}

const TASK_TYPE_LABELS: Record<string, string> = {
  clothes_refill:          '의류 채우기',
  kit_delivery:            '키트 배달',
  specimen_delivery:       '검체 배달',
  logistics_delivery:      '물류 배달',
  used_clothes_collection: '사용 의류 수거',
  battery_low:             '충전 복귀',
  emergency_call:          '비상 호출',
  patient_clothes_rental:  '환자 의류 대여',
  patient_clothes_return:  '환자 의류 반납',
}

const ACTIVE_STATUSES = new Set(['PENDING', 'DISPATCHED', 'IN_PROGRESS'])

const WS_URL = `${window.location.protocol === 'https:' ? 'wss' : 'ws'}://${window.location.host}/ws`
const RECONNECT_DELAY_MS = 3000

// ── Component ─────────────────────────────────────────────────────────────────

export function TabletPage() {
  const { robotId = 'AMR-001' } = useParams<{ robotId: string }>()

  const [robot,       setRobot]       = useState<RobotStatus | null>(null)
  const [activeTask,  setActiveTask]  = useState<Task | null>(null)
  const [lockPhase,   setLockPhase]   = useState<LockPhase>('idle')
  const [actionError, setActionError] = useState<string | null>(null)
  const [now,         setNow]         = useState<Date>(new Date())

  // Ref so WS closure can access latest robot DB id without re-subscribing
  const robotDbIdRef = useRef<number | null>(null)
  const wsRef        = useRef<WebSocket | null>(null)

  // ── Clock ────────────────────────────────────────────────────────────────
  useEffect(() => {
    const id = setInterval(() => setNow(new Date()), 1000)
    return () => clearInterval(id)
  }, [])

  // ── Initial REST hydration ────────────────────────────────────────────────
  useEffect(() => {
    fetchRobotStatus()
      .then((r) => {
        setRobot(r)
        robotDbIdRef.current = r.id
        return fetchOngoingTasks().then((tasks) => {
          setActiveTask(tasks.find((t) => t.assigned_robot_id === r.id) ?? null)
        })
      })
      .catch(console.error)
  }, [])

  // ── WebSocket ─────────────────────────────────────────────────────────────
  useEffect(() => {
    let cancelled = false

    function connect() {
      if (cancelled) return
      const ws = new WebSocket(WS_URL)
      wsRef.current = ws

      ws.onmessage = (e: MessageEvent) => {
        try {
          const msg = JSON.parse(e.data) as WsMessage

          switch (msg.event) {
            case 'robot_state_update':
            case 'robot_battery_update':
            case 'robot_location_update':
              setRobot((prev) => prev ? { ...prev, ...(msg.data as Partial<RobotStatus>) } : null)
              break

            case 'task_status_update': {
              const t = msg.data as Task
              if (t.assigned_robot_id === robotDbIdRef.current) {
                setActiveTask(ACTIVE_STATUSES.has(t.status) ? t : null)
              }
              break
            }

            case 'lock_status_update': {
              const d = msg.data as WsLockStatusUpdate
              if (d.robot_id !== robotId) break
              if (d.command === 'UNLOCK') {
                if (d.status === 'ACCEPTED' || d.status === 'OPENED') {
                  setLockPhase('unlocking')
                } else if (d.status === 'FAILED') {
                  setActionError('잠금 해제 실패. 다시 시도해주세요.')
                  setLockPhase('idle')
                }
              } else if (d.command === 'LOCK' && d.status === 'LOCKED') {
                setLockPhase('idle')
              }
              break
            }
          }
        } catch { /* ignore malformed messages */ }
      }

      ws.onclose = () => { if (!cancelled) setTimeout(connect, RECONNECT_DELAY_MS) }
      ws.onerror = () => ws.close()
    }

    connect()
    return () => {
      cancelled = true
      wsRef.current?.close()
    }
  }, [robotId])

  // ── Sync lockPhase with robot state transitions ───────────────────────────
  useEffect(() => {
    const state = robot?.current_state as RobotState | undefined
    if (!state) return
    if (OPEN_STATES.has(state)) {
      // Compartment opened → show Complete button
      setLockPhase((prev) => (prev === 'locking' ? 'locking' : 'idle'))
    }
    if (RESET_STATES.has(state)) {
      setLockPhase('idle')
      setActionError(null)
    }
  }, [robot?.current_state])

  // ── Actions ───────────────────────────────────────────────────────────────

  async function handleUnlock() {
    setActionError(null)
    setLockPhase('unlocking')
    try {
      await sendLockCommand(robotId, 'UNLOCK')
    } catch (err: unknown) {
      setActionError(err instanceof Error ? err.message : '잠금 해제 요청 실패')
      setLockPhase('idle')
    }
  }

  async function handleComplete() {
    setActionError(null)
    setLockPhase('locking')
    try {
      await sendLockCommand(robotId, 'LOCK')
    } catch (err: unknown) {
      setActionError(err instanceof Error ? err.message : '잠금 요청 실패')
      setLockPhase('idle')
    }
  }

  // ── Derived display values ────────────────────────────────────────────────

  const state      = (robot?.current_state ?? 'IDLE') as RobotState
  const stateStyle = ROBOT_STATE_STYLE[state] ?? ROBOT_STATE_STYLE.IDLE
  const stateLabel = ROBOT_STATE_LABELS[state] ?? state
  const isWarning  = WARNING_STATES.has(state)
  const isOpen     = OPEN_STATES.has(state)
  const isWaiting  = state === 'WAIT_UNLOCK'

  // ── Render ────────────────────────────────────────────────────────────────

  return (
    <div style={{ fontFamily: 'sans-serif', minHeight: '100vh', background: '#12141a', color: '#eee', display: 'flex', flexDirection: 'column' }}>

      {/* Header */}
      <header style={{
        background: 'rgba(0,0,0,0.45)', padding: '0.8rem 1.5rem',
        display: 'flex', justifyContent: 'space-between', alignItems: 'center',
        borderBottom: '1px solid rgba(255,255,255,0.08)',
      }}>
        <span style={{ fontSize: '1.15rem', fontWeight: 700, letterSpacing: '0.03em' }}>
          🤖 {robotId}
        </span>
        <div style={{ display: 'flex', gap: '1.5rem', fontSize: '1rem', alignItems: 'center' }}>
          <BatteryLabel percent={robot?.battery_percent ?? null} />
          <span style={{ color: 'rgba(255,255,255,0.5)', fontVariantNumeric: 'tabular-nums' }}>
            {now.toLocaleTimeString('ko-KR')}
          </span>
        </div>
      </header>

      {/* State Panel */}
      <section style={{
        background: stateStyle.bg, color: stateStyle.color,
        padding: '2.5rem 2rem', textAlign: 'center',
        transition: 'background 0.4s ease',
      }}>
        <p style={{ fontSize: 'clamp(1.8rem, 5vw, 3rem)', fontWeight: 800, margin: 0, lineHeight: 1.3 }}>
          {stateLabel}
        </p>
        <p style={{ fontSize: '0.85rem', opacity: 0.65, marginTop: 8, fontFamily: 'monospace' }}>
          {state}
        </p>
        {robot?.location_display && (
          <p style={{ fontSize: '1rem', opacity: 0.8, marginTop: 4 }}>
            📍 {robot.location_display}
          </p>
        )}
      </section>

      {/* Active Task Strip */}
      {activeTask && (
        <section style={{
          background: 'rgba(255,255,255,0.04)', padding: '0.9rem 1.5rem',
          borderBottom: '1px solid rgba(255,255,255,0.07)',
        }}>
          <p style={{ margin: '0 0 3px', fontSize: '0.78rem', color: 'rgba(255,255,255,0.4)', textTransform: 'uppercase', letterSpacing: '0.06em' }}>
            현재 작업
          </p>
          <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
            <span style={{ fontSize: '1rem', fontWeight: 600 }}>
              {TASK_TYPE_LABELS[activeTask.task_type] ?? activeTask.task_type}
            </span>
            <span style={{
              fontSize: '0.75rem', padding: '2px 8px', borderRadius: 4,
              background: 'rgba(255,255,255,0.1)', color: 'rgba(255,255,255,0.65)',
            }}>
              {activeTask.status}
            </span>
          </div>
          {activeTask.destination_location && (
            <p style={{ margin: '3px 0 0', fontSize: '0.85rem', color: 'rgba(255,255,255,0.4)' }}>
              → {activeTask.destination_location.display_name}
            </p>
          )}
        </section>
      )}

      {/* Main Action Area */}
      <main style={{
        flex: 1, display: 'flex', flexDirection: 'column',
        alignItems: 'center', justifyContent: 'center',
        padding: '2.5rem 2rem', gap: '1.5rem',
      }}>

        {/* Error banner */}
        {actionError && (
          <div style={{
            background: '#922b21', color: '#fff', padding: '0.8rem 1.2rem',
            borderRadius: 8, fontSize: '0.95rem', maxWidth: 380, textAlign: 'center',
            display: 'flex', alignItems: 'center', gap: 10,
          }}>
            <span style={{ flex: 1 }}>{actionError}</span>
            <button
              onClick={() => setActionError(null)}
              style={{ background: 'none', border: 'none', color: '#fff', fontSize: '1.1rem', cursor: 'pointer', lineHeight: 1 }}
            >
              ×
            </button>
          </div>
        )}

        {/* ─── Warning state ─── */}
        {isWarning && (
          <div style={{ textAlign: 'center', maxWidth: 400 }}>
            <div style={{ fontSize: '4.5rem', marginBottom: '1rem' }}>⚠️</div>
            <p style={{ fontSize: '1.4rem', fontWeight: 700, color: '#e74c3c', marginBottom: '0.5rem' }}>
              {stateLabel}
            </p>
            <p style={{ color: 'rgba(255,255,255,0.55)', fontSize: '1rem', lineHeight: 1.6 }}>
              {WARNING_MSGS[state] ?? '관리자에게 문의하세요'}
            </p>
          </div>
        )}

        {/* ─── WAIT_UNLOCK: Unlock button ─── */}
        {isWaiting && !isWarning && lockPhase === 'idle' && (
          <div style={{ textAlign: 'center' }}>
            <p style={{ color: 'rgba(255,255,255,0.55)', marginBottom: '2rem', fontSize: '1.05rem' }}>
              작업함 잠금을 해제하려면 아래 버튼을 누르세요
            </p>
            <button onClick={handleUnlock} style={unlockBtnStyle}>
              🔓 잠금 해제
            </button>
          </div>
        )}

        {/* ─── Unlocking loading ─── */}
        {isWaiting && !isWarning && lockPhase === 'unlocking' && (
          <div style={{ textAlign: 'center', color: 'rgba(255,255,255,0.7)' }}>
            <div style={{ fontSize: '3.5rem', marginBottom: '1rem' }}>⏳</div>
            <p style={{ fontSize: '1.3rem', fontWeight: 600 }}>잠금 해제 중...</p>
            <p style={{ fontSize: '0.9rem', color: 'rgba(255,255,255,0.4)', marginTop: 6 }}>
              로봇의 응답을 기다리고 있습니다
            </p>
          </div>
        )}

        {/* ─── DELIVERY_OPEN: Complete button ─── */}
        {isOpen && !isWarning && (
          <div style={{ textAlign: 'center' }}>
            <div style={{ fontSize: '3rem', marginBottom: '1rem' }}>📦</div>
            <p style={{ color: 'rgba(255,255,255,0.7)', marginBottom: '0.5rem', fontSize: '1.1rem', fontWeight: 600 }}>
              작업함이 열렸습니다
            </p>
            <p style={{ color: 'rgba(255,255,255,0.45)', marginBottom: '2rem', fontSize: '0.95rem' }}>
              {state === 'DELIVERY_OPEN_PAT'
                ? '환자가 의류를 수령한 후 완료 버튼을 누르세요'
                : '물품 작업을 완료한 후 완료 버튼을 누르세요'}
            </p>
            <button
              onClick={handleComplete}
              disabled={lockPhase === 'locking'}
              style={{ ...completeBtnStyle, opacity: lockPhase === 'locking' ? 0.55 : 1 }}
            >
              {lockPhase === 'locking' ? '처리 중...' : '✅ 작업 완료'}
            </button>
          </div>
        )}

        {/* ─── All other states (no action) ─── */}
        {!isWarning && !isOpen && !isWaiting && (
          <p style={{ color: 'rgba(255,255,255,0.25)', fontSize: '0.9rem' }}>
            로봇이 작업 중입니다
          </p>
        )}

      </main>
    </div>
  )
}

// ── Small helpers ─────────────────────────────────────────────────────────────

function BatteryLabel({ percent }: { percent: number | null }) {
  if (percent === null) return <span>🔋 —%</span>
  const color = percent <= 20 ? '#e74c3c' : percent <= 40 ? '#e67e22' : '#2ecc71'
  return <span style={{ color }}>🔋 {percent.toFixed(0)}%</span>
}

// ── Shared button styles ──────────────────────────────────────────────────────

const unlockBtnStyle: React.CSSProperties = {
  background: '#e67e22',
  color: '#fff',
  border: 'none',
  borderRadius: 14,
  padding: '1.4rem 3.5rem',
  fontSize: '1.5rem',
  fontWeight: 800,
  cursor: 'pointer',
  boxShadow: '0 6px 20px rgba(230,126,34,0.45)',
  letterSpacing: '0.02em',
}

const completeBtnStyle: React.CSSProperties = {
  background: '#27ae60',
  color: '#fff',
  border: 'none',
  borderRadius: 14,
  padding: '1.4rem 3.5rem',
  fontSize: '1.5rem',
  fontWeight: 800,
  cursor: 'pointer',
  boxShadow: '0 6px 20px rgba(39,174,96,0.45)',
  letterSpacing: '0.02em',
}
