/**
 * Nurse order creation page — single form.
 *
 * Order Type dropdown → determines which location fields are shown.
 * Location fields:
 *   - Non-bed: dropdown of fixed locations (station, laundry, warehouse, …)
 *   - Bed: 3-step BedSelector → resolves to 5-digit code → looked up in locations
 * Order content (top/bottom/bedding/other): shown for clothes-related types.
 * Note: free-text additional memo.
 */
import { useState, useEffect, FormEvent } from 'react'
import { useNavigate, Link }     from 'react-router-dom'
import { fetchNonBedLocations, fetchBedSelectorMeta, fetchLocations } from '@/api/locations'
import { createNurseOrder }      from '@/api/tasks'
import { BedSelector }           from '@/components/BedSelector'
import type { Location, BedSelectorMeta, NurseOrderCreate, TaskType } from '@/types'

const ORDER_TYPES: { value: TaskType; label: string }[] = [
  { value: 'clothes_refill',          label: '의류 보충 (Clothes Refill)' },
  { value: 'kit_delivery',            label: '키트 배송 (Kit Delivery)' },
  { value: 'specimen_delivery',       label: '검체 배송 (Specimen Delivery)' },
  { value: 'logistics_delivery',      label: '물류 배송 (Logistics Delivery)' },
  { value: 'used_clothes_collection', label: '사용 의류 수거 (Used Clothes Collection)' },
  { value: 'battery_low',             label: '배터리 부족 (Battery Low)' },
]

// Nurse logistics task types that carry clothing quantity fields
const CLOTHES_TYPES: TaskType[] = ['clothes_refill', 'used_clothes_collection']

// Task types that do NOT require a destination (system/autonomous tasks)
const NO_DEST_REQUIRED: TaskType[] = ['emergency_call', 'battery_low']

type LocMode = 'fixed' | 'bed'

