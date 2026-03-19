/**
 * 3-step bed selector: floor → room → bed → auto-generates 5-digit bed code.
 * Used in the nurse order creation form for both origin and destination.
 */
import { useState } from 'react'
import type { BedSelectorMeta } from '@/types'

interface Props {
  meta: BedSelectorMeta
  onSelect: (locationCode: string) => void
  label?: string
}

export function BedSelector({ meta, onSelect, label = '침상 선택' }: Props) {
  const [floor, setFloor] = useState<number | null>(null)
  const [room,  setRoom]  = useState<number | null>(null)
  const [bed,   setBed]   = useState<number | null>(null)

  function handleFloor(f: number) {
    setFloor(f); setRoom(null); setBed(null)
  }

  function handleRoom(r: number) {
    setRoom(r); setBed(null)
  }

  function handleBed(b: number) {
    setBed(b)
    if (floor !== null && room !== null) {
      // Generate 5-digit bed code: floor + room + bed
      const code = `${floor}${room}${b}`
      onSelect(code)
    }
  }

  const rooms = floor ? (meta.rooms_by_floor[floor] ?? []) : []
  const beds  = room  ? Array.from({ length: meta.beds_per_room }, (_, i) => i + 1) : []

  const sel = { border: '1px solid #ccc', borderRadius: 4, padding: '0.3rem 0.5rem', marginRight: 6 }

  return (
    <fieldset style={{ border: '1px solid #ddd', borderRadius: 6, padding: '0.6rem 0.8rem' }}>
      <legend style={{ fontWeight: 600 }}>{label}</legend>
      <label>층
        <select value={floor ?? ''} onChange={(e) => handleFloor(Number(e.target.value))} style={sel}>
          <option value="">선택</option>
          {meta.floors.map((f) => <option key={f} value={f}>{f}층</option>)}
        </select>
      </label>
      {floor && (
        <label>호실
          <select value={room ?? ''} onChange={(e) => handleRoom(Number(e.target.value))} style={sel}>
            <option value="">선택</option>
            {rooms.map((r) => <option key={r} value={r}>{r}호</option>)}
          </select>
        </label>
      )}
      {room && (
        <label>침상
          <select value={bed ?? ''} onChange={(e) => handleBed(Number(e.target.value))} style={sel}>
            <option value="">선택</option>
            {beds.map((b) => <option key={b} value={b}>{b}번</option>)}
          </select>
        </label>
      )}
      {bed && floor && room && (
        <span style={{ marginLeft: 8, fontSize: '0.85rem', color: '#27ae60' }}>
          코드: {floor}{room}{bed}
        </span>
      )}
    </fieldset>
  )
}
