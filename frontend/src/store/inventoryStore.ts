import { create } from 'zustand'
import type { InventoryItem, RobotInventory, WsInventoryUpdate } from '@/types'

interface InventoryState {
  items: InventoryItem[]
  robotInventory: RobotInventory | null
  setItems: (items: InventoryItem[]) => void
  setRobotInventory: (inv: RobotInventory) => void
  applyUpdate: (update: WsInventoryUpdate) => void
}

export const useInventoryStore = create<InventoryState>((set) => ({
  items: [],
  robotInventory: null,

  setItems(items) {
    set({ items })
  },

  setRobotInventory(inv) {
    set({ robotInventory: inv })
  },

  applyUpdate(update) {
    if ('type' in update && update.type === 'robot') {
      set((s) => ({
        robotInventory: s.robotInventory
          ? {
              ...s.robotInventory,
              kit_count: update.kit_count,
              clothes_top_count: update.clothes_top_count,
              clothes_bottom_count: update.clothes_bottom_count,
            }
          : null,
      }))
    } else if ('location_id' in update) {
      set((s) => ({
        items: s.items.map((item) =>
          item.location.id === update.location_id
            ? { ...item, clean_count: update.clean_count, used_count: update.used_count }
            : item,
        ),
      }))
    }
  },
}))
