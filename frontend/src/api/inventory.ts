import { api } from './client'
import type { InventoryItem, RobotInventory } from '@/types'

export function fetchInventory() {
  return api.get<InventoryItem[]>('/inventory')
}

export function fetchRobotInventory() {
  return api.get<RobotInventory>('/inventory/robot')
}
