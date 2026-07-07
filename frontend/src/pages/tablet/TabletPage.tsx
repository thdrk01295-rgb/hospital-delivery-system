/**
 * Tablet Page — robot-mounted tablet UI at /tablet/:robotId
 *
 * No auth required. All actions are gated on robot state received via WebSocket.
 *
 * v6 Route-stage model with robot face UI:
 *   currentStop="origin"      → Complete signals MOVE_TO_DESTINATION, keeps task active
 *   currentStop="destination" → Complete signals FINISH_TASK, marks task COMPLETE
 *
 *   Lock phase:
 *     null/WAITING_UNLOCK → toggle="잠금해제", complete disabled
 *     OPENED              → toggle="잠금", complete disabled + NEED_LOCK guard if pressed
 *     RELOCKED            → toggle="잠금해제", complete enabled
 *
 * UI override states (frontend-only, not RobotState):
 *   NEED_LOCK   → user pressed Complete while compartment open; need_lock_face.png, auto-clears 2.5 s
 *   LOCK_FAILED → robot/lock_status arrived with status=FAILED; error_face.png
 */
import { useEffect, useRef, useState } from 'react'
import { useParams }                    from 'react-router-dom'

// ── Robot face assets ─────────────────────────────────────────────────────────
// TabletPage is at src/pages/tablet/ → ../../assets/robot-faces/
import idleFace        from '../../assets/robot-faces/idle_face.png'
import movingFace      from '../../assets/robot-faces/moving_face.png'
import arrivedFace     from '../../assets/robot-faces/arrived_face.png'
import taskFace        from '../../assets/robot-faces/task_face.png'
import completeFace    from '../../assets/robot-faces/complete_face.png'
import errorFace       from '../../assets/robot-faces/error_face.png'
import needLockFace    from '../../assets/robot-faces/need_lock_face.png'
import lowBatteryFace  from '../../assets/robot-faces/low_battery_face.png'
import chargingFace    from '../../assets/robot-faces/charging_face.png'
import emergencyFace   from '../../assets/robot-faces/emergency_face.png'

import { fetchRobotStatus, sendLockCommand, completeRobotTask, type CompleteTaskResult } from '@/api/robot'
import { fetchOngoingTasks }            from '@/api/tasks'
import type { RobotState, RobotStatus, Task, WsMessage, WsLockStatusUpdate } from '@/types'

// ── Constants ─────────────────────────────────────────────────────────────────

type TaskLockPhase  = 'WAITING_UNLOCK' | 'OPENED' | 'RELOCKED'
type UiOverride     = 'NEED_LOCK' | 'LOCK_FAILED'

// States where compartment interaction buttons (toggle + complete) are rendered
const INTERACTION_STATES = new Set<RobotState>(['WAIT_UNLOCK', 'DELIVERY_OPEN_NUR', 'DELIVERY_OPEN_PAT'])

// Arriving at these states resets lock phase
const RESET_STATES = new Set<RobotState>(['IDLE', 'MOVING', 'COMPLETE'])

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

// ── State → face / message mappings ──────────────────────────────────────────

const FACE_BY_STATE: Partial<Record<RobotState, string>> = {
  IDLE:              idleFace,
  MOVING:            movingFace,
  ARRIVED:           arrivedFace,
  WAIT_UNLOCK:       taskFace,
  AUTH_SUCCESS:      taskFace,
  AUTH_FAIL:         errorFace,
  DELIVERY_OPEN_NUR: taskFace,
  DELIVERY_OPEN_PAT: taskFace,
  COMPLETE:          completeFace,
  LOW_BATTERY:       lowBatteryFace,
  CHARGING_BATTERY:  chargingFace,
  ERROR:             errorFace,
  EMERGENCY:         emergencyFace,
}

const MSG_BY_STATE: Partial<Record<RobotState, string>> = {
  IDLE:              '대기 중입니다',
  MOVING:            '이동 중입니다',
  ARRIVED:           '도착했습니다',
  WAIT_UNLOCK:       '작업을 수행하려면 잠금해제를 눌러주세요',
  AUTH_SUCCESS:      '인증되었습니다',
  AUTH_FAIL:         '인증에 실패했습니다',
  DELIVERY_OPEN_NUR: '물품을 넣거나 꺼내주세요',
  DELIVERY_OPEN_PAT: '의류 업무를 완료해주세요',
  COMPLETE:          '완료되었습니다',
  LOW_BATTERY:       '충전이 필요합니다',
  CHARGING_BATTERY:  '충전 중입니다',
  ERROR:             '오류가 발생했습니다',
  EMERGENCY:         '비상 정지 상태입니다',
}

