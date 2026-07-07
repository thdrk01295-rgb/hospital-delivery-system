import type { RobotState } from '@/types'

/** Canonical Korean display labels per the v3 MQTT spec. */
export const ROBOT_STATE_LABELS: Record<RobotState, string> = {
  IDLE:              '대기 중',
  MOVING:            '이동 중',
  ARRIVED:           '도착',
  WAIT_UNLOCK:       '잠금 해제 대기',
  AUTH_SUCCESS:      '인증 완료',
  AUTH_FAIL:         '인증 실패',
  DELIVERY_OPEN_NUR: '의료진 작업함 열림',
  DELIVERY_OPEN_PAT: '환자 작업함 열림',
  COMPLETE:          '완료',
  LOW_BATTERY:       '배터리 부족',
  CHARGING_BATTERY:  '충전 중',
  ERROR:             '오류',
  EMERGENCY:         '비상 정지',
}

/** Background / text colours used in state panels. */
export const ROBOT_STATE_STYLE: Record<RobotState, { bg: string; color: string }> = {
  IDLE:              { bg: '#ecf0f1', color: '#2c3e50' },
  MOVING:            { bg: '#2980b9', color: '#fff'    },
  ARRIVED:           { bg: '#27ae60', color: '#fff'    },
  WAIT_UNLOCK:       { bg: '#f39c12', color: '#fff'    },
  AUTH_SUCCESS:      { bg: '#27ae60', color: '#fff'    },
  AUTH_FAIL:         { bg: '#c0392b', color: '#fff'    },
  DELIVERY_OPEN_NUR: { bg: '#8e44ad', color: '#fff'    },
  DELIVERY_OPEN_PAT: { bg: '#16a085', color: '#fff'    },
  COMPLETE:          { bg: '#27ae60', color: '#fff'    },
  LOW_BATTERY:       { bg: '#e67e22', color: '#fff'    },
  CHARGING_BATTERY:  { bg: '#95a5a6', color: '#fff'    },
  ERROR:             { bg: '#c0392b', color: '#fff'    },
  EMERGENCY:         { bg: '#7b241c', color: '#fff'    },
}
