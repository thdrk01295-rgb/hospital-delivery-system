import type { RobotInventory } from '@/types'

interface Props {
  inventory: RobotInventory | null
}

export function RobotInventoryPanel({ inventory }: Props) {
  if (!inventory) {
    return (
      <div style={empty}>재고 정보 없음</div>
    )
  }

  const rows: { label: string; count: number; capacity: number }[] = [
    { label: '키트',    count: inventory.kit_count,            capacity: inventory.kit_capacity },
    { label: '상의',    count: inventory.clothes_top_count,    capacity: inventory.clothes_top_capacity },
    { label: '하의',    count: inventory.clothes_bottom_count, capacity: inventory.clothes_bottom_capacity },
  ]

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: '0.55rem' }}>
      {rows.map(({ label, count, capacity }) => {
        const pct = capacity > 0 ? (count / capacity) * 100 : 0
        const barColor = pct > 50 ? '#10b981' : pct > 20 ? '#f59e0b' : '#ef4444'
        return (
          <div key={label}>
            <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '0.2rem' }}>
              <span style={labelStyle}>{label}</span>
              <span style={{ fontSize: '0.78rem', fontWeight: 700, color: barColor }}>
                {count} / {capacity}
              </span>
            </div>
            <div style={track}>
              <div style={{ ...fill, width: `${pct}%`, background: barColor }} />
            </div>
          </div>
        )
      })}
    </div>
  )
}

const empty: React.CSSProperties = {
  fontSize: '0.82rem',
  color: '#94a3b8',
}

const labelStyle: React.CSSProperties = {
  fontSize: '0.78rem',
  color: '#64748b',
}

const track: React.CSSProperties = {
  height: 6,
  background: '#e2e8f0',
  borderRadius: 4,
  overflow: 'hidden',
}

const fill: React.CSSProperties = {
  height: '100%',
  borderRadius: 4,
  transition: 'width 0.3s ease',
}
