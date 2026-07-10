import { useState, useEffect, FormEvent } from 'react'
import { fetchNonBedLocations, fetchBedSelectorMeta, fetchLocations } from '@/api/locations'
import { createNurseOrder }  from '@/api/tasks'
import { BedSelector }       from '@/components/BedSelector'
import type { Location, BedSelectorMeta, NurseOrderCreate, TaskType } from '@/types'

const ORDER_TYPES: { value: TaskType; label: string }[] = [
  { value: 'clothes_refill',          label: '의류 보충' },
  { value: 'kit_delivery',            label: '키트 배송' },
  { value: 'kit_refill',              label: '키트 보충' },
  { value: 'specimen_delivery',       label: '검체 배송' },
  { value: 'logistics_delivery',      label: '물류 배송' },
  { value: 'used_clothes_collection', label: '사용 의류 수거' },
]

const CLOTHES_TYPES: TaskType[] = ['clothes_refill', 'used_clothes_collection']
const NO_ORIGIN_TYPES: Set<TaskType> = new Set(['kit_delivery', 'kit_refill', 'clothes_refill'])
const AUTO_DEST_TYPES: Set<TaskType> = new Set(['kit_refill', 'clothes_refill'])
type LocMode = 'fixed' | 'bed'

export function TaskCreatePanel() {
  const [expanded, setExpanded] = useState(false)

  const [taskType,     setTaskType]     = useState<TaskType>('kit_delivery')
  const [originMode,   setOriginMode]   = useState<LocMode>('fixed')
  const [destMode,     setDestMode]     = useState<LocMode>('fixed')
  const [originId,     setOriginId]     = useState<number | null>(null)
  const [destId,       setDestId]       = useState<number | null>(null)
  const [note,         setNote]         = useState('')
  const [orderTop,     setOrderTop]     = useState(0)
  const [orderBottom,  setOrderBottom]  = useState(0)
  const [orderBedding, setOrderBedding] = useState(0)
  const [orderOther,   setOrderOther]   = useState(0)
  const [submitting,   setSubmitting]   = useState(false)
  const [success,      setSuccess]      = useState(false)
  const [error,        setError]        = useState<string | null>(null)

  const [nonBedLocations, setNonBedLocations] = useState<Location[]>([])
  const [allLocations,    setAllLocations]    = useState<Location[]>([])
  const [bedMeta,         setBedMeta]         = useState<BedSelectorMeta | null>(null)

  useEffect(() => {
    fetchNonBedLocations().then(setNonBedLocations).catch(console.error)
    fetchBedSelectorMeta().then(setBedMeta).catch(console.error)
    fetchLocations().then(setAllLocations).catch(console.error)
  }, [])

  function bedCodeToId(code: string): number | null {
    return allLocations.find((l) => l.location_code === code)?.id ?? null
  }

  const showClothes  = CLOTHES_TYPES.includes(taskType)
  const hideOrigin   = NO_ORIGIN_TYPES.has(taskType)
  const hideDestPick = AUTO_DEST_TYPES.has(taskType)

  function resetForm() {
    setTaskType('kit_delivery')
    setOriginMode('fixed'); setDestMode('fixed')
    setOriginId(null); setDestId(null)
    setNote('')
    setOrderTop(0); setOrderBottom(0); setOrderBedding(0); setOrderOther(0)
    setError(null)
  }

  async function handleSubmit(e: FormEvent) {
    e.preventDefault()
    setError(null)
    if (!hideDestPick && destId === null) {
      setError('목적지를 선택하세요.')
      return
    }
    setSubmitting(true)
    const body: NurseOrderCreate = {
      task_type:               taskType,
      origin_location_id:      hideOrigin   ? undefined : (originId ?? undefined),
      destination_location_id: hideDestPick ? undefined : (destId   ?? undefined),
      note:                    note          || undefined,
      order_top:     showClothes ? orderTop     : undefined,
      order_bottom:  showClothes ? orderBottom  : undefined,
      order_bedding: showClothes ? orderBedding : undefined,
      order_other:   showClothes ? orderOther   : undefined,
    }
    try {
      await createNurseOrder(body)
      setSuccess(true)
      setTimeout(() => {
        setSuccess(false)
        setExpanded(false)
        resetForm()
      }, 1500)
    } catch (err: unknown) {
      setError(err instanceof Error ? err.message : '오더 생성 실패')
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div style={panel}>
      <button onClick={() => setExpanded(!expanded)} style={headerBtn}>
        <span style={{ fontWeight: 700, fontSize: '0.82rem', color: '#1e3a5f' }}>
          + 새 오더 생성
        </span>
        <span style={{ color: '#94a3b8', fontSize: '0.75rem' }}>{expanded ? '▲' : '▼'}</span>
      </button>

      {expanded && (
        <form onSubmit={handleSubmit} style={{ padding: '0.9rem', display: 'flex', flexDirection: 'column', gap: '0.65rem' }}>

          <label style={lbl}>
            오더 유형
            <select value={taskType} onChange={(e) => setTaskType(e.target.value as TaskType)} style={sel}>
              {ORDER_TYPES.map((o) => <option key={o.value} value={o.value}>{o.label}</option>)}
            </select>
          </label>

          {!hideOrigin && (
            <div style={{ display: 'flex', flexDirection: 'column', gap: '0.3rem' }}>
              <span style={lbl}>출발지</span>
              <div style={{ display: 'flex', gap: '0.9rem', fontSize: '0.8rem', color: '#475569', marginBottom: 3 }}>
                <label><input type="radio" value="fixed" checked={originMode === 'fixed'} onChange={() => setOriginMode('fixed')} /> 고정</label>
                <label><input type="radio" value="bed"   checked={originMode === 'bed'}   onChange={() => setOriginMode('bed')}   /> 침상</label>
              </div>
              {originMode === 'fixed' ? (
                <select value={originId ?? ''} onChange={(e) => setOriginId(e.target.value ? Number(e.target.value) : null)} style={sel}>
                  <option value="">선택 안 함</option>
                  {nonBedLocations.map((l) => <option key={l.id} value={l.id}>{l.display_name}</option>)}
                </select>
              ) : bedMeta ? (
                <BedSelector meta={bedMeta} onSelect={(code) => setOriginId(bedCodeToId(code))} label="출발 침상" />
              ) : <span style={{ fontSize: '0.8rem', color: '#94a3b8' }}>로딩 중...</span>}
            </div>
          )}

          {!hideDestPick ? (
            <div style={{ display: 'flex', flexDirection: 'column', gap: '0.3rem' }}>
              <span style={lbl}>목적지</span>
              <div style={{ display: 'flex', gap: '0.9rem', fontSize: '0.8rem', color: '#475569', marginBottom: 3 }}>
                <label><input type="radio" value="fixed" checked={destMode === 'fixed'} onChange={() => setDestMode('fixed')} /> 고정</label>
                <label><input type="radio" value="bed"   checked={destMode === 'bed'}   onChange={() => setDestMode('bed')}   /> 침상</label>
              </div>
              {destMode === 'fixed' ? (
                <select value={destId ?? ''} onChange={(e) => setDestId(e.target.value ? Number(e.target.value) : null)} style={sel}>
                  <option value="">선택 안 함</option>
                  {nonBedLocations.map((l) => <option key={l.id} value={l.id}>{l.display_name}</option>)}
                </select>
              ) : bedMeta ? (
                <BedSelector meta={bedMeta} onSelect={(code) => setDestId(bedCodeToId(code))} label="목적지 침상" />
              ) : <span style={{ fontSize: '0.8rem', color: '#94a3b8' }}>로딩 중...</span>}
            </div>
          ) : (
            <div style={{ display: 'flex', flexDirection: 'column', gap: '0.3rem' }}>
              <span style={lbl}>목적지</span>
              <span style={{ fontSize: '0.8rem', color: '#64748b', padding: '0.35rem 0.5rem', background: '#f1f5f9', borderRadius: 5 }}>
                {taskType === 'kit_refill' ? '자동: WAREHOUSE-01 (창고)' : '자동: LAUNDRY-01 (세탁소)'}
              </span>
            </div>
          )}

          {showClothes && (
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '0.5rem' }}>
              <label style={lbl}>상의<input type="number" min={0} value={orderTop}     onChange={(e) => setOrderTop(Number(e.target.value))}     style={numInp} /></label>
              <label style={lbl}>하의<input type="number" min={0} value={orderBottom}  onChange={(e) => setOrderBottom(Number(e.target.value))}  style={numInp} /></label>
              <label style={lbl}>침구<input type="number" min={0} value={orderBedding} onChange={(e) => setOrderBedding(Number(e.target.value))} style={numInp} /></label>
              <label style={lbl}>기타<input type="number" min={0} value={orderOther}   onChange={(e) => setOrderOther(Number(e.target.value))}   style={numInp} /></label>
            </div>
          )}

          <label style={lbl}>
            메모
            <textarea value={note} onChange={(e) => setNote(e.target.value)} rows={2} style={{ ...sel, resize: 'vertical' }} />
          </label>

          {error   && <div style={{ color: '#dc2626', fontSize: '0.8rem' }}>{error}</div>}
          {success && <div style={{ color: '#059669', fontSize: '0.8rem' }}>오더가 생성되었습니다.</div>}

          <button
            type="submit"
            disabled={submitting || success}
            style={{
              padding: '0.5rem',
              background: submitting || success ? '#93c5fd' : '#1d6fb8',
              color: '#fff',
              border: 'none',
              borderRadius: 6,
              fontSize: '0.85rem',
              fontWeight: 700,
              cursor: submitting || success ? 'not-allowed' : 'pointer',
            }}
          >
            {submitting ? '생성 중...' : success ? '완료' : '오더 생성'}
          </button>
        </form>
      )}
    </div>
  )
}

const panel: React.CSSProperties = {
  background: '#fff',
  border: '1px solid #e2e8f0',
  borderRadius: 10,
  overflow: 'hidden',
  boxShadow: '0 1px 3px rgba(0,0,0,0.06)',
}

const headerBtn: React.CSSProperties = {
  width: '100%',
  padding: '0.75rem 1rem',
  display: 'flex',
  justifyContent: 'space-between',
  alignItems: 'center',
  background: '#f8fafc',
  border: 'none',
  borderBottom: '1px solid #e2e8f0',
  cursor: 'pointer',
}

const lbl: React.CSSProperties = {
  display: 'flex',
  flexDirection: 'column',
  fontSize: '0.78rem',
  fontWeight: 600,
  color: '#475569',
  gap: 3,
}

const sel: React.CSSProperties = {
  padding: '0.35rem 0.5rem',
  border: '1px solid #cbd5e1',
  borderRadius: 5,
  fontSize: '0.82rem',
  color: '#1e293b',
  background: '#fff',
}

const numInp: React.CSSProperties = {
  padding: '0.3rem 0.5rem',
  border: '1px solid #cbd5e1',
  borderRadius: 5,
  fontSize: '0.82rem',
  width: '100%',
}
