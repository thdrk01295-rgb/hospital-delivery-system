import { create } from 'zustand'
import type { InventoryItem, WsInventoryUpdate } from '@/types'

interface InventoryState {
  items: InventoryItem[]
  setItems: (items: InventoryItem[]) => void
  applyUpdate: (update: WsInventoryUpdate) => void
}

export const useInventoryStore = create<InventoryState>((set) => ({
  items: [],

  setItems(items) {
    set({ items })
  },

  applyUpdate(update) {
    set((s) => ({
      items: s.items.map((item) =>
        item.location.id === update.location_id
          ? { ...item, clean_count: update.clean_count, used_count: update.used_count }
          : item,
      ),
    }))
  },
}))
