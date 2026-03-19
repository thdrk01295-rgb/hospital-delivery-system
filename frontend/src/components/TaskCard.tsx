import type { Task } from '@/types'

const TYPE_LABELS: Record<string, string> = {
  clothes_refill:           '의류 보충',
  kit_delivery:             '키트 배송',
  specimen_delivery:        '검체 배송',
  logistics_delivery:       '물류 배송',
  used_clothes_collection:  '사용 의류 수거',
  battery_low:              '배터리 부족',
  emergency_call:           '긴급 호출',
  patient_clothes_rental:   '의류 대여',
  patient_clothes_return:   '의류 반납',
}

const STATUS_LABELS: Record<string, string> = {
  PENDING:     '대기 중',
  DISPATCHED:  '배정됨',
  IN_PROGRESS: '진행 중',
  COMPLETE:    '완료',
  CANCELLED:   '취소',
  FAILED:      '실패',
}

const STATUS_COLORS: Record<string, string> = {
  PENDING:     '#f39c12',
  DISPATCHED:  '#2980b9',
  IN_PROGRESS: '#27ae60',
  COMPLETE:    '#7f8c8d',
  CANCELLED:   '#95a5a6',
  FAILED:      '#c0392b',
}

interface Props {
  task: Task
  compact?: boolean
}

export function TaskCard({ task, compact }: Props) {
  const color = STATUS_COLORS[task.status] ?? '#999'

  return (
    <div style={{
      border: `1px solid ${color}`, borderLeft: `4px solid ${color}`,
      borderRadius: 6, padding: compact ? '0.5rem 0.8rem' : '0.8rem 1rem',
      marginBottom: '0.5rem', background: '#fff',
    }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <strong>{TYPE_LABELS[task.task_type] ?? task.task_type}</strong>
        <span style={{ fontSize: '0.8rem', color, fontWeight: 600 }}>
          {STATUS_LABELS[task.status] ?? task.status}
        </span>
      </div>
      {!compact && (
        <>
          {task.origin_location && (
            <div style={{ fontSize: '0.85rem', color: '#555', marginTop: 4 }}>
              출발지: {task.origin_location.display_name}
            </div>
          )}
          {task.destination_location && (
            <div style={{ fontSize: '0.85rem', color: '#555' }}>
              목적지: {task.destination_location.display_name}
            </div>
          )}
          {task.note && (
            <div style={{ fontSize: '0.8rem', color: '#888', marginTop: 4 }}>
              추가 메모: {task.note}
            </div>
          )}
          <div style={{ fontSize: '0.75rem', color: '#aaa', marginTop: 4 }}>
            {new Date(task.created_at).toLocaleString('ko-KR')}
          </div>
        </>
      )}
    </div>
  )
}
