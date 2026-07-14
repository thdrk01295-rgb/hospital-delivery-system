#include "sensor_components/collision_guard_policy.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <string>

#include <gtest/gtest.h>

namespace sensor_components
{
namespace
{

SensorSnapshot sensor(const std::string & name, RangeState state, double distance = 0.0)
{
  const double stored_distance =
    state == RangeState::VALID ? distance : std::numeric_limits<double>::quiet_NaN();
  return {name, RangeClassification{state, stored_distance, "test"}};
}

std::array<SensorSnapshot, kSensorCount> clearSensors()
{
  return {
    sensor("front_left", RangeState::CLEAR),
    sensor("front_right", RangeState::CLEAR),
    sensor("rear_left", RangeState::CLEAR),
    sensor("rear_right", RangeState::CLEAR),
    sensor("rear_center", RangeState::CLEAR)};
}

GuardConfig config()
{
  return GuardConfig{};
}

TEST(CollisionGuardPolicyTest, RearCenterValidAndRearPairInvalidAllowsReverse)
{
  auto sensors = clearSensors();
  sensors[static_cast<std::size_t>(SensorIndex::kRearCenter)] =
    sensor("rear_center", RangeState::VALID, 0.76);
  sensors[static_cast<std::size_t>(SensorIndex::kRearLeft)] =
    sensor("rear_left", RangeState::INVALID);
  sensors[static_cast<std::size_t>(SensorIndex::kRearRight)] =
    sensor("rear_right", RangeState::INVALID);

  const auto decision = evaluateCollisionGuard(-0.05, 0.0, sensors, config());

  EXPECT_DOUBLE_EQ(decision.linear_x, -0.05);
  EXPECT_FALSE(decision.status.blocked);
  EXPECT_EQ(decision.status.action, "pass");
  EXPECT_EQ(decision.status.reason, "reverse_clear");
  EXPECT_EQ(decision.status.active_sensor, "rear_center");
}

TEST(CollisionGuardPolicyTest, AllRearClearAllowsReverse)
{
  const auto decision = evaluateCollisionGuard(-0.05, 0.0, clearSensors(), config());

  EXPECT_DOUBLE_EQ(decision.linear_x, -0.05);
  EXPECT_FALSE(decision.status.blocked);
  EXPECT_EQ(decision.status.reason, "reverse_clear");
  EXPECT_TRUE(std::isnan(decision.status.distance));
}

TEST(CollisionGuardPolicyTest, AllRearInvalidStopsReverse)
{
  auto sensors = clearSensors();
  sensors[static_cast<std::size_t>(SensorIndex::kRearCenter)] =
    sensor("rear_center", RangeState::INVALID);
  sensors[static_cast<std::size_t>(SensorIndex::kRearLeft)] =
    sensor("rear_left", RangeState::INVALID);
  sensors[static_cast<std::size_t>(SensorIndex::kRearRight)] =
    sensor("rear_right", RangeState::INVALID);

  const auto decision = evaluateCollisionGuard(-0.05, 0.0, sensors, config());

  EXPECT_DOUBLE_EQ(decision.linear_x, 0.0);
  EXPECT_TRUE(decision.status.blocked);
  EXPECT_EQ(decision.status.reason, "reverse_all_invalid_stop");
}

TEST(CollisionGuardPolicyTest, NearestRearValidAtStopDistanceStopsReverse)
{
  auto sensors = clearSensors();
  sensors[static_cast<std::size_t>(SensorIndex::kRearCenter)] =
    sensor("rear_center", RangeState::VALID, 0.50);
  sensors[static_cast<std::size_t>(SensorIndex::kRearLeft)] =
    sensor("rear_left", RangeState::VALID, 0.20);

  const auto decision = evaluateCollisionGuard(-0.05, 0.0, sensors, config());

  EXPECT_DOUBLE_EQ(decision.linear_x, 0.0);
  EXPECT_TRUE(decision.status.blocked);
  EXPECT_EQ(decision.status.reason, "reverse_stop");
  EXPECT_EQ(decision.status.active_sensor, "rear_left");
}

TEST(CollisionGuardPolicyTest, NearestRearValidInSlowBandLimitsReverse)
{
  auto sensors = clearSensors();
  sensors[static_cast<std::size_t>(SensorIndex::kRearRight)] =
    sensor("rear_right", RangeState::VALID, 0.30);

  const auto decision = evaluateCollisionGuard(-0.08, 0.0, sensors, config());

  EXPECT_DOUBLE_EQ(decision.linear_x, -0.03);
  EXPECT_FALSE(decision.status.blocked);
  EXPECT_EQ(decision.status.action, "slow");
  EXPECT_EQ(decision.status.reason, "reverse_slow");
}

TEST(CollisionGuardPolicyTest, OneLeftRotateSensorValidOneInvalidUsesValidSensor)
{
  auto sensors = clearSensors();
  sensors[static_cast<std::size_t>(SensorIndex::kFrontLeft)] =
    sensor("front_left", RangeState::VALID, 0.35);
  sensors[static_cast<std::size_t>(SensorIndex::kRearLeft)] =
    sensor("rear_left", RangeState::INVALID);

  const auto decision = evaluateCollisionGuard(0.0, 0.10, sensors, config());

  EXPECT_DOUBLE_EQ(decision.angular_z, 0.10);
  EXPECT_FALSE(decision.status.blocked);
  EXPECT_EQ(decision.status.reason, "rotate_left_clear");
  EXPECT_EQ(decision.status.active_sensor, "front_left");
}

TEST(CollisionGuardPolicyTest, BothLeftRotateSensorsClearAllowsRotate)
{
  const auto decision = evaluateCollisionGuard(0.0, 0.10, clearSensors(), config());

  EXPECT_DOUBLE_EQ(decision.angular_z, 0.10);
  EXPECT_FALSE(decision.status.blocked);
  EXPECT_EQ(decision.status.reason, "rotate_left_clear");
}

TEST(CollisionGuardPolicyTest, BothLeftRotateSensorsInvalidStopsRotate)
{
  auto sensors = clearSensors();
  sensors[static_cast<std::size_t>(SensorIndex::kFrontLeft)] =
    sensor("front_left", RangeState::INVALID);
  sensors[static_cast<std::size_t>(SensorIndex::kRearLeft)] =
    sensor("rear_left", RangeState::INVALID);

  const auto decision = evaluateCollisionGuard(0.0, 0.10, sensors, config());

  EXPECT_DOUBLE_EQ(decision.angular_z, 0.0);
  EXPECT_TRUE(decision.status.blocked);
  EXPECT_EQ(decision.status.reason, "rotate_left_all_invalid_stop");
}

TEST(CollisionGuardPolicyTest, OneRightRotateSensorValidOneInvalidUsesValidSensor)
{
  auto sensors = clearSensors();
  sensors[static_cast<std::size_t>(SensorIndex::kFrontRight)] =
    sensor("front_right", RangeState::VALID, 0.35);
  sensors[static_cast<std::size_t>(SensorIndex::kRearRight)] =
    sensor("rear_right", RangeState::INVALID);

  const auto decision = evaluateCollisionGuard(0.0, -0.10, sensors, config());

  EXPECT_DOUBLE_EQ(decision.angular_z, -0.10);
  EXPECT_FALSE(decision.status.blocked);
  EXPECT_EQ(decision.status.reason, "rotate_right_clear");
  EXPECT_EQ(decision.status.active_sensor, "front_right");
}

TEST(CollisionGuardPolicyTest, BothRightRotateSensorsClearAllowsRotate)
{
  const auto decision = evaluateCollisionGuard(0.0, -0.10, clearSensors(), config());

  EXPECT_DOUBLE_EQ(decision.angular_z, -0.10);
  EXPECT_FALSE(decision.status.blocked);
  EXPECT_EQ(decision.status.reason, "rotate_right_clear");
}

TEST(CollisionGuardPolicyTest, BothRightRotateSensorsInvalidStopsRotate)
{
  auto sensors = clearSensors();
  sensors[static_cast<std::size_t>(SensorIndex::kFrontRight)] =
    sensor("front_right", RangeState::INVALID);
  sensors[static_cast<std::size_t>(SensorIndex::kRearRight)] =
    sensor("rear_right", RangeState::INVALID);

  const auto decision = evaluateCollisionGuard(0.0, -0.10, sensors, config());

  EXPECT_DOUBLE_EQ(decision.angular_z, 0.0);
  EXPECT_TRUE(decision.status.blocked);
  EXPECT_EQ(decision.status.reason, "rotate_right_all_invalid_stop");
}

TEST(CollisionGuardPolicyTest, AboveMaxRangeIsClearNotInvalid)
{
  const auto classified = classifyRange(1.21, 0.03, 1.2, 8.0);

  EXPECT_EQ(classified.state, RangeState::CLEAR);
  EXPECT_EQ(classified.reason, "above_max");
}

TEST(CollisionGuardPolicyTest, StaleSampleIsInvalid)
{
  const auto classified = classifySensorSample(true, 0.51, 0.40, config());

  EXPECT_EQ(classified.state, RangeState::INVALID);
  EXPECT_EQ(classified.reason, "timeout");
}

TEST(CollisionGuardPolicyTest, NanIsInvalid)
{
  const auto classified =
    classifyRange(std::numeric_limits<double>::quiet_NaN(), 0.03, 1.2, 8.0);

  EXPECT_EQ(classified.state, RangeState::INVALID);
  EXPECT_EQ(classified.reason, "nan");
}

TEST(CollisionGuardPolicyTest, SentinelRangeIsInvalid)
{
  const auto classified = classifyRange(8.0, 0.03, 1.2, 8.0);

  EXPECT_EQ(classified.state, RangeState::INVALID);
  EXPECT_EQ(classified.reason, "sentinel");
}

TEST(CollisionGuardPolicyTest, PositiveInfinityIsClear)
{
  const auto classified =
    classifyRange(std::numeric_limits<double>::infinity(), 0.03, 1.2, 8.0);

  EXPECT_EQ(classified.state, RangeState::CLEAR);
  EXPECT_EQ(classified.reason, "out_of_range");
}

TEST(CollisionGuardPolicyTest, AngularSignMapsToLeftAndRight)
{
  auto left = evaluateCollisionGuard(0.0, 0.10, clearSensors(), config());
  auto right = evaluateCollisionGuard(0.0, -0.10, clearSensors(), config());

  EXPECT_EQ(left.status.mode, "rotate_left");
  EXPECT_EQ(right.status.mode, "rotate_right");
}

}  // namespace
}  // namespace sensor_components
