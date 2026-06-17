#include "sensor_components/collision_guard_node.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>

#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "diagnostic_msgs/msg/key_value.hpp"

namespace sensor_components
{
namespace
{
constexpr double kUnknownDistance = std::numeric_limits<double>::quiet_NaN();

std::string boolString(bool value)
{
  return value ? "true" : "false";
}

std::string distanceString(double distance)
{
  if (!std::isfinite(distance)) {
    return "nan";
  }
  std::ostringstream stream;
  stream.precision(3);
  stream << std::fixed << distance;
  return stream.str();
}
}  // namespace

CollisionGuardNode::CollisionGuardNode()
: Node("collision_guard_node"),
  input_cmd_topic_(declare_parameter<std::string>("input_cmd_topic", "/cmd_vel_smoothed")),
  output_cmd_topic_(declare_parameter<std::string>("output_cmd_topic", "/cmd_vel")),
  status_topic_(declare_parameter<std::string>("status_topic", "/tof/collision_guard_status")),
  min_range_(declare_parameter<double>("min_range", 0.03)),
  max_valid_range_(declare_parameter<double>("max_valid_range", 1.2)),
  sentinel_invalid_range_(declare_parameter<double>("sentinel_invalid_range", 8.0)),
  sensor_timeout_sec_(declare_parameter<double>("sensor_timeout_sec", 0.35)),
  status_publish_period_sec_(declare_parameter<double>("status_publish_period_sec", 0.5)),
  linear_deadband_(declare_parameter<double>("linear_deadband", 0.01)),
  reverse_deadband_(declare_parameter<double>("reverse_deadband", 0.01)),
  rotate_deadband_(declare_parameter<double>("rotate_deadband", 0.03)),
  reverse_stop_distance_(declare_parameter<double>("reverse_stop_distance", 0.25)),
  reverse_slow_distance_(declare_parameter<double>("reverse_slow_distance", 0.45)),
  side_stop_distance_(declare_parameter<double>("side_stop_distance", 0.18)),
  side_slow_distance_(declare_parameter<double>("side_slow_distance", 0.35)),
  rotate_stop_distance_(declare_parameter<double>("rotate_stop_distance", 0.22)),
  rotate_slow_distance_(declare_parameter<double>("rotate_slow_distance", 0.40)),
  reverse_limited_speed_abs_(declare_parameter<double>("reverse_limited_speed_abs", 0.03)),
  rotate_limited_speed_abs_(declare_parameter<double>("rotate_limited_speed_abs", 0.15)),
  last_status_publish_(0, 0, get_clock()->get_clock_type())
{
  sensors_[static_cast<std::size_t>(SensorIndex::kFrontLeft)].name = "front_left";
  sensors_[static_cast<std::size_t>(SensorIndex::kFrontLeft)].frame_id = "tof_front_left_link";
  sensors_[static_cast<std::size_t>(SensorIndex::kFrontLeft)].topic =
    declare_parameter<std::string>("front_left_topic", "/tof/front_left");

  sensors_[static_cast<std::size_t>(SensorIndex::kFrontRight)].name = "front_right";
  sensors_[static_cast<std::size_t>(SensorIndex::kFrontRight)].frame_id = "tof_front_right_link";
  sensors_[static_cast<std::size_t>(SensorIndex::kFrontRight)].topic =
    declare_parameter<std::string>("front_right_topic", "/tof/front_right");

  sensors_[static_cast<std::size_t>(SensorIndex::kRearLeft)].name = "rear_left";
  sensors_[static_cast<std::size_t>(SensorIndex::kRearLeft)].frame_id = "tof_rear_left_link";
  sensors_[static_cast<std::size_t>(SensorIndex::kRearLeft)].topic =
    declare_parameter<std::string>("rear_left_topic", "/tof/rear_left");

  sensors_[static_cast<std::size_t>(SensorIndex::kRearRight)].name = "rear_right";
  sensors_[static_cast<std::size_t>(SensorIndex::kRearRight)].frame_id = "tof_rear_right_link";
  sensors_[static_cast<std::size_t>(SensorIndex::kRearRight)].topic =
    declare_parameter<std::string>("rear_right_topic", "/tof/rear_right");

  sensors_[static_cast<std::size_t>(SensorIndex::kRearCenter)].name = "rear_center";
  sensors_[static_cast<std::size_t>(SensorIndex::kRearCenter)].frame_id = "tof_rear_center_link";
  sensors_[static_cast<std::size_t>(SensorIndex::kRearCenter)].topic =
    declare_parameter<std::string>("rear_center_topic", "/tof/rear_center");

  for (std::size_t i = 0; i < sensors_.size(); ++i) {
    sensors_[i].last_received = rclcpp::Time(0, 0, get_clock()->get_clock_type());
    sensors_[i].subscription = create_subscription<sensor_msgs::msg::Range>(
      sensors_[i].topic,
      rclcpp::SensorDataQoS(),
      [this, i](const sensor_msgs::msg::Range::SharedPtr msg) {
        rangeCallback(msg, static_cast<SensorIndex>(i));
      });
  }

  cmd_sub_ = create_subscription<geometry_msgs::msg::Twist>(
    input_cmd_topic_, 10, std::bind(&CollisionGuardNode::cmdVelCallback, this, std::placeholders::_1));
  cmd_pub_ = create_publisher<geometry_msgs::msg::Twist>(output_cmd_topic_, 10);
  status_pub_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>(status_topic_, 10);
}

void CollisionGuardNode::rangeCallback(
  const sensor_msgs::msg::Range::SharedPtr msg,
  SensorIndex index)
{
  const auto i = static_cast<std::size_t>(index);
  if (i >= sensors_.size()) {
    return;
  }
  sensors_[i].last_msg = msg;
  sensors_[i].last_received = get_clock()->now();
}

void CollisionGuardNode::cmdVelCallback(const geometry_msgs::msg::Twist::SharedPtr msg)
{
  const auto now = get_clock()->now();
  std::array<SensorView, kSensorCount> views;
  for (std::size_t i = 0; i < views.size(); ++i) {
    views[i] = sensorView(static_cast<SensorIndex>(i), now);
  }

  geometry_msgs::msg::Twist output = *msg;
  GuardStatus status{"idle", "pass", false, "none", kUnknownDistance, "ok"};

  const bool reversing = msg->linear.x < -reverse_deadband_;
  const bool rotating =
    std::abs(msg->angular.z) > rotate_deadband_ &&
    (std::abs(msg->linear.x) < linear_deadband_ || reversing);

  if (reversing) {
    applyReverseGuard(output, views, status);
  }
  if (rotating) {
    applyRotateGuard(output, views, status);
  }
  if (!reversing && !rotating && std::abs(msg->linear.x) >= linear_deadband_) {
    status.mode = "forward";
  }

  cmd_pub_->publish(output);
  publishStatus(status, now);
}

CollisionGuardNode::SensorView CollisionGuardNode::sensorView(
  SensorIndex index,
  const rclcpp::Time & now) const
{
  const auto & sensor = sensors_[static_cast<std::size_t>(index)];
  if (!sensor.last_msg) {
    return {SensorValidity::kMissing, kUnknownDistance, "missing"};
  }
  if ((now - sensor.last_received).seconds() > sensor_timeout_sec_) {
    return {SensorValidity::kTimeout, kUnknownDistance, "timeout"};
  }

  const double range = sensor.last_msg->range;
  if (!std::isfinite(range)) {
    return {SensorValidity::kInvalid, kUnknownDistance, "non_finite"};
  }
  if (range <= 0.0 || range < min_range_) {
    return {SensorValidity::kInvalid, range, "below_min"};
  }
  if (range >= sentinel_invalid_range_) {
    return {SensorValidity::kInvalid, range, "sentinel"};
  }
  if (range > max_valid_range_) {
    return {SensorValidity::kInvalid, range, "above_max"};
  }

  return {SensorValidity::kValid, range, "ok"};
}

bool CollisionGuardNode::sensorIsStop(const SensorView & sensor, double stop_distance) const
{
  return sensor.validity == SensorValidity::kValid && sensor.distance <= stop_distance;
}

bool CollisionGuardNode::sensorIsSlow(const SensorView & sensor, double slow_distance) const
{
  return sensor.validity == SensorValidity::kValid && sensor.distance <= slow_distance;
}

bool CollisionGuardNode::sensorIsInvalidForMotion(const SensorView & sensor) const
{
  return sensor.validity != SensorValidity::kValid;
}

void CollisionGuardNode::applyReverseGuard(
  geometry_msgs::msg::Twist & output,
  const std::array<SensorView, kSensorCount> & views,
  GuardStatus & status) const
{
  const auto rear_left = views[static_cast<std::size_t>(SensorIndex::kRearLeft)];
  const auto rear_right = views[static_cast<std::size_t>(SensorIndex::kRearRight)];
  const auto rear_center = views[static_cast<std::size_t>(SensorIndex::kRearCenter)];
  const auto front_left = views[static_cast<std::size_t>(SensorIndex::kFrontLeft)];
  const auto front_right = views[static_cast<std::size_t>(SensorIndex::kFrontRight)];

  const bool rear_left_invalid = sensorIsInvalidForMotion(rear_left);
  const bool rear_right_invalid = sensorIsInvalidForMotion(rear_right);

  if (rear_left_invalid && rear_right_invalid) {
    output.linear.x = 0.0;
    updateStatus(status, "reverse", "stop", true, "rear_left,rear_right", kUnknownDistance, "invalid");
    return;
  }

  if (sensorIsStop(rear_left, reverse_stop_distance_)) {
    output.linear.x = 0.0;
    updateStatus(status, "reverse", "stop", true, "rear_left", rear_left.distance, "stop_distance");
    return;
  }
  if (sensorIsStop(rear_right, reverse_stop_distance_)) {
    output.linear.x = 0.0;
    updateStatus(status, "reverse", "stop", true, "rear_right", rear_right.distance, "stop_distance");
    return;
  }
  if (sensorIsStop(rear_center, reverse_stop_distance_)) {
    output.linear.x = 0.0;
    updateStatus(status, "reverse", "stop", true, "rear_center", rear_center.distance, "stop_distance");
    return;
  }
  if (sensorIsStop(front_left, side_stop_distance_)) {
    output.linear.x = 0.0;
    updateStatus(status, "reverse", "stop", true, "front_left", front_left.distance, "side_pinch");
    return;
  }
  if (sensorIsStop(front_right, side_stop_distance_)) {
    output.linear.x = 0.0;
    updateStatus(status, "reverse", "stop", true, "front_right", front_right.distance, "side_pinch");
    return;
  }

  if (rear_left_invalid || rear_right_invalid) {
    output.linear.x = std::max(output.linear.x, -reverse_limited_speed_abs_);
    updateStatus(
      status, "reverse", "slow", false,
      rear_left_invalid ? "rear_left" : "rear_right", kUnknownDistance, "invalid");
    return;
  }

  if (sensorIsSlow(rear_left, reverse_slow_distance_)) {
    output.linear.x = std::max(output.linear.x, -reverse_limited_speed_abs_);
    updateStatus(status, "reverse", "slow", false, "rear_left", rear_left.distance, "slow_distance");
  }
  if (sensorIsSlow(rear_right, reverse_slow_distance_)) {
    output.linear.x = std::max(output.linear.x, -reverse_limited_speed_abs_);
    updateStatus(status, "reverse", "slow", false, "rear_right", rear_right.distance, "slow_distance");
  }
  if (sensorIsSlow(rear_center, reverse_slow_distance_)) {
    output.linear.x = std::max(output.linear.x, -reverse_limited_speed_abs_);
    updateStatus(status, "reverse", "slow", false, "rear_center", rear_center.distance, "slow_distance");
  }
  if (sensorIsSlow(front_left, side_slow_distance_)) {
    output.linear.x = std::max(output.linear.x, -reverse_limited_speed_abs_);
    updateStatus(status, "reverse", "slow", false, "front_left", front_left.distance, "side_slow");
  }
  if (sensorIsSlow(front_right, side_slow_distance_)) {
    output.linear.x = std::max(output.linear.x, -reverse_limited_speed_abs_);
    updateStatus(status, "reverse", "slow", false, "front_right", front_right.distance, "side_slow");
  }
}

void CollisionGuardNode::applyRotateGuard(
  geometry_msgs::msg::Twist & output,
  const std::array<SensorView, kSensorCount> & views,
  GuardStatus & status) const
{
  const bool rotate_left = output.angular.z > 0.0;
  const auto primary_index = rotate_left ? SensorIndex::kFrontLeft : SensorIndex::kFrontRight;
  const auto secondary_index = rotate_left ? SensorIndex::kRearLeft : SensorIndex::kRearRight;
  const auto primary = views[static_cast<std::size_t>(primary_index)];
  const auto secondary = views[static_cast<std::size_t>(secondary_index)];
  const std::string mode = rotate_left ? "rotate_left" : "rotate_right";
  const std::string primary_name = sensorName(primary_index);
  const std::string secondary_name = sensorName(secondary_index);

  const bool primary_invalid = sensorIsInvalidForMotion(primary);
  const bool secondary_invalid = sensorIsInvalidForMotion(secondary);

  if (primary_invalid && secondary_invalid) {
    output.angular.z = 0.0;
    updateStatus(
      status, mode, "stop", true, joinSensors(primary_name, secondary_name), kUnknownDistance, "invalid");
    return;
  }

  if (sensorIsStop(primary, side_stop_distance_) || sensorIsStop(primary, rotate_stop_distance_)) {
    output.angular.z = 0.0;
    updateStatus(status, mode, "stop", true, primary_name, primary.distance, "side_pinch");
    return;
  }
  if (sensorIsStop(secondary, rotate_stop_distance_)) {
    output.angular.z = 0.0;
    updateStatus(status, mode, "stop", true, secondary_name, secondary.distance, "stop_distance");
    return;
  }

  if (primary_invalid || secondary_invalid) {
    output.angular.z = std::copysign(
      std::min(std::abs(output.angular.z), rotate_limited_speed_abs_), output.angular.z);
    updateStatus(
      status, mode, "slow", false, primary_invalid ? primary_name : secondary_name,
      kUnknownDistance, "invalid");
    return;
  }

  if (sensorIsSlow(primary, side_slow_distance_) || sensorIsSlow(primary, rotate_slow_distance_)) {
    output.angular.z = std::copysign(
      std::min(std::abs(output.angular.z), rotate_limited_speed_abs_), output.angular.z);
    updateStatus(status, mode, "slow", false, primary_name, primary.distance, "side_slow");
  }
  if (sensorIsSlow(secondary, rotate_slow_distance_)) {
    output.angular.z = std::copysign(
      std::min(std::abs(output.angular.z), rotate_limited_speed_abs_), output.angular.z);
    updateStatus(status, mode, "slow", false, secondary_name, secondary.distance, "slow_distance");
  }
}

void CollisionGuardNode::publishStatus(const GuardStatus & status, const rclcpp::Time & now)
{
  const std::string signature = statusSignature(status);
  const bool changed = signature != last_status_signature_;
  const bool due = (now - last_status_publish_).seconds() >= status_publish_period_sec_;
  if (!changed && !due) {
    return;
  }

  diagnostic_msgs::msg::DiagnosticArray array;
  array.header.stamp = now;

  diagnostic_msgs::msg::DiagnosticStatus diagnostic;
  diagnostic.name = "tof_collision_guard";
  diagnostic.hardware_id = "tof";
  diagnostic.level = status.blocked || status.action != "pass" ?
    diagnostic_msgs::msg::DiagnosticStatus::WARN :
    diagnostic_msgs::msg::DiagnosticStatus::OK;
  diagnostic.message = status.reason;

  const auto add_value = [&diagnostic](const std::string & key, const std::string & value) {
    diagnostic_msgs::msg::KeyValue item;
    item.key = key;
    item.value = value;
    diagnostic.values.push_back(item);
  };

  add_value("mode", status.mode);
  add_value("action", status.action);
  add_value("blocked", boolString(status.blocked));
  add_value("active_sensor", status.active_sensor);
  add_value("distance", distanceString(status.distance));
  add_value("reason", status.reason);

  array.status.push_back(diagnostic);
  status_pub_->publish(array);
  last_status_signature_ = signature;
  last_status_publish_ = now;
}

void CollisionGuardNode::updateStatus(
  GuardStatus & status,
  const std::string & mode,
  const std::string & action,
  bool blocked,
  const std::string & active_sensor,
  double distance,
  const std::string & reason) const
{
  if (status.blocked && !blocked) {
    return;
  }
  if (status.action == "slow" && action == "pass") {
    return;
  }
  if (status.action == "slow" && action == "slow" &&
    std::isfinite(status.distance) && std::isfinite(distance) && status.distance <= distance)
  {
    return;
  }

  status.mode = status.mode == "idle" || status.mode == mode ? mode : status.mode + "+" + mode;
  status.action = action;
  status.blocked = blocked;
  status.active_sensor = active_sensor;
  status.distance = distance;
  status.reason = reason;
}

std::string CollisionGuardNode::statusSignature(const GuardStatus & status) const
{
  return status.mode + "|" + status.action + "|" + boolString(status.blocked) + "|" +
         status.active_sensor + "|" + status.reason + "|" + distanceString(status.distance);
}

std::string CollisionGuardNode::sensorName(SensorIndex index) const
{
  return sensors_[static_cast<std::size_t>(index)].name;
}

std::string CollisionGuardNode::joinSensors(const std::string & first, const std::string & second) const
{
  return first + "," + second;
}

}  // namespace sensor_components

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<sensor_components::CollisionGuardNode>());
  rclcpp::shutdown();
  return 0;
}
