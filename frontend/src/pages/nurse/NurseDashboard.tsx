/**
 * Nurse Dashboard — hospital operations console.
 *
 * Layout:
 *   Header:   title | clock | [New Order] [Logout] [Emergency Stop]
 *   Banner:   active abnormal event (RobotStatusBanner)
 *   Main 2-col:
 *     Left (sidebar):  SystemStatusCard | Inventory | TaskCreatePanel
 *     Right (content): ActiveTaskList  | RecentTaskTable
 *
 * Data:
 *   Initial hydration on mount via REST; live updates via WebSocket
 *   (dispatched by useWebSocket in App.tsx → Zustand stores).
 */
import { useEffect, useState }        from 'react'
import { Link, useNavigate }           from 'react-router-dom'
import { fetchRobotStatus }            from '@/api/robot'
import { fetchOngoingTasks, fetchCompletedTasks, cancelNurseTask } from '@/api/tasks'
import { fetchInventory, fetchRobotInventory } from '@/api/inventory'
import { fetchActiveAbnormalEvent }    from '@/api/abnormalEvents'
import { useRobotStore }               from '@/store/robotStore'
import { useTaskStore }                from '@/store/taskStore'
import { useInventoryStore }           from '@/store/inventoryStore'
import { useAbnormalEventStore }       from '@/store/abnormalEventStore'
import { useAuthStore }                from '@/store/authStore'
import { RobotStatusBanner }           from '@/components/RobotStatusBanner'
import { TaskCard }                    from '@/components/TaskCard'
import { InventoryTable }              from '@/components/InventoryTable'
import { EmergencyStopButton }         from '@/components/EmergencyStopButton'
import { SystemStatusCard }            from '@/components/SystemStatusCard'
import { TaskCreatePanel }             from '@/components/TaskCreatePanel'
import { RobotInventoryPanel }         from '@/components/RobotInventoryPanel'
import type { Task }                   from '@/types'

