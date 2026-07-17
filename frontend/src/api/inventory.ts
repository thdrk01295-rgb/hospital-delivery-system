import { publicApi } from './client'
import type { InventoryItem, RobotInventory } from '@/types'

export function fetchInventory() {
  return publicApi.get<InventoryItem[]>('/inventory')
}

export function fetchRobotInventory() {
  return publicApi.get<RobotInventory>('/inventory/robot')
}
