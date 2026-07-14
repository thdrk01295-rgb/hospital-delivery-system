#include "sensor_components/collision_guard_policy.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>

namespace sensor_components
{
namespace
{
constexpr double kUnknownDistance = std::numeric_limits<double>::quiet_NaN();
constexpr double kRangeEpsilon = 1.0e-6;

struct GroupEvaluation
{
  bool has_valid{false};
  bool all_invalid{false};
  double nearest_distance{kUnknownDistance};
  std::string nearest_sensor;
  std::string selected_sensors;
};

std::string joinSensorNames(
  const std::array<SensorSnapshot, kSensorCount> & sensors,
  const std::array<SensorIndex, 3> & indices,
  std::size_t count)
{
  std::string result;
  for (std::size_t i = 0; i < count; ++i) {
    if (!result.empty()) {
      result += ",";
    }
    result += sensors[static_cast<std::size_t>(indices[i])].name;
  }
  return result;
}

std::string sensorStateSummary(const std::array<SensorSnapshot, kSensorCount> & sensors)
{
  std::string summary;
  for (const auto & sensor : sensors) {
    if (!summary.empty()) {
      summary += ",";
    }
    summary += sensor.name;
    summary += "=";
    summary += rangeStateName(sensor.range.state);
  }
  return summary;
}

GroupEvaluation evaluateGroup(
  const std::array<SensorSnapshot, kSensorCount> & sensors,
  const std::array<SensorIndex, 3> & indices,
  std::size_t count)
{
  GroupEvaluation result;
  result.selected_sensors = joinSensorNames(sensors, indices, count);
  std::size_t invalid_count = 0;

  for (std::size_t i = 0; i < count; ++i) {
    const auto index = static_cast<std::size_t>(indices[i]);
    const auto & sensor = sensors[index];
    if (sensor.range.state == RangeState::INVALID) {
      ++invalid_count;
      continue;
    }
    if (sensor.range.state != RangeState::VALID) {
      continue;
    }
    if (!result.has_valid || sensor.range.distance < result.nearest_distance) {
      result.has_valid = true;
      result.nearest_distance = sensor.range.distance;
      result.nearest_sensor = sensor.name;
    }
  }

  result.all_invalid = invalid_count == count;
  return result;
}

void setStatus(
  GuardStatus & status,
  const std::string & mode,
  const std::string & action,
  bool blocked,
  const std::string & active_sensor,
  double distance,
  const std::string & reason)
{
  status.mode = mode;
  status.action = action;
  status.blocked = blocked;
  status.active_sensor = active_sensor;
  status.distance = distance;
  status.reason = reason;
}

std::string reverseMode(double angular_z, const GuardConfig & config)
{
  if (angular_z > config.rotate_deadband) {
    return "reverse_turn_left";
  }
  if (angular_z < -config.rotate_deadband) {
    return "reverse_turn_right";
  }
  return "reverse";
}

void applyReverseGuard(
  MotionDecision & decision,
  const std::array<SensorSnapshot, kSensorCount> & sensors,
  const GuardConfig & config)
{
  const std::array<SensorIndex, 3> rear_sensors{
    SensorIndex::kRearCenter,
    SensorIndex::kRearLeft,
    SensorIndex::kRearRight};
  const auto group = evaluateGroup(sensors, rear_sensors, rear_sensors.size());
  const std::string mode = reverseMode(decision.angular_z, config);

  if (group.all_invalid) {
    decision.linear_x = 0.0;
    setStatus(
      decision.status, mode, "stop", true, group.selected_sensors, kUnknownDistance,
      mode + "_all_invalid_stop");
    return;
  }

  if (group.has_valid) {
    if (group.nearest_distance <= config.reverse_stop_distance) {
      decision.linear_x = 0.0;
      setStatus(
        decision.status, mode, "stop", true, group.nearest_sensor, group.nearest_distance,
        mode + "_stop");
      return;
    }
    if (group.nearest_distance <= config.reverse_slow_distance) {
      decision.linear_x = std::max(decision.linear_x, -config.reverse_limited_speed_abs);
      setStatus(
        decision.status, mode, "slow", false, group.nearest_sensor, group.nearest_distance,
        mode + "_slow");
      return;
    }
    setStatus(
      decision.status, mode, "pass", false, group.nearest_sensor, group.nearest_distance,
      mode + "_clear");
    return;
  }

  setStatus(
    decision.status, mode, "pass", false, group.selected_sensors, kUnknownDistance,
    mode + "_clear");
}

void applyRotateGuard(
  MotionDecision & decision,
  const std::array<SensorSnapshot, kSensorCount> & sensors,
  const GuardConfig & config)
{
  const bool rotate_left = decision.angular_z > 0.0;
  const std::array<SensorIndex, 3> side_sensors{
    rotate_left ? SensorIndex::kFrontLeft : SensorIndex::kFrontRight,
    rotate_left ? SensorIndex::kRearLeft : SensorIndex::kRearRight,
    SensorIndex::kRearCenter};
  const auto group = evaluateGroup(sensors, side_sensors, 2);
  const std::string mode = rotate_left ? "rotate_left" : "rotate_right";

  if (group.all_invalid) {
    decision.angular_z = 0.0;
    setStatus(
      decision.status, mode, "stop", true, group.selected_sensors, kUnknownDistance,
      mode + "_all_invalid_stop");
    return;
  }

  if (group.has_valid) {
    if (group.nearest_distance <= config.rotate_stop_distance) {
      decision.angular_z = 0.0;
      setStatus(
        decision.status, mode, "stop", true, group.nearest_sensor, group.nearest_distance,
        mode + "_stop");
      return;
    }
    if (group.nearest_distance <= config.rotate_slow_distance) {
      decision.angular_z = std::copysign(
        std::min(std::abs(decision.angular_z), config.rotate_limited_speed_abs),
        decision.angular_z);
      setStatus(
        decision.status, mode, "slow", false, group.nearest_sensor, group.nearest_distance,
        mode + "_slow");
      return;
    }
    setStatus(
      decision.status, mode, "pass", false, group.nearest_sensor, group.nearest_distance,
      mode + "_clear");
    return;
  }

  setStatus(
    decision.status, mode, "pass", false, group.selected_sensors, kUnknownDistance,
    mode + "_clear");
}

}  // namespace

const char * rangeStateName(RangeState state)
{
  switch (state) {
    case RangeState::VALID:
      return "VALID";
    case RangeState::CLEAR:
      return "CLEAR";
    case RangeState::INVALID:
      return "INVALID";
  }
  return "INVALID";
}

RangeClassification classifyRange(
  double range,
  double min_range,
  double max_valid_range,
  double sentinel_invalid_range)
{
  if (std::isnan(range)) {
    return {RangeState::INVALID, kUnknownDistance, "nan"};
  }
  if (std::isinf(range)) {
    if (range > 0.0) {
      return {RangeState::CLEAR, kUnknownDistance, "out_of_range"};
    }
    return {RangeState::INVALID, kUnknownDistance, "negative_infinity"};
  }
  if (range <= 0.0 || range + kRangeEpsilon < min_range) {
    return {RangeState::INVALID, range, "below_min"};
  }
  if (std::isfinite(sentinel_invalid_range) &&
    range + kRangeEpsilon >= sentinel_invalid_range)
  {
    return {RangeState::INVALID, range, "sentinel"};
  }
  if (range > max_valid_range + kRangeEpsilon) {
    return {RangeState::CLEAR, kUnknownDistance, "above_max"};
  }
  return {RangeState::VALID, range, "ok"};
}

RangeClassification classifySensorSample(
  bool has_sample,
  double age_sec,
  double range,
  const GuardConfig & config)
{
  if (!has_sample) {
    return {RangeState::INVALID, kUnknownDistance, "missing"};
  }
  if (age_sec > config.sensor_timeout_sec) {
    return {RangeState::INVALID, kUnknownDistance, "timeout"};
  }
  return classifyRange(
    range, config.min_range, config.max_valid_range, config.sentinel_invalid_range);
}

MotionDecision evaluateCollisionGuard(
  double linear_x,
  double angular_z,
  const std::array<SensorSnapshot, kSensorCount> & sensors,
  const GuardConfig & config)
{
  MotionDecision decision{
    linear_x,
    angular_z,
    GuardStatus{"idle", "pass", false, "none", kUnknownDistance, "ok", sensorStateSummary(sensors)}};

  const bool reversing = linear_x < -config.reverse_deadband;
  const bool in_place_rotating =
    std::abs(linear_x) < config.linear_deadband &&
    std::abs(angular_z) > config.rotate_deadband;

  if (in_place_rotating) {
    applyRotateGuard(decision, sensors, config);
  } else if (reversing) {
    applyReverseGuard(decision, sensors, config);
  } else if (std::abs(linear_x) >= config.linear_deadband) {
    decision.status.mode = "forward";
  }

  return decision;
}

}  // namespace sensor_components
