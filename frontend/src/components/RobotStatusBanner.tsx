/**
 * Displays the active abnormal event as a dismissible banner.
 * Shown on both nurse layout pages.
 */
import { useAbnormalEventStore } from '@/store/abnormalEventStore'
import { resolveAbnormalEvent }  from '@/api/abnormalEvents'

const EVENT_LABELS: Record<string, string> = {
  error:          'ERROR — 관리자에게 문의하세요',
  low_battery:    '배터리 부족 — 충전이 필요합니다',
  emergency_call: '긴급 호출 발생',
}

export function RobotStatusBanner() {
  const { activeEvent, setActiveEvent } = useAbnormalEventStore()

  if (!activeEvent) return null

  const label = EVENT_LABELS[activeEvent.event_type] ?? activeEvent.event_type

  async function handleResolve() {
    if (!activeEvent) return
    try {
      await resolveAbnormalEvent(activeEvent.id)
      setActiveEvent(null)
    } catch {
      // keep banner visible on failure
    }
  }

  return (
    <div style={{
      background: '#c0392b', color: '#fff', padding: '0.6rem 1.2rem',
      display: 'flex', justifyContent: 'space-between', alignItems: 'center',
    }}>
      <strong>⚠ {label}</strong>
      {activeEvent.note && <span style={{ marginLeft: '1rem', opacity: 0.85 }}>{activeEvent.note}</span>}
      <button
        onClick={handleResolve}
        style={{ marginLeft: '1rem', background: 'transparent', border: '1px solid #fff',
                 color: '#fff', cursor: 'pointer', padding: '0.2rem 0.7rem', borderRadius: 4 }}
      >
        해제
      </button>
    </div>
  )
}
