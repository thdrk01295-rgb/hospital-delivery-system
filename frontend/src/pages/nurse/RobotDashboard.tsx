/**
 * Robot Dashboard — tablet / nurse monitoring view at /nurse/robot
 *
 * Sections (from approved Step 6):
 *   1. Driving State    — full-width Korean state text panel (colour-coded)
 *   2. Current Position — last known location text
 *   3. Radar Log        — scrollable list of recent robot state transitions
 *                         (updated in real time via robotStore)
 *   4. Sensor Node Status — placeholder grid of sensor readings
 *   5. E-stop           — emergency stop button → POST /tasks/nurse/emergency
 *   6. Remote View      — placeholder panel (future camera/stream integration)
 */
import { useEffect, useRef, useState } from 'react'
import { Link }                         from 'react-router-dom'
import { fetchRobotStatus }             from '@/api/robot'
import { triggerEmergency }             from '@/api/tasks'
import { useRobotStore }                from '@/store/robotStore'
import type { RobotState }              from '@/types'

// ── State display config ──────────────────────────────────────────────────────

const STATE_DISPLAY: Record<RobotState, { text: string; bg: string; color: string }> = {
  IDLE:              { text: '대기 중',                                               bg: '#ecf0f1', color: '#2c3e50' },
  MOVING:            { text: '이동 중...',                                            bg: '#2980b9', color: '#fff'    },
  ARRIVED:           { text: '도착했습니다',                                          bg: '#27ae60', color: '#fff'    },
  WAIT_NFC:          { text: '의료인님. 작업을 수행하려면 카드를 태그해주세요',         bg: '#f39c12', color: '#fff'    },
  AUTH_SUCCESS:      { text: '인증되었습니다',                                        bg: '#27ae60', color: '#fff'    },
  AUTH_FAIL:         { text: '인증 실패',                                             bg: '#c0392b', color: '#fff'    },
  DELIVERY_OPEN_NUR: { text: '락이 해제 되었습니다. 작업을 완료하시고 카드를 태그해주세요', bg: '#8e44ad', color: '#fff' },
  DELIVERY_OPEN_PAT: { text: '의류 작업을 완료 후 완료 버튼을 눌러주세요',             bg: '#16a085', color: '#fff'    },
  COMPLETE:          { text: '처리 완료',                                             bg: '#27ae60', color: '#fff'    },
  LOW_BATTERY:       { text: '충전이 필요합니다',                                     bg: '#e67e22', color: '#fff'    },
  CHAGING_BATTERY:   { text: '충전중',                                                bg: '#95a5a6', color: '#fff'    },
  ERROR:             { text: 'ERROR. 관리자에게 문의하세요',                          bg: '#c0392b', color: '#fff'    },
}

// Placeholder sensor nodes — names reflect real AMR sensor categories
const SENSOR_NODES = [
  { id: 'lidar',    label: 'LiDAR',      unit: 'pts/s' },
  { id: 'imu',      label: 'IMU',        unit: 'deg/s' },
  { id: 'encoder',  label: 'Encoder',    unit: 'rpm'   },
  { id: 'ultrasnd', label: 'Ultrasonic', unit: 'cm'    },
  { id: 'motor_l',  label: 'Motor L',    unit: 'A'     },
  { id: 'motor_r',  label: 'Motor R',    unit: 'A'     },
]

interface RadarEntry {
  time: string
  state: RobotState
  location: string | null
}

// ── Component ─────────────────────────────────────────────────────────────────