const FACE_BY_OVERRIDE: Record<UiOverride, string> = {
  NEED_LOCK:   needLockFace,
  LOCK_FAILED: errorFace,
}

const MSG_BY_OVERRIDE: Record<UiOverride, string> = {
  NEED_LOCK:   '먼저 수납함을 잠가주세요',
  LOCK_FAILED: '잠금 처리에 실패했습니다. 다시 시도해주세요',
}

const ACTIVE_STATUSES = new Set(['PENDING', 'DISPATCHED', 'IN_PROGRESS'])
const WS_URL = `${window.location.protocol === 'https:' ? 'wss' : 'ws'}://${window.location.host}/ws`
const RECONNECT_DELAY_MS = 3000
const NEED_LOCK_AUTO_CLEAR_MS = 2500

// ── Component ─────────────────────────────────────────────────────────────────

export function TabletPage() {
  const { robotId = 'AMR-001' } = useParams<{ robotId: string }>()

  const [robot,             setRobot]             = useState<RobotStatus | null>(null)
  const [activeTask,        setActiveTask]         = useState<Task | null>(null)
  const [taskLockPhase,     setTaskLockPhase]      = useState<TaskLockPhase | null>(null)
  const [currentStop,       setCurrentStop]        = useState<'origin' | 'destination'>('origin')
  const [uiOverride,        setUiOverride]         = useState<UiOverride | null>(null)
  const [lockActionPending, setLockActionPending]  = useState(false)
  const [apiError,          setApiError]           = useState<string | null>(null)
  const [now,               setNow]               = useState<Date>(new Date())

  const robotDbIdRef       = useRef<number | null>(null)
  const wsRef              = useRef<WebSocket | null>(null)
  const needLockTimerRef   = useRef<ReturnType<typeof setTimeout> | null>(null)

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
            case 'robot_location_update': {
              const update = msg.data as Partial<RobotStatus>
              setRobot((prev) => prev ? { ...prev, ...update } : null)
              if (msg.event === 'robot_state_update' && update.current_state) {
                const s = update.current_state as RobotState
                if (s === 'WAIT_UNLOCK') {
                  setTaskLockPhase('WAITING_UNLOCK')
                  setLockActionPending(false)
                  setUiOverride(null)
                  setApiError(null)
                } else if (RESET_STATES.has(s)) {
                  setTaskLockPhase(null)
                  setLockActionPending(false)
                  setUiOverride(null)
                  setApiError(null)
                }
              }
              break
            }

            case 'task_status_update': {
              const t = msg.data as Task
              if (t.assigned_robot_id === robotDbIdRef.current) {
                const isActive = ACTIVE_STATUSES.has(t.status)
                setActiveTask(isActive ? t : null)
                if (!isActive) {
                  setCurrentStop('origin')
                }
              }
              break
            }

            case 'task_route_stage_update': {
              const d = msg.data as { task_id: number; current_stop: 'origin' | 'destination' }
              setCurrentStop(d.current_stop)
              break
            }

            case 'lock_status_update': {
              const d = msg.data as WsLockStatusUpdate
              if (d.robot_id !== robotId) break

              // Update lock phase — primary path uses server's authoritative lock_phase;
              // defensive fallback derives from command+status when lock_phase is null.
              if (d.lock_phase === 'OPENED' || d.lock_phase === 'RELOCKED') {
                setTaskLockPhase(d.lock_phase)
              } else {
                if (d.command === 'UNLOCK' && d.status === 'OPENED') {
                  setTaskLockPhase('OPENED')
                } else if (d.command === 'LOCK' && d.status === 'LOCKED') {
                  setTaskLockPhase((prev) => (prev === 'OPENED' ? 'RELOCKED' : prev))
                }
              }

              // UI override: FAILED → LOCK_FAILED face; success → clear LOCK_FAILED
              if (d.status === 'FAILED') {
                setUiOverride('LOCK_FAILED')
              } else {
                setUiOverride((prev) => (prev === 'LOCK_FAILED' ? null : prev))
              }

              setLockActionPending(false)
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

  // ── Actions ───────────────────────────────────────────────────────────────

  async function handleToggle() {
    const command = taskLockPhase === 'OPENED' ? 'LOCK' : 'UNLOCK'
    setUiOverride(null)   // clear any lock-failure override when user retries
    setApiError(null)
    setLockActionPending(true)
    try {
      await sendLockCommand(robotId, command)
    } catch (err: unknown) {
      setApiError(err instanceof Error ? err.message : command === 'UNLOCK' ? '잠금 해제 요청 실패' : '잠금 요청 실패')
      setLockActionPending(false)
    }
  }

  async function handleComplete() {
    // Guard: do not complete while compartment is open
    if (taskLockPhase !== 'RELOCKED') {
      setUiOverride('NEED_LOCK')
      if (needLockTimerRef.current) clearTimeout(needLockTimerRef.current)
      needLockTimerRef.current = setTimeout(() => setUiOverride(null), NEED_LOCK_AUTO_CLEAR_MS)
      return
    }
    setApiError(null)
    setLockActionPending(true)
    try {
      const result: CompleteTaskResult = await completeRobotTask(robotId)
      if (!result.is_final) {
        // Origin stop done — reset for the destination stop immediately.
        setTaskLockPhase(null)
        setCurrentStop('destination')
        setLockActionPending(false)
      }
      // is_final=true: task_status_update WS event will clear activeTask naturally.
    } catch (err: unknown) {
      setApiError(err instanceof Error ? err.message : '작업 완료 처리 실패')
      setLockActionPending(false)
    }
  }

  // ── Derived display values ────────────────────────────────────────────────

  const state        = (robot?.current_state ?? 'IDLE') as RobotState
  const isUnlocked   = taskLockPhase === 'OPENED'
  const canComplete  = taskLockPhase === 'RELOCKED'
  const toggleLabel  = isUnlocked ? '잠금' : '잠금해제'
  const toggleIcon   = isUnlocked ? '🔒' : '🔓'
  const showButtons  = INTERACTION_STATES.has(state)

  const faceImage  = uiOverride ? FACE_BY_OVERRIDE[uiOverride] : (FACE_BY_STATE[state] ?? idleFace)
  const message    = uiOverride ? MSG_BY_OVERRIDE[uiOverride]  : (MSG_BY_STATE[state]  ?? '대기 중입니다')

  // ── Render ────────────────────────────────────────────────────────────────

  return (
    <div style={pageStyle}>

      {/* ── Header ──────────────────────────────────────────────────────── */}
      <header style={headerStyle}>
        <span style={{ fontWeight: 700, fontSize: '1.05rem', color: '#333' }}>
          🤖 {robotId}
        </span>
        <div style={{ display: 'flex', gap: '1.4rem', alignItems: 'center' }}>
          <BatteryLabel percent={robot?.battery_percent ?? null} />
          <span style={{ color: '#999', fontVariantNumeric: 'tabular-nums', fontSize: '0.95rem' }}>
            {now.toLocaleTimeString('ko-KR')}
          </span>
        </div>
      </header>

      {/* ── Main content ────────────────────────────────────────────────── */}
      <main style={mainStyle}>

        {/* Robot face — fixed size so layout never jumps on state change */}
        <img
          src={faceImage}
          alt="robot face"
          style={faceStyle}
        />

        {/* State message */}
        <p style={messageStyle}>{message}</p>

        {/* Active task info + stop badge */}
        {activeTask && (
          <div style={taskInfoStyle}>
            <span style={{ fontWeight: 600, fontSize: '1rem', color: '#333' }}>
              {TASK_TYPE_LABELS[activeTask.task_type] ?? activeTask.task_type}
            </span>
            {showButtons && (
              <span style={{
                ...stopBadgeBase,
                background: currentStop === 'origin' ? '#fff3e0' : '#e8f5e9',
                color:      currentStop === 'origin' ? '#e65100' : '#2e7d32',
                border: `1px solid ${currentStop === 'origin' ? '#ffcc80' : '#a5d6a7'}`,
              }}>
                {currentStop === 'origin' ? '출발지' : '목적지'}
              </span>
            )}
            {activeTask.destination_location && (
              <span style={{ color: '#999', fontSize: '0.85rem' }}>
                → {activeTask.destination_location.display_name}
              </span>
            )}
          </div>
        )}

        {/* Action buttons — only in interaction states */}
        {showButtons && (
          <div style={btnGroupStyle}>
            {/* Toggle: 잠금해제 / 잠금 */}
            <button
              onClick={handleToggle}
              disabled={lockActionPending}
              style={{
                ...btnBase,
                ...(isUnlocked ? lockBtnExtra : unlockBtnExtra),
                opacity: lockActionPending ? 0.55 : 1,
              }}
            >
              {lockActionPending && !canComplete
                ? '처리 중...'
                : `${toggleIcon} ${toggleLabel}`}
            </button>

            {/* Complete */}
            <button
              onClick={handleComplete}
              disabled={lockActionPending}
              style={{
                ...btnBase,
                ...completeBtnExtra,
                opacity: lockActionPending ? 0.55 : canComplete ? 1 : 0.38,
                cursor: lockActionPending ? 'not-allowed' : 'pointer',
              }}
            >
              {lockActionPending && canComplete ? '처리 중...' : '✅ 작업 완료'}
            </button>
          </div>
        )}

        {/* API / network error */}
        {apiError && (
          <div style={apiErrorStyle}>
            <span style={{ flex: 1 }}>{apiError}</span>
            <button
              onClick={() => setApiError(null)}
              style={{ background: 'none', border: 'none', color: '#b71c1c', fontSize: '1.1rem', cursor: 'pointer', lineHeight: 1 }}
            >
              ×
            </button>
          </div>
        )}

      </main>
    </div>
  )
}

// ── Small helpers ─────────────────────────────────────────────────────────────

function BatteryLabel({ percent }: { percent: number | null }) {
  if (percent === null) return <span style={{ color: '#aaa' }}>🔋 —%</span>
  const color = percent <= 20 ? '#c62828' : percent <= 40 ? '#e65100' : '#2e7d32'
  return <span style={{ color, fontWeight: 600, fontSize: '0.95rem' }}>🔋 {percent.toFixed(0)}%</span>
}

// ── Styles ────────────────────────────────────────────────────────────────────

const pageStyle: React.CSSProperties = {
  minHeight: '100vh',
  background: '#ffffff',
  color: '#222',
  display: 'flex',
  flexDirection: 'column',
  fontFamily: "'Noto Sans KR', 'Apple SD Gothic Neo', sans-serif",
}

const headerStyle: React.CSSProperties = {
  padding: '0.75rem 1.5rem',
  display: 'flex',
  justifyContent: 'space-between',
  alignItems: 'center',
  borderBottom: '1px solid #e8e8e8',
  background: '#fafafa',
}

const mainStyle: React.CSSProperties = {
  flex: 1,
  display: 'flex',
  flexDirection: 'column',
  alignItems: 'center',
  justifyContent: 'center',
  padding: '2rem 1.5rem',
  gap: '1.4rem',
}

const faceStyle: React.CSSProperties = {
  width: 'min(55vw, 420px)',
  height: 'auto',
  userSelect: 'none',
  flexShrink: 0,
}

const messageStyle: React.CSSProperties = {
  fontSize: 'clamp(1.4rem, 4vw, 2.2rem)',
  fontWeight: 700,
  textAlign: 'center',
  margin: 0,
  color: '#1a1a1a',
  lineHeight: 1.3,
}

const taskInfoStyle: React.CSSProperties = {
  display: 'flex',
  flexWrap: 'wrap',
  alignItems: 'center',
  gap: '0.5rem',
  justifyContent: 'center',
  padding: '0.6rem 1.2rem',
  background: '#f5f5f5',
  borderRadius: 12,
  fontSize: '0.9rem',
}

const stopBadgeBase: React.CSSProperties = {
  padding: '2px 10px',
  borderRadius: 20,
  fontSize: '0.8rem',
  fontWeight: 700,
  letterSpacing: '0.04em',
}

const btnGroupStyle: React.CSSProperties = {
  display: 'flex',
  flexDirection: 'column',
  gap: '0.85rem',
  alignItems: 'center',
  width: '100%',
  maxWidth: 400,
}

const btnBase: React.CSSProperties = {
  width: '100%',
  border: 'none',
  borderRadius: 16,
  padding: '1.2rem 2rem',
  fontSize: 'clamp(1.1rem, 3.5vw, 1.5rem)',
  fontWeight: 800,
  cursor: 'pointer',
  letterSpacing: '0.02em',
  transition: 'opacity 0.15s',
}

const unlockBtnExtra: React.CSSProperties = {
  background: '#e65100',
  color: '#fff',
  boxShadow: '0 4px 16px rgba(230,81,0,0.35)',
}

const lockBtnExtra: React.CSSProperties = {
  background: '#1565c0',
  color: '#fff',
  boxShadow: '0 4px 16px rgba(21,101,192,0.35)',
}

const completeBtnExtra: React.CSSProperties = {
  background: '#2e7d32',
  color: '#fff',
  boxShadow: '0 4px 16px rgba(46,125,50,0.35)',
}

const apiErrorStyle: React.CSSProperties = {
  display: 'flex',
  alignItems: 'center',
  gap: 10,
  background: '#ffebee',
  color: '#b71c1c',
  border: '1px solid #ef9a9a',
  padding: '0.7rem 1rem',
  borderRadius: 10,
  fontSize: '0.9rem',
  maxWidth: 400,
  width: '100%',
}
