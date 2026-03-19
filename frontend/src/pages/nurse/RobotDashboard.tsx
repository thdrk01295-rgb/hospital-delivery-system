/**
 * Robot Dashboard — tablet / monitoring view.
 * Shows the robot's current state as large Korean display text.
 * Also shows battery, location, and current ongoing task.
 * Does NOT show order creation or patient-specific screens.
 */
import { useEffect } from 'react'
import { Link }       from 'react-router-dom'
import { fetchRobotStatus } from '@/api/robot'
import { useRobotStore }    from '@/store/robotStore'
import type { RobotState }  from '@/types'

const STATE_DISPLAY: Record<RobotState, { text: string; bg: string; color: string }> = {
  IDLE:              { text: '대기 중',                                    bg: '#ecf0f1', color: '#2c3e50' },
  MOVING:            { text: '이동 중...',                                  bg: '#2980b9', color: '#fff'   },
  ARRIVED:           { text: '도착했습니다',                                bg: '#27ae60', color: '#fff'   },
  WAIT_NFC:          { text: '의료인님. 작업을 수행하려면 카드를 태그해주세요', bg: '#f39c12', color: '#fff'   },
  AUTH_SUCCESS:      { text: '인증되었습니다',                              bg: '#27ae60', color: '#fff'   },
  AUTH_FAIL:         { text: '인증 실패',                                   bg: '#c0392b', color: '#fff'   },
  DELIVERY_OPEN_NUR: { text: '락이 해제 되었습니다. 작업을 완료하시고 카드를 태그해주세요', bg: '#8e44ad', color: '#fff' },
  DELIVERY_OPEN_PAT: { text: '의류 작업을 완료 후 완료 버튼을 눌러주세요',  bg: '#16a085', color: '#fff'   },
  COMPLETE:          { text: '처리 완료',                                   bg: '#27ae60', color: '#fff'   },
  LOW_BATTERY:       { text: '충전이 필요합니다',                           bg: '#e67e22', color: '#fff'   },
  CHAGING_BATTERY:   { text: '충전중',                                      bg: '#95a5a6', color: '#fff'   },
  ERROR:             { text: 'ERROR. 관리자에게 문의하세요',                bg: '#c0392b', color: '#fff'   },
}

export function RobotDashboard() {
  const { robot, setRobot } = useRobotStore()

  useEffect(() => {
    fetchRobotStatus().then(setRobot).catch(console.error)
  }, [])

  const state   = (robot?.current_state ?? 'IDLE') as RobotState
  const display = STATE_DISPLAY[state] ?? STATE_DISPLAY.IDLE

  return (
    <div style={{ fontFamily: 'sans-serif', minHeight: '100vh', background: '#1a252f', color: '#fff', display: 'flex', flexDirection: 'column' }}>

      {/* Header */}
      <header style={{ padding: '0.8rem 1.5rem', display: 'flex', justifyContent: 'space-between', alignItems: 'center', background: 'rgba(0,0,0,0.3)' }}>
        <strong>🤖 로봇 대시보드</strong>
        <nav style={{ display: 'flex', gap: 12 }}>
          <Link to="/nurse/dashboard" style={{ color: '#7fb3d3', textDecoration: 'none', fontSize: '0.9rem' }}>← 간호사 대시보드</Link>
        </nav>
      </header>

      {/* State display */}
      <div style={{
        flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center',
        background: display.bg, margin: '1.5rem', borderRadius: 16,
        transition: 'background 0.4s',
      }}>
        <p style={{
          fontSize: 'clamp(1.5rem, 4vw, 3rem)', fontWeight: 700,
          color: display.color, textAlign: 'center', padding: '2rem',
          lineHeight: 1.5,
        }}>
          {display.text}
        </p>
      </div>

      {/* Info bar */}
      <footer style={{ padding: '1rem 1.5rem', background: 'rgba(0,0,0,0.2)', display: 'flex', gap: 30, flexWrap: 'wrap', fontSize: '0.95rem' }}>
        <span>🔋 배터리: <strong>{robot?.battery_percent?.toFixed(0) ?? '--'}%</strong></span>
        <span>📍 위치: <strong>{robot?.location_display ?? '알 수 없음'}</strong></span>
        <span>상태코드: <code style={{ background: 'rgba(255,255,255,0.1)', padding: '0 6px', borderRadius: 3 }}>{state}</code></span>
      </footer>
    </div>
  )
}
