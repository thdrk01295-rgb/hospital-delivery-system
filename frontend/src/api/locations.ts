import { api } from './client'
import type { Location, BedSelectorMeta } from '@/types'

export function fetchLocations() {
  return api.get<Location[]>('/locations')
}

export function fetchNonBedLocations() {
  return api.get<Location[]>('/locations/non-bed')
}

export function fetchBedSelectorMeta() {
  return api.get<BedSelectorMeta>('/locations/bed-selector-meta')
}
