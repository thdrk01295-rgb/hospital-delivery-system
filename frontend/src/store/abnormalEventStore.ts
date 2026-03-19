import { create } from 'zustand'
import type { AbnormalEvent, WsAbnormalEventUpdate } from '@/types'

interface AbnormalEventState {
  activeEvent: AbnormalEvent | null
  setActiveEvent: (event: AbnormalEvent | null) => void
  applyUpdate: (update: WsAbnormalEventUpdate) => void
}

export const useAbnormalEventStore = create<AbnormalEventState>((set) => ({
  activeEvent: null,

  setActiveEvent(event) {
    set({ activeEvent: event })
  },

  applyUpdate(update) {
    if (!update.active) {
      // Event of this type was resolved
      set((s) =>
        s.activeEvent?.event_type === update.event_type
          ? { activeEvent: null }
          : {},
      )
    } else {
      // New active event — store it as a minimal AbnormalEvent shape
      set({
        activeEvent: {
          id:              update.event_id ?? 0,
          event_type:      update.event_type,
          related_task_id: update.related_task_id ?? null,
          note:            update.note ?? null,
          occurred_at:     new Date().toISOString(),
          resolved_at:     null,
          is_active:       true,
        },
      })
    }
  },
}))