export function RobotDashboard() {
  const { robot, setRobot } = useRobotStore()
  const [estopLoading, setEstopLoading] = useState(false)
  const [radarLog, setRadarLog]         = useState<RadarEntry[]>([])
  const prevStateRef                    = useRef<RobotState | null>(null)

  // Initial hydration
  useEffect(() => {
    fetchRobotStatus().then(setRobot).catch(console.error)
  }, [])

  // Append to radar log whenever state changes
  useEffect(() => {
    if (!robot) return
    const current = robot.current_state as RobotState
    if (current === prevStateRef.current) return
    prevStateRef.current = current
    setRadarLog((prev) => [
      {
        time:     new Date().toLocaleTimeString('ko-KR'),
        state:    current,
        location: robot.location_display ?? null,
      },
      ...prev.slice(0, 49),   // keep last 50 entries
    ])
  }, [robot?.current_state])

  async function handleEstop() {
    if (!window.confirm('긴급 정지(E-Stop)를 발생시키겠습니까?')) return
    setEstopLoading(true)
    try {
      await triggerEmergency()
    } catch (err: unknown) {
      alert(err instanceof Error ? err.message : 'E-Stop 실패')
    } finally {
      setEstopLoading(false)
    }
  }

  const state   = (robot?.current_state ?? 'IDLE') as RobotState
  const display = STATE_DISPLAY[state] ?? STATE_DISPLAY.IDLE

  return (
    <div style={{ fontFamily: 'sans-serif', minHeight: '100vh', background: '#1a252f', color: '#ecf0f1', display: 'flex', flexDirection: 'column' }}>

      {/* ── Header ─────────────────────────────────────────────────── */}
      <header style={{ padding: '0.7rem 1.5rem', display: 'flex', justifyContent: 'space-between', alignItems: 'center', background: 'rgba(0,0,0,0.35)' }}>
        <strong style={{ fontSize: '1.05rem' }}>🤖 로봇 대시보드</strong>
        <Link to="/nurse/dashboard" style={{ color: '#7fb3d3', textDecoration: 'none', fontSize: '0.9rem' }}>
          ← 간호사 대시보드
        </Link>
      </header>

      {/* ── Main grid ──────────────────────────────────────────────── */}
      <main style={{ flex: 1, display: 'grid', gridTemplateColumns: '1fr 1fr', gridTemplateRows: 'auto auto auto', gap: '1rem', padding: '1rem' }}>

        {/* 1. Driving State — spans full width */}
        <section style={{ ...panel, gridColumn: '1 / -1', background: display.bg, transition: 'background 0.4s', padding: '2rem' }}>
          <SectionLabel light={display.color === '#fff'}>주행 상태 (Driving State)</SectionLabel>
          <p style={{
            fontSize: 'clamp(1.4rem, 3.5vw, 2.6rem)', fontWeight: 700,
            color: display.color, textAlign: 'center', margin: '0.5rem 0 0',
            lineHeight: 1.5,
          }}>
            {display.text}
          </p>
          <p style={{ textAlign: 'center', opacity: 0.75, fontSize: '0.85rem', color: display.color, marginTop: 6 }}>
            상태코드: <code>{state}</code>
          </p>
        </section>

        {/* 2. Current Position */}
        <section style={panel}>
          <SectionLabel>현재 위치 (Current Position)</SectionLabel>
          <p style={{ fontSize: '1.3rem', fontWeight: 600, margin: '0.4rem 0' }}>
            📍 {robot?.location_display ?? '알 수 없음'}
          </p>
          <p style={dimText}>🔋 배터리: <strong>{robot?.battery_percent?.toFixed(0) ?? '--'}%</strong></p>
          <p style={dimText}>마지막 수신: {robot?.last_seen_at
            ? new Date(robot.last_seen_at).toLocaleTimeString('ko-KR')
            : '—'
          }</p>
        </section>

        {/* 3. Radar Log */}
        <section style={{ ...panel, overflowY: 'auto', maxHeight: 280 }}>
          <SectionLabel>레이더 로그 (Radar Log)</SectionLabel>
          {radarLog.length === 0
            ? <p style={dimText}>상태 변화 없음</p>
            : radarLog.map((entry, i) => (
              <div key={i} style={{
                display: 'flex', gap: 10, alignItems: 'baseline',
                fontSize: '0.82rem', padding: '3px 0', borderBottom: '1px solid rgba(255,255,255,0.05)',
              }}>
                <span style={{ color: '#7fb3d3', flexShrink: 0 }}>{entry.time}</span>
                <span style={{ fontWeight: 600, color: STATE_DISPLAY[entry.state]?.bg ?? '#fff' }}>
                  {entry.state}
                </span>
                {entry.location && <span style={dimText}>@ {entry.location}</span>}
              </div>
            ))
          }
        </section>

        {/* 4. Sensor Node Status */}
        <section style={panel}>
          <SectionLabel>센서 노드 상태 (Sensor Node Status)</SectionLabel>
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '0.5rem', marginTop: 8 }}>
            {SENSOR_NODES.map((node) => (
              <div key={node.id} style={{
                background: 'rgba(255,255,255,0.05)', borderRadius: 6,
                padding: '0.4rem 0.7rem', display: 'flex', justifyContent: 'space-between',
                alignItems: 'center', fontSize: '0.82rem',
              }}>
                <span>{node.label}</span>
                <span style={{ color: '#2ecc71', fontFamily: 'monospace' }}>
                  — {node.unit}
                </span>
              </div>
            ))}
          </div>
          <p style={{ ...dimText, marginTop: 8, fontSize: '0.75rem' }}>
            실시간 센서 데이터는 ROS2 토픽 연동 후 활성화됩니다
          </p>
        </section>

        {/* 5. E-Stop + 6. Remote View — side by side */}
        <section style={{ ...panel, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 12 }}>
          <SectionLabel>긴급 정지 (E-Stop)</SectionLabel>
          <button
            onClick={handleEstop}
            disabled={estopLoading}
            style={{
              width: 140, height: 140, borderRadius: '50%',
              background: estopLoading ? '#922b21' : '#e74c3c',
              border: '4px solid #c0392b',
              color: '#fff', fontSize: '1rem', fontWeight: 700,
              cursor: 'pointer', boxShadow: '0 0 24px rgba(231,76,60,0.5)',
              transition: 'background 0.2s',
            }}
          >
            {estopLoading ? '처리 중...' : 'E-STOP'}
          </button>
          <p style={{ ...dimText, fontSize: '0.78rem', textAlign: 'center' }}>
            긴급 상황 시 눌러 주세요
          </p>
        </section>

        <section style={{ ...panel, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 10 }}>
          <SectionLabel>원격 보기 (Remote View)</SectionLabel>
          <div style={{
            width: '100%', flex: 1, minHeight: 160,
            background: 'rgba(0,0,0,0.3)', borderRadius: 8,
            display: 'flex', alignItems: 'center', justifyContent: 'center',
            border: '1px dashed rgba(255,255,255,0.15)',
          }}>
            <p style={{ color: 'rgba(255,255,255,0.3)', fontSize: '0.85rem', textAlign: 'center' }}>
              📷 카메라 스트림<br />(추후 연동 예정)
            </p>
          </div>
        </section>

      </main>
    </div>
  )
}

// ── Small helpers ─────────────────────────────────────────────────────────────

function SectionLabel({ children, light = false }: { children: React.ReactNode; light?: boolean }) {
  return (
    <p style={{
      margin: '0 0 6px', fontSize: '0.75rem', fontWeight: 700,
      textTransform: 'uppercase', letterSpacing: '0.08em',
      color: light ? 'rgba(255,255,255,0.7)' : 'rgba(255,255,255,0.45)',
    }}>
      {children}
    </p>
  )
}

const panel: React.CSSProperties = {
  background: 'rgba(255,255,255,0.05)',
  border: '1px solid rgba(255,255,255,0.08)',
  borderRadius: 10,
  padding: '1rem 1.2rem',
}

const dimText: React.CSSProperties = {
  color: 'rgba(255,255,255,0.45)',
  fontSize: '0.85rem',
  margin: '3px 0',
}
