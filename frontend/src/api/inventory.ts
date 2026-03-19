import { api } from './client'
import type { InventoryItem } from '@/types'

export function fetchInventory() {
  return api.get<InventoryItem[]>('/inventory')
}
