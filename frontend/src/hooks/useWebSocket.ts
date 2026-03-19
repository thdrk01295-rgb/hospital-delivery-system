/**
 * Opens a WebSocket connection to /ws and routes incoming events to stores.
 * Mount once at the app root after login; unmounts on logout.
 *
 * Patient screen note: robot_state_update is dispatched to robotStore for all
 * clients. The PatientScreenController is responsible for deciding whether the
 * robot state is relevant to this patient (based on whether an active task exists).
 */
import { useEffect, useRef } from 'react'
import type { WsMessage, RobotStatus, Task, WsInventoryUpdate, WsAbnormalEventUpdate } from '@/types'
import { useRobotStore }         from '@/store/robotStore'
import { useTaskStore }           from '@/store/taskStore'
import { useInventoryStore }      from '@/store/inventoryStore'
import { useAbnormalEventStore }  from '@/store/abnormalEventStore'

const WS_URL = `${window.location.protocol === 'https:' ? 'wss' : 'ws'}://${window.location.host}/ws`
const RECONNECT_DELAY_MS = 3000

export function useWebSocket(enabled: boolean) {
  const wsRef = useRef<WebSocket | null>(null)

  const setRobot       = useRobotStore((s) => s.setRobot)
  const updateRobot    = useRobotStore((s) => s.updateState)
  const applyTask      = useTaskStore((s) => s.applyTaskUpdate)
  const applyInventory = useInventoryStore((s) => s.applyUpdate)
  const applyAbnormal  = useAbnormalEventStore((s) => s.applyUpdate)

  useEffect(() => {
    if (!enabled) return

    let cancelled = false

    function connect() {
      if (cancelled) return
      const ws = new WebSocket(WS_URL)
      wsRef.current = ws

      ws.onmessage = (e: MessageEvent) => {
        try {
          const msg = JSON.parse(e.data) as WsMessage
          switch (msg.event) {
            case 'robot_state_update':
            case 'robot_location_update':
            case 'robot_battery_update':
              updateRobot(msg.data as Partial<RobotStatus>)
              break
            case 'task_status_update':
              applyTask(msg.data as Task)
              break
            case 'inventory_update':
              applyInventory(msg.data as WsInventoryUpdate)
              break
            case 'abnormal_event_update':
              applyAbnormal(msg.data as WsAbnormalEventUpdate)
              break
          }
        } catch {
          // ignore malformed messages
        }
      }

      ws.onclose = () => {
        if (!cancelled) setTimeout(connect, RECONNECT_DELAY_MS)
      }

      ws.onerror = () => {
        ws.close()
      }
    }

    connect()

    return () => {
      cancelled = true
      wsRef.current?.close()
    }
  }, [enabled])
}
