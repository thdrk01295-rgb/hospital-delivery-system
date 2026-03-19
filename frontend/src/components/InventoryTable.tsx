import type { InventoryItem } from '@/types'

interface Props {
  items: InventoryItem[]
}

export function InventoryTable({ items }: Props) {
  if (items.length === 0) {
    return <p style={{ color: '#888' }}>재고 정보 없음</p>
  }

  return (
    <table style={{ width: '100%', borderCollapse: 'collapse', fontSize: '0.9rem' }}>
      <thead>
        <tr style={{ background: '#f0f0f0' }}>
          <th style={th}>위치</th>
          <th style={th}>청결 의류</th>
          <th style={th}>사용 의류</th>
          <th style={th}>업데이트</th>
        </tr>
      </thead>
      <tbody>
        {items.map((item) => (
          <tr key={item.id} style={{ borderBottom: '1px solid #eee' }}>
            <td style={td}>{item.location.display_name}</td>
            <td style={{ ...td, color: item.clean_count <= 5 ? '#c0392b' : '#27ae60', fontWeight: 600 }}>
              {item.clean_count}
            </td>
            <td style={td}>{item.used_count}</td>
            <td style={{ ...td, color: '#aaa', fontSize: '0.8rem' }}>
              {new Date(item.updated_at).toLocaleTimeString('ko-KR')}
            </td>
          </tr>
        ))}
      </tbody>
    </table>
  )
}

const th: React.CSSProperties = { padding: '0.5rem 0.8rem', textAlign: 'left', fontWeight: 600 }
const td: React.CSSProperties = { padding: '0.4rem 0.8rem' }
