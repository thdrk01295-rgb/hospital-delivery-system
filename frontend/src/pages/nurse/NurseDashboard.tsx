/**
 * Nurse Dashboard
 * ────────────────
 * Top bar:   battery | current time | current date | robot location
 * Banner:    active abnormal event (RobotStatusBanner)
 * Body:
 *   - Inventory status
 *   - Ongoing tasks
 *   - Completed tasks
 *   - Emergency call button
 * Nav:       link to Robot Dashboard | Order Create | Logout
 *
 * Initial REST hydration on mount:
 *   GET /robot/status, GET /tasks/ongoing, GET /tasks/completed, GET /inventory
 * Live updates via WebSocket (dispatched by useWebSocket in App.tsx).
 */
import { useEffect, useState } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import { fetchRobotStatus }       from '@/api/robot'
import { fetchOngoingTasks, fetchCompletedTasks, triggerEmergency } from '@/api/tasks'
import { fetchInventory }         from '@/api/inventory'
import { fetchActiveAbnormalEvent } from '@/api/abnormalEvents'
import { useRobotStore }          from '@/store/robotStore'
import { useTaskStore }           from '@/store/taskStore'
import { useInventoryStore }      from '@/store/inventoryStore'
import { useAbnormalEventStore }  from '@/store/abnormalEventStore'
import { useAuthStore }           from '@/store/authStore'
import { RobotStatusBanner }      from '@/components/RobotStatusBanner'
import { TaskCard }               from '@/components/TaskCard'
import { InventoryTable }         from '@/components/InventoryTable'

const BATTERY_COLOR = (pct: number) =>
  pct > 50 ? '#27ae60' : pct > 20 ? '#f39c12' : '#c0392b'

export function NurseDashboard() {
  const navigate        = useNavigate()
  const { logout }      = useAuthStore()
  const { robot, setRobot }               = useRobotStore()
  const { ongoingTasks, completedTasks, setOngoingTasks, setCompletedTasks } = useTaskStore()
  const { items: inventory, setItems }    = useInventoryStore()
  const { setActiveEvent }                = useAbnormalEventStore()
  const [now, setNow]                     = useState(new Date())
  const [emergencyLoading, setEmergencyLoading] = useState(false)

  // Clock tick
  useEffect(() => {
    const id = setInterval(() => setNow(new Date()), 1000)
    return () => clearInterval(id)
  }, [])

  // Initial REST hydration
  useEffect(() => {
    Promise.all([
      fetchRobotStatus(),
      fetchOngoingTasks(),
      fetchCompletedTasks(),
      fetchInventory(),
      fetchActiveAbnormalEvent(),
    ]).then(([robotData, ongoing, completed, inv, abnormal]) => {
      setRobot(robotData)
      setOngoingTasks(ongoing)
      setCompletedTasks(completed)
      setItems(inv)
      setActiveEvent(abnormal)
    }).catch(console.error)
  }, [])

  async function handleEmergency() {
    if (!window.confirm('긴급 호출을 발생시키겠습니까?')) return
    setEmergencyLoading(true)
    try {
      await triggerEmergency()
    } catch (err: unknown) {
      alert(err instanceof Error ? err.message : '긴급 호출 실패')
    } finally {
      setEmergencyLoading(false)
    }
  }

  function handleLogout() {
    logout()
    navigate('/nurse/login', { replace: true })
  }

  const battery = robot?.battery_percent ?? 0

  return (
    <div style={{ fontFamily: 'sans-serif', background: '#f4f6f8', minHeight: '100vh' }}>
      {/* Top bar */}
      <header style={{
        background: '#2c3e50', color: '#fff',
        padding: '0.6rem 1.5rem', display: 'flex',
        justifyContent: 'space-between', alignItems: 'center', flexWrap: 'wrap', gap: 8,
      }}>
        <strong style={{ fontSize: '1.1rem' }}>🏥 병원 물류 AMR — 간호사 대시보드</strong>

        <div style={{ display: 'flex', gap: 20, alignItems: 'center', fontSize: '0.9rem' }}>
          {/* Battery */}
          <span>
            🔋 <span style={{ color: BATTERY_COLOR(battery), fontWeight: 700 }}>
              {battery.toFixed(0)}%
            </span>
          </span>
          {/* Location */}
          <span>📍 현재 위치: {robot?.location_display ?? '알 수 없음'}</span>
          {/* Clock */}
          <span>🕐 {now.toLocaleTimeString('ko-KR')}</span>
          <span>📅 {now.toLocaleDateString('ko-KR')}</span>
        </div>

        <nav style={{ display: 'flex', gap: 10 }}>
          <Link to="/nurse/robot-dashboard" style={navBtn}>로봇 대시보드</Link>
          <Link to="/nurse/order"           style={navBtn}>오더 생성</Link>
          <button onClick={handleLogout}    style={{ ...navBtn, border: '1px solid #e74c3c', background: 'transparent', color: '#e74c3c', cursor: 'pointer' }}>
            로그아웃
          </button>
        </nav>
      </header>

      {/* Abnormal event banner */}
      <RobotStatusBanner />

      <main style={{ padding: '1.2rem 1.5rem', display: 'grid', gap: '1.2rem', gridTemplateColumns: '1fr 1fr' }}>

        {/* Inventory */}
        <section style={card}>
          <h3 style={cardTitle}>재고 현황 (Inventory Status)</h3>
          <InventoryTable items={inventory} />
        </section>

        {/* Ongoing tasks */}
        <section style={card}>
          <h3 style={cardTitle}>진행 중인 작업 ({ongoingTasks.length})</h3>
          {ongoingTasks.length === 0
            ? <p style={{ color: '#888' }}>진행 중인 작업 없음</p>
            : ongoingTasks.map((t) => <TaskCard key={t.id} task={t} />)
          }
        </section>

        {/* Completed tasks */}
        <section style={{ ...card, gridColumn: '1 / -1' }}>
          <h3 style={cardTitle}>완료된 작업</h3>
          {completedTasks.length === 0
            ? <p style={{ color: '#888' }}>완료된 작업 없음</p>
            : completedTasks.slice(0, 10).map((t) => <TaskCard key={t.id} task={t} compact />)
          }
        </section>
      </main>

      {/* Emergency button */}
      <div style={{ textAlign: 'center', padding: '0.5rem 1.5rem 2rem' }}>
        <button
          onClick={handleEmergency}
          disabled={emergencyLoading}
          style={{
            padding: '0.8rem 2.5rem', background: '#c0392b', color: '#fff',
            border: 'none', borderRadius: 8, fontSize: '1.1rem',
            fontWeight: 700, cursor: 'pointer', letterSpacing: 1,
          }}
        >
          🚨 {emergencyLoading ? '처리 중...' : '긴급 호출'}
        </button>
      </div>
    </div>
  )
}

const card:      React.CSSProperties = { background: '#fff', borderRadius: 8, padding: '1rem', boxShadow: '0 1px 4px rgba(0,0,0,0.08)' }
const cardTitle: React.CSSProperties = { margin: '0 0 0.8rem', fontSize: '1rem', fontWeight: 700, borderBottom: '2px solid #2980b9', paddingBottom: 6 }
const navBtn:    React.CSSProperties = { padding: '0.3rem 0.9rem', background: 'rgba(255,255,255,0.15)', color: '#fff', borderRadius: 4, textDecoration: 'none', fontSize: '0.85rem' }