export function NurseDashboard() {
  const navigate   = useNavigate()
  const { logout } = useAuthStore()

  const { robot, setRobot }                                                   = useRobotStore()
  const { ongoingTasks, completedTasks, setOngoingTasks, setCompletedTasks, applyTaskUpdate } = useTaskStore()
  const { items: inventory, robotInventory, setItems, setRobotInventory }    = useInventoryStore()
  const { setActiveEvent }                                                    = useAbnormalEventStore()
  const [now, setNow]                                                         = useState(new Date())

  useEffect(() => {
    const id = setInterval(() => setNow(new Date()), 1000)
    return () => clearInterval(id)
  }, [])

  useEffect(() => {
    Promise.all([
      fetchRobotStatus(),
      fetchOngoingTasks(),
      fetchCompletedTasks(),
      fetchInventory(),
      fetchActiveAbnormalEvent(),
      fetchRobotInventory().catch(() => null),
    ]).then(([robotData, ongoing, completed, inv, abnormal, robotInv]) => {
      setRobot(robotData)
      setOngoingTasks(ongoing)
      setCompletedTasks(completed)
      setItems(inv)
      setActiveEvent(abnormal)
      if (robotInv) setRobotInventory(robotInv)
    }).catch(console.error)
  }, [])

  async function handleCancelTask(taskId: number) {
    try {
      const updated = await cancelNurseTask(taskId)
      applyTaskUpdate(updated)
    } catch (err) {
      alert(err instanceof Error ? err.message : '작업 취소 실패')
    }
  }

  function handleLogout() {
    logout()
    navigate('/login/nurse', { replace: true })
  }

  return (
    <div style={shell}>

      {/* ── Header ─────────────────────────────────────────────────────── */}
      <header style={header}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.6rem' }}>
          <span style={{ fontSize: '1.05rem', fontWeight: 800, letterSpacing: '-0.02em' }}>
            AMR 물류 시스템
          </span>
          <span style={{ fontSize: '0.75rem', color: 'rgba(255,255,255,0.45)', paddingLeft: 2 }}>
            간호사 대시보드
          </span>
        </div>

        <div style={{ fontSize: '0.82rem', color: 'rgba(255,255,255,0.65)', display: 'flex', gap: '0.75rem', alignItems: 'center' }}>
          <span>{now.toLocaleDateString('ko-KR')}</span>
          <span style={{ fontWeight: 700, color: '#fff', fontSize: '0.9rem' }}>
            {now.toLocaleTimeString('ko-KR')}
          </span>
        </div>

        <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
          <Link to="/nurse/orders/new" style={navLink}>새 오더</Link>
          <button onClick={handleLogout} style={navBtn}>로그아웃</button>
          <EmergencyStopButton />
        </div>
      </header>

      {/* ── Abnormal event banner ──────────────────────────────────────── */}
      <RobotStatusBanner />

      {/* ── Main 2-column grid ────────────────────────────────────────── */}
      <main style={mainGrid}>

        {/* Left sidebar */}
        <aside style={sidebar}>
          <SystemStatusCard robot={robot} />

          <div style={card}>
            <div style={sectionLabel}>재고 현황</div>
            <InventoryTable items={inventory} />
          </div>

          <div style={card}>
            <div style={{ ...sectionLabel, marginBottom: '0.6rem' }}>로봇 탑재 재고</div>
            <RobotInventoryPanel inventory={robotInventory} />
          </div>

          <TaskCreatePanel />
        </aside>

        {/* Right content area */}
        <section style={{ display: 'flex', flexDirection: 'column', gap: '1.2rem', minWidth: 0 }}>

          {/* Active tasks */}
          <div style={card}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '0.8rem' }}>
              <span style={sectionLabel}>진행 중인 작업</span>
              <span style={countBadge}>{ongoingTasks.length}</span>
            </div>
            {ongoingTasks.length === 0
              ? <p style={emptyMsg}>진행 중인 작업이 없습니다</p>
              : ongoingTasks.map((t) => <TaskCard key={t.id} task={t} onCancel={handleCancelTask} />)
            }
          </div>

          {/* Recent completed tasks */}
          <div style={card}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '0.8rem' }}>
              <span style={sectionLabel}>최근 완료 작업</span>
              {completedTasks.length > 0 && (
                <span style={{ fontSize: '0.75rem', color: '#94a3b8' }}>최근 {Math.min(completedTasks.length, 15)}건</span>
              )}
            </div>
            {completedTasks.length === 0
              ? <p style={emptyMsg}>완료된 작업이 없습니다</p>
              : <RecentTaskTable tasks={completedTasks.slice(0, 15)} />
            }
          </div>

        </section>
      </main>
    </div>
  )
}

// ── Inline: recent task table ─────────────────────────────────────────

const TYPE_LABELS: Record<string, string> = {
  clothes_refill:          '의류 보충',
  kit_delivery:            '키트 배송',
  kit_refill:              '키트 보충',
  specimen_delivery:       '검체 배송',
  logistics_delivery:      '물류 배송',
  used_clothes_collection: '사용 의류 수거',
  battery_low:             '배터리 부족',
  emergency_call:          '긴급 호출',
  patient_clothes_rental:  '의류 대여',
  patient_clothes_return:  '의류 반납',
}

const STATUS_LABELS: Record<string, string> = {
  PENDING: '대기', DISPATCHED: '배정', IN_PROGRESS: '진행',
  COMPLETE: '완료', CANCELLED: '취소', FAILED: '실패',
}

const STATUS_HEX: Record<string, string> = {
  PENDING: '#f59e0b', DISPATCHED: '#3b82f6', IN_PROGRESS: '#10b981',
  COMPLETE: '#6b7280', CANCELLED: '#9ca3af', FAILED: '#ef4444',
}

