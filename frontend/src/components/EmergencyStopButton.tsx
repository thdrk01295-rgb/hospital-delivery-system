import { useState } from 'react'
import { triggerEmergency, releaseEmergency } from '@/api/tasks'
import { useAbnormalEventStore }              from '@/store/abnormalEventStore'

export function EmergencyStopButton() {
  const { activeEvent } = useAbnormalEventStore()
  const [loading, setLoading] = useState(false)

  const isEmergency = activeEvent?.event_type === 'emergency_call'

  async function handleTrigger() {
    if (!window.confirm('비상 정지를 발동하겠습니까?')) return
    setLoading(true)
    try {
      await triggerEmergency()
    } catch (err) {
      alert(err instanceof Error ? err.message : '비상 정지 실패')
    } finally {
      setLoading(false)
    }
  }

  async function handleRelease() {
    if (!window.confirm('비상 정지를 해제하겠습니까?')) return
    setLoading(true)
    try {
      await releaseEmergency()
    } catch (err) {
      alert(err instanceof Error ? err.message : '비상 해제 실패')
    } finally {
      setLoading(false)
    }
  }

  if (isEmergency) {
    return (
      <button
        onClick={handleRelease}
        disabled={loading}
        style={{
          padding: '0.4rem 1rem',
          background: '#7f1d1d',
          color: '#fff',
          border: '2px solid #dc2626',
          borderRadius: 6,
          fontWeight: 700,
          fontSize: '0.82rem',
          cursor: loading ? 'not-allowed' : 'pointer',
          letterSpacing: '0.03em',
          opacity: loading ? 0.7 : 1,
        }}
      >
        {loading ? '처리 중...' : '⚠ 비상 해제'}
      </button>
    )
  }

  return (
    <button
      onClick={handleTrigger}
      disabled={loading}
      style={{
        padding: '0.4rem 1rem',
        background: '#dc2626',
        color: '#fff',
        border: 'none',
        borderRadius: 6,
        fontWeight: 700,
        fontSize: '0.82rem',
        cursor: loading ? 'not-allowed' : 'pointer',
        letterSpacing: '0.03em',
        opacity: loading ? 0.7 : 1,
      }}
    >
      {loading ? '처리 중...' : '🚨 비상 정지'}
    </button>
  )
}
