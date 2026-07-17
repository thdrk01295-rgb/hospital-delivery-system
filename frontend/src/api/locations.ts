import { publicApi } from './client'
import type { Location, BedSelectorMeta } from '@/types'

export function fetchLocations() {
  return publicApi.get<Location[]>('/locations')
}

export function fetchNonBedLocations() {
  return publicApi.get<Location[]>('/locations/non-bed')
}

export function fetchBedSelectorMeta() {
  return publicApi.get<BedSelectorMeta>('/locations/bed-selector-meta')
}