function RecentTaskTable({ tasks }: { tasks: Task[] }) {
  return (
    <div style={{ overflowX: 'auto' }}>
      <table style={{ width: '100%', borderCollapse: 'collapse', fontSize: '0.84rem' }}>
        <thead>
          <tr style={{ borderBottom: '2px solid #e2e8f0' }}>
            <th style={th}>유형</th>
            <th style={th}>출발지</th>
            <th style={th}>목적지</th>
            <th style={th}>상태</th>
            <th style={th}>시각</th>
          </tr>
        </thead>
        <tbody>
          {tasks.map((t) => {
            const hex = STATUS_HEX[t.status] ?? '#6b7280'
            return (
              <tr key={t.id} style={{ borderBottom: '1px solid #f1f5f9' }}>
                <td style={td}>{TYPE_LABELS[t.task_type] ?? t.task_type}</td>
                <td style={{ ...td, color: '#64748b' }}>{t.origin_location?.display_name ?? '—'}</td>
                <td style={{ ...td, color: '#64748b' }}>{t.destination_location?.display_name ?? '—'}</td>
                <td style={td}>
                  <span style={{
                    display: 'inline-block',
                    padding: '0.1rem 0.55rem',
                    borderRadius: 12,
                    fontSize: '0.75rem',
                    fontWeight: 700,
                    background: hex + '22',
                    color: hex,
                  }}>
                    {STATUS_LABELS[t.status] ?? t.status}
                  </span>
                </td>
                <td style={{ ...td, color: '#94a3b8', fontSize: '0.78rem' }}>
                  {new Date(t.created_at).toLocaleTimeString('ko-KR')}
                </td>
              </tr>
            )
          })}
        </tbody>
      </table>
    </div>
  )
}

// ── Style constants ───────────────────────────────────────────────────

const shell: React.CSSProperties = {
  fontFamily: 'system-ui, -apple-system, "Segoe UI", sans-serif',
  background: '#f0f2f5',
  minHeight: '100vh',
  display: 'flex',
  flexDirection: 'column',
}

const header: React.CSSProperties = {
  background: '#1e3a5f',
  color: '#fff',
  padding: '0 1.5rem',
  height: 56,
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'space-between',
  flexShrink: 0,
  boxShadow: '0 2px 8px rgba(0,0,0,0.18)',
}

const mainGrid: React.CSSProperties = {
  flex: 1,
  padding: '1.25rem 1.5rem',
  display: 'grid',
  gridTemplateColumns: '280px 1fr',
  gap: '1.25rem',
  alignItems: 'start',
}

const sidebar: React.CSSProperties = {
  display: 'flex',
  flexDirection: 'column',
  gap: '1.1rem',
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
}

const countBadge: React.CSSProperties = {
  background: '#1d6fb8',
  color: '#fff',
  borderRadius: 12,
  padding: '0.1rem 0.55rem',
  fontSize: '0.75rem',
  fontWeight: 700,
}

const emptyMsg: React.CSSProperties = {
  color: '#94a3b8',
  fontSize: '0.85rem',
  margin: '0.25rem 0',
}

const navLink: React.CSSProperties = {
  padding: '0.35rem 0.85rem',
  background: 'rgba(255,255,255,0.12)',
  color: '#fff',
  borderRadius: 6,
  textDecoration: 'none',
  fontSize: '0.82rem',
  fontWeight: 600,
}

const navBtn: React.CSSProperties = {
  padding: '0.35rem 0.85rem',
  background: 'transparent',
  color: 'rgba(255,255,255,0.65)',
  border: '1px solid rgba(255,255,255,0.22)',
  borderRadius: 6,
  fontSize: '0.82rem',
  cursor: 'pointer',
}

const th: React.CSSProperties = {
  padding: '0.4rem 0.8rem',
  textAlign: 'left',
  fontWeight: 600,
  color: '#64748b',
  fontSize: '0.78rem',
  whiteSpace: 'nowrap',
}

const td: React.CSSProperties = {
  padding: '0.45rem 0.8rem',
  verticalAlign: 'middle',
}
