import type { RobotStatus, RobotState } from '@/types'

const STATE_LABELS: Record<RobotState, string> = {
  IDLE:              '대기 중',
  MOVING:            '이동 중',
  ARRIVED:           '도착',
  WAIT_UNLOCK:       '잠금 해제 대기',
  AUTH_SUCCESS:      '인증 완료',
  AUTH_FAIL:         '인증 실패',
  DELIVERY_OPEN_NUR: '작업함 열림 (의료진)',
  DELIVERY_OPEN_PAT: '작업함 열림 (환자)',
  COMPLETE:          '처리 완료',
  LOW_BATTERY:       '배터리 부족',
  CHARGING_BATTERY:  '충전 중',
  ERROR:             '오류',
  EMERGENCY:         '비상 정지',
}

const STATE_CHIP: Record<RobotState, { bg: string; color: string }> = {
  IDLE:              { bg: '#f1f5f9', color: '#475569' },
  MOVING:            { bg: '#dbeafe', color: '#1d4ed8' },
  ARRIVED:           { bg: '#d1fae5', color: '#065f46' },
  WAIT_UNLOCK:       { bg: '#fef3c7', color: '#92400e' },
  AUTH_SUCCESS:      { bg: '#d1fae5', color: '#065f46' },
  AUTH_FAIL:         { bg: '#fee2e2', color: '#991b1b' },
  DELIVERY_OPEN_NUR: { bg: '#ede9fe', color: '#5b21b6' },
  DELIVERY_OPEN_PAT: { bg: '#d1fae5', color: '#065f46' },
  COMPLETE:          { bg: '#d1fae5', color: '#065f46' },
  LOW_BATTERY:       { bg: '#ffedd5', color: '#9a3412' },
  CHARGING_BATTERY:  { bg: '#e2e8f0', color: '#475569' },
  ERROR:             { bg: '#fee2e2', color: '#991b1b' },
  EMERGENCY:         { bg: '#fee2e2', color: '#7f1d1d' },
}

const BATTERY_COLOR = (pct: number) =>
  pct > 50 ? '#059669' : pct > 20 ? '#d97706' : '#dc2626'

interface Props {
  robot: RobotStatus | null
}

export function SystemStatusCard({ robot }: Props) {
  const state     = (robot?.current_state ?? 'IDLE') as RobotState
  const chip      = STATE_CHIP[state] ?? STATE_CHIP.IDLE
  const battery   = robot?.battery_percent ?? 0

  return (
    <div style={card}>
      <div style={sectionLabel}>로봇 상태</div>

      <span style={{
        display: 'inline-block',
        padding: '0.25rem 0.75rem',
        borderRadius: 20,
        background: chip.bg,
        color: chip.color,
        fontWeight: 700,
        fontSize: '0.88rem',
        marginBottom: '0.1rem',
      }}>
        {STATE_LABELS[state] ?? state}
      </span>
      <div style={{ fontSize: '0.72rem', color: '#94a3b8', marginBottom: '0.9rem' }}>
        코드: {state}
      </div>

      <div style={{ display: 'flex', flexDirection: 'column', gap: 0 }}>
        <div style={row}>
          <span style={rowLabel}>배터리</span>
          <span style={{ fontWeight: 700, color: BATTERY_COLOR(battery) }}>
            {battery.toFixed(0)}%
          </span>
        </div>
        <div style={row}>
          <span style={rowLabel}>현재 위치</span>
          <span style={{ fontWeight: 600, fontSize: '0.85rem', textAlign: 'right', maxWidth: '60%' }}>
            {robot?.location_display ?? '알 수 없음'}
          </span>
        </div>
        <div style={{ ...row, borderBottom: 'none' }}>
          <span style={rowLabel}>마지막 수신</span>
          <span style={{ fontSize: '0.82rem', color: '#64748b' }}>
            {robot?.last_seen_at
              ? new Date(robot.last_seen_at).toLocaleTimeString('ko-KR')
              : '—'}
          </span>
        </div>
      </div>
    </div>
  )
}

const card: React.CSSProperties = {
  background: '#fff',
  border: '1px solid #e2e8f0',
  borderRadius: 10,
  padding: '1rem 1.1rem',
  boxShadow: '0 1px 3px rgba(0,0,0,0.06)',
}

const sectionLabel: React.CSSProperties = {
  fontSize: '0.7rem',
  fontWeight: 700,
  textTransform: 'uppercase',
  letterSpacing: '0.08em',
  color: '#94a3b8',
  marginBottom: '0.6rem',
}

const row: React.CSSProperties = {
  display: 'flex',
  justifyContent: 'space-between',
  alignItems: 'center',
  padding: '0.35rem 0',
  borderBottom: '1px solid #f1f5f9',
  fontSize: '0.85rem',
}

const rowLabel: React.CSSProperties = {
  color: '#64748b',
  flexShrink: 0,
}
