#ifndef SENSOR_COMPONENTS_COLLISION_GUARD_POLICY_HPP_
#define SENSOR_COMPONENTS_COLLISION_GUARD_POLICY_HPP_

#include <array>
#include <cstddef>
#include <string>

namespace sensor_components
{

enum class SensorIndex : std::size_t
{
  kFrontLeft = 0,
  kFrontRight = 1,
  kRearLeft = 2,
  kRearRight = 3,
  kRearCenter = 4,
};

constexpr std::size_t kSensorCount = 5;

enum class RangeState
{
  VALID,
  CLEAR,
  INVALID
};

struct GuardConfig
{
  double min_range{0.03};
  double max_valid_range{1.2};
  double sentinel_invalid_range{8.0};
  double sensor_timeout_sec{0.50};
  double linear_deadband{0.01};
  double reverse_deadband{0.01};
  double rotate_deadband{0.05};
  double reverse_stop_distance{0.22};
  double reverse_slow_distance{0.38};
  double side_stop_distance{0.14};
  double side_slow_distance{0.25};
  double rotate_stop_distance{0.16};
  double rotate_slow_distance{0.28};
  double reverse_limited_speed_abs{0.03};
  double rotate_limited_speed_abs{0.10};
};

struct RangeClassification
{
  RangeState state{RangeState::INVALID};
  double distance;
  std::string reason;
};

struct SensorSnapshot
{
  std::string name;
  RangeClassification range;
};

struct GuardStatus
{
  std::string mode;
  std::string action;
  bool blocked;
  std::string active_sensor;
  double distance;
  std::string reason;
  std::string sensor_states;
};

struct MotionDecision
{
  double linear_x;
  double angular_z;
  GuardStatus status;
};

RangeClassification classifyRange(
  double range,
  double min_range,
  double max_valid_range,
  double sentinel_invalid_range);

RangeClassification classifySensorSample(
  bool has_sample,
  double age_sec,
  double range,
  const GuardConfig & config);

MotionDecision evaluateCollisionGuard(
  double linear_x,
  double angular_z,
  const std::array<SensorSnapshot, kSensorCount> & sensors,
  const GuardConfig & config);

const char * rangeStateName(RangeState state);

}  // namespace sensor_components

#endif  // SENSOR_COMPONENTS_COLLISION_GUARD_POLICY_HPP_