export function OrderCreate() {
  const navigate = useNavigate()

  const [taskType,    setTaskType]    = useState<TaskType>('kit_delivery')
  const [originMode,  setOriginMode]  = useState<LocMode>('fixed')
  const [destMode,    setDestMode]    = useState<LocMode>('fixed')
  const [originId,    setOriginId]    = useState<number | null>(null)
  const [destId,      setDestId]      = useState<number | null>(null)
  const [note,        setNote]        = useState('')
  const [orderTop,    setOrderTop]    = useState(0)
  const [orderBottom, setOrderBottom] = useState(0)
  const [orderBedding,setOrderBedding]= useState(0)
  const [orderOther,  setOrderOther]  = useState(0)
  const [submitting,  setSubmitting]  = useState(false)
  const [success,     setSuccess]     = useState(false)
  const [error,       setError]       = useState<string | null>(null)

  const [nonBedLocations, setNonBedLocations] = useState<Location[]>([])
  const [allLocations,    setAllLocations]    = useState<Location[]>([])
  const [bedMeta,         setBedMeta]         = useState<BedSelectorMeta | null>(null)
  const [locLoadError,    setLocLoadError]    = useState<string | null>(null)

  useEffect(() => {
    fetchNonBedLocations()
      .then((locs) => { setNonBedLocations(locs); setLocLoadError(null) })
      .catch((err: Error) => setLocLoadError(`위치 목록 로드 실패: ${err.message}`))
    fetchBedSelectorMeta().then(setBedMeta).catch(console.error)
    fetchLocations().then(setAllLocations).catch(console.error)
  }, [])

  function bedCodeToLocationId(code: string): number | null {
    return allLocations.find((l) => l.location_code === code)?.id ?? null
  }

  const showClothes = CLOTHES_TYPES.includes(taskType)

  async function handleSubmit(e: FormEvent) {
    e.preventDefault()
    setError(null)

    // Client-side validation: destination is required for all non-system task types
    if (!NO_DEST_REQUIRED.includes(taskType) && destId === null) {
      if (destMode === 'bed') {
        setError('침상 목적지를 선택하세요. (목적지 침상이 아직 선택되지 않았습니다.)')
      } else {
        setError('목적지를 선택하세요.')
      }
      return
    }

    setSubmitting(true)
    const body: NurseOrderCreate = {
      task_type: taskType,
      origin_location_id:      originId      ?? undefined,
      destination_location_id: destId        ?? undefined,
      note:         note        || undefined,
      order_top:    showClothes ? orderTop    : undefined,
      order_bottom: showClothes ? orderBottom : undefined,
      order_bedding:showClothes ? orderBedding: undefined,
      order_other:  showClothes ? orderOther  : undefined,
    }
    try {
      await createNurseOrder(body)
      setSuccess(true)
      setTimeout(() => navigate('/nurse/dashboard'), 1500)
    } catch (err: unknown) {
      setError(err instanceof Error ? err.message : '오더 생성 실패')
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div style={{ fontFamily: 'sans-serif', background: '#f4f6f8', minHeight: '100vh' }}>
      <header style={{ background: '#2c3e50', color: '#fff', padding: '0.7rem 1.5rem', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <strong>오더 생성 (Order Create)</strong>
        <Link to="/nurse/dashboard" style={{ color: '#7fb3d3', textDecoration: 'none', fontSize: '0.9rem' }}>← 대시보드</Link>
      </header>

      <form onSubmit={handleSubmit} style={{ maxWidth: 620, margin: '2rem auto', background: '#fff', borderRadius: 10, padding: '1.5rem 2rem', boxShadow: '0 2px 8px rgba(0,0,0,0.08)' }}>
        <h3 style={{ margin: '0 0 1.2rem' }}>새 오더</h3>

        {/* Order Type */}
        <label style={lbl}>
          오더 유형 (Order Type)
          <select value={taskType} onChange={(e) => setTaskType(e.target.value as TaskType)} style={inp}>
            {ORDER_TYPES.map((o) => <option key={o.value} value={o.value}>{o.label}</option>)}
          </select>
        </label>

        {/* Location load error */}
        {locLoadError && (
          <p style={{ color: '#c0392b', background: '#fdf2f2', border: '1px solid #e74c3c', borderRadius: 5, padding: '0.5rem 0.75rem', marginBottom: '1rem' }}>
            {locLoadError}
          </p>
        )}

        {/* Origin Location */}
        <fieldset style={fs}>
          <legend style={fsLeg}>출발지 (Origin Location)</legend>
          <div style={{ marginBottom: 8 }}>
            <label style={{ marginRight: 16 }}>
              <input type="radio" value="fixed" checked={originMode === 'fixed'} onChange={() => setOriginMode('fixed')} /> 고정 위치
            </label>
            <label>
              <input type="radio" value="bed" checked={originMode === 'bed'} onChange={() => setOriginMode('bed')} /> 침상 (Bed)
            </label>
          </div>
          {originMode === 'fixed' ? (
            <select value={originId ?? ''} onChange={(e) => setOriginId(e.target.value ? Number(e.target.value) : null)} style={inp}>
              <option value="">선택 안 함</option>
              {nonBedLocations.map((l) => <option key={l.id} value={l.id}>{l.display_name}</option>)}
            </select>
          ) : bedMeta ? (
            <BedSelector meta={bedMeta} onSelect={(code) => setOriginId(bedCodeToLocationId(code))} label="출발 침상" />
          ) : <p>로딩 중...</p>}
        </fieldset>

        {/* Destination Location */}
        <fieldset style={fs}>
          <legend style={fsLeg}>목적지 (Destination Location)</legend>
          <div style={{ marginBottom: 8 }}>
            <label style={{ marginRight: 16 }}>
              <input type="radio" value="fixed" checked={destMode === 'fixed'} onChange={() => setDestMode('fixed')} /> 고정 위치
            </label>
            <label>
              <input type="radio" value="bed" checked={destMode === 'bed'} onChange={() => setDestMode('bed')} /> 침상 (Bed)
            </label>
          </div>
          {destMode === 'fixed' ? (
            <select value={destId ?? ''} onChange={(e) => setDestId(e.target.value ? Number(e.target.value) : null)} style={inp}>
              <option value="">선택 안 함</option>
              {nonBedLocations.map((l) => <option key={l.id} value={l.id}>{l.display_name}</option>)}
            </select>
          ) : bedMeta ? (
            <BedSelector meta={bedMeta} onSelect={(code) => setDestId(bedCodeToLocationId(code))} label="목적지 침상" />
          ) : <p>로딩 중...</p>}
        </fieldset>

        {/* Order content — clothes only */}
        {showClothes && (
          <fieldset style={fs}>
            <legend style={fsLeg}>오더 내용 (Order Content)</legend>
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10 }}>
              {([['order_top', '상의 (Top)', orderTop, setOrderTop],
                 ['order_bottom', '하의 (Bottom)', orderBottom, setOrderBottom],
                 ['order_bedding', '침구 (Bedding)', orderBedding, setOrderBedding],
                 ['order_other', '기타 (Other)', orderOther, setOrderOther]] as const).map(([key, label, val, setter]) => (
                <label key={key} style={{ ...lbl, marginBottom: 0 }}>
                  {label}
                  <input type="number" min={0} value={val} onChange={(e) => setter(Number(e.target.value))} style={inp} />
                </label>
              ))}
            </div>
          </fieldset>
        )}

        {/* Additional note */}
        <label style={lbl}>
          추가 메모 (Additional Note)
          <textarea value={note} onChange={(e) => setNote(e.target.value)} rows={2} style={{ ...inp, resize: 'vertical' }} />
        </label>

        {error   && <p style={{ color: '#c0392b' }}>{error}</p>}
        {success && <p style={{ color: '#27ae60' }}>오더가 생성되었습니다. 이동 중...</p>}

        <button
          type="submit"
          disabled={submitting || success}
          style={{ width: '100%', padding: '0.75rem', background: '#2980b9', color: '#fff', border: 'none', borderRadius: 7, fontSize: '1rem', fontWeight: 600, cursor: 'pointer', marginTop: 8 }}
        >
          {submitting ? '생성 중...' : '오더 생성'}
        </button>
      </form>
    </div>
  )
}

const lbl: React.CSSProperties = { display: 'flex', flexDirection: 'column', marginBottom: '1rem', fontWeight: 600, fontSize: '0.9rem', color: '#333', gap: 4 }
const inp: React.CSSProperties = { padding: '0.45rem 0.6rem', border: '1px solid #ccc', borderRadius: 5, fontSize: '0.95rem', marginTop: 2 }
const fs:  React.CSSProperties = { border: '1px solid #ddd', borderRadius: 7, padding: '0.8rem 1rem', marginBottom: '1rem' }
const fsLeg: React.CSSProperties = { fontWeight: 700, fontSize: '0.88rem', color: '#555', padding: '0 4px' }
