// ── Robot ────────────────────────────────────────────────────────────────────

export type RobotState =
  | 'IDLE'
  | 'MOVING'
  | 'ARRIVED'
  | 'WAIT_NFC'
  | 'AUTH_SUCCESS'
  | 'AUTH_FAIL'
  | 'DELIVERY_OPEN_NUR'
  | 'DELIVERY_OPEN_PAT'
  | 'COMPLETE'
  | 'LOW_BATTERY'
  | 'CHAGING_BATTERY'  // spec spelling preserved
  | 'ERROR'

export interface RobotStatus {
  id: number
  robot_code: string
  current_state: RobotState
  battery_percent: number
  last_seen_at: string | null
  location_display: string | null
  current_location: LocationSummary | null
}

export interface LocationSummary {
  id: number
  location_code: string
  display_name: string
}

// ── Location ─────────────────────────────────────────────────────────────────

export type LocationType =
  | 'bed'
  | 'station'
  | 'laundry'
  | 'warehouse'
  | 'specimen_lab'
  | 'exam_A'
  | 'exam_B'
  | 'exam_C'

export interface Location {
  id: number
  location_code: string
  location_type: LocationType
  floor: number | null
  room: number | null
  bed: number | null
  display_name: string
  is_active: boolean
}

export interface BedSelectorMeta {
  floors: number[]
  rooms_by_floor: Record<number, number[]>
  beds_per_room: number
}

// ── Task ─────────────────────────────────────────────────────────────────────

export type TaskType =
  | 'clothes_refill'
  | 'kit_delivery'
  | 'specimen_delivery'
  | 'logistics_delivery'
  | 'used_clothes_collection'
  | 'battery_low'
  | 'emergency_call'
  | 'patient_clothes_rental'
  | 'patient_clothes_return'

export type TaskStatus =
  | 'PENDING'
  | 'DISPATCHED'
  | 'IN_PROGRESS'
  | 'COMPLETE'
  | 'CANCELLED'
  | 'FAILED'

export type RequestedByRole = 'nurse' | 'patient' | 'system'

export interface Task {
  id: number
  task_type: TaskType
  status: TaskStatus
  priority: number
  requested_by_role: RequestedByRole
  requested_by_user: string | null
  patient_bed_code: string | null
  origin_location: Location | null
  destination_location: Location | null
  note: string | null
  order_top: number | null
  order_bottom: number | null
  order_bedding: number | null
  order_other: number | null
  assigned_robot_id: number | null
  created_at: string
  started_at: string | null
  completed_at: string | null
  updated_at: string
}

// ── Nurse order form ─────────────────────────────────────────────────────────

export interface NurseOrderCreate {
  task_type: TaskType
  origin_location_id?: number
  destination_location_id?: number
  note?: string
  order_top?: number
  order_bottom?: number
  order_bedding?: number
  order_other?: number
}

// ── Patient request form ─────────────────────────────────────────────────────

export interface PatientClothingRequestCreate {
  task_type: 'patient_clothes_rental' | 'patient_clothes_return'
  note?: string
  order_top?: number
  order_bottom?: number
  order_bedding?: number
  order_other?: number
}

// ── Inventory ────────────────────────────────────────────────────────────────

export interface InventoryItem {
  id: number
  location: Location
  clean_count: number
  used_count: number
  updated_at: string
}

// ── Abnormal events ──────────────────────────────────────────────────────────

export type AbnormalEventType = 'error' | 'low_battery' | 'emergency_call'

export interface AbnormalEvent {
  id: number
  event_type: AbnormalEventType
  related_task_id: number | null
  note: string | null
  occurred_at: string
  resolved_at: string | null
  is_active: boolean
}

// ── Auth ─────────────────────────────────────────────────────────────────────

export type UserRole = 'nurse' | 'patient'

export interface TokenPayload {
  access_token: string
  token_type: string
  role: UserRole
  bed_code: string | null
}

// ── WebSocket events ─────────────────────────────────────────────────────────

export type WsEventType =
  | 'robot_state_update'
  | 'robot_location_update'
  | 'robot_battery_update'
  | 'task_status_update'
  | 'inventory_update'
  | 'abnormal_event_update'

export interface WsMessage<T = unknown> {
  event: WsEventType
  data: T
}

export interface WsInventoryUpdate {
  location_id: number
  clean_count: number
  used_count: number
}

export interface WsAbnormalEventUpdate {
  event_type: AbnormalEventType
  event_id?: number
  active: boolean
  note?: string
  related_task_id?: number
}

// ── Patient screen mode ───────────────────────────────────────────────────────
// Determined by PatientScreenController based on active task + robot state.

export type PatientScreenMode =
  | 'SERVICE_SELECTION'   // no active task, or initial state
  | 'RENTAL_CONFIRM'      // task=rental, status PENDING/DISPATCHED/IN_PROGRESS
  | 'RETURN_CONFIRM'      // task=return,  status PENDING/DISPATCHED/IN_PROGRESS
  | 'ROBOT_ARRIVED'       // robot state = ARRIVED
  | 'DELIVERY_ACTION'     // robot state = DELIVERY_OPEN_PAT
  | 'COMPLETE'            // robot state = COMPLETE (auto-resets after 3s)
  | 'ROBOT_STATUS'        // robot state = ERROR | LOW_BATTERY | CHAGING_BATTERY
