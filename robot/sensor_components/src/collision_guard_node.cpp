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
  sensor_timeout_sec_(declare_parameter<double>("sensor_timeout_sec", 0.50)),
  status_publish_period_sec_(declare_parameter<double>("status_publish_period_sec", 0.5)),
  linear_deadband_(declare_parameter<double>("linear_deadband", 0.01)),
  reverse_deadband_(declare_parameter<double>("reverse_deadband", 0.01)),
  rotate_deadband_(declare_parameter<double>("rotate_deadband", 0.05)),
  reverse_stop_distance_(declare_parameter<double>("reverse_stop_distance", 0.22)),
  reverse_slow_distance_(declare_parameter<double>("reverse_slow_distance", 0.38)),
  side_stop_distance_(declare_parameter<double>("side_stop_distance", 0.14)),
  side_slow_distance_(declare_parameter<double>("side_slow_distance", 0.25)),
  rotate_stop_distance_(declare_parameter<double>("rotate_stop_distance", 0.16)),
  rotate_slow_distance_(declare_parameter<double>("rotate_slow_distance", 0.28)),
  reverse_limited_speed_abs_(declare_parameter<double>("reverse_limited_speed_abs", 0.03)),
  rotate_limited_speed_abs_(declare_parameter<double>("rotate_limited_speed_abs", 0.10)),
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
  std::array<SensorSnapshot, kSensorCount> snapshots;
  for (std::size_t i = 0; i < snapshots.size(); ++i) {
    snapshots[i] = sensorSnapshot(static_cast<SensorIndex>(i), now);
  }

  geometry_msgs::msg::Twist output = *msg;
  const auto decision =
    evaluateCollisionGuard(msg->linear.x, msg->angular.z, snapshots, guardConfig());
  output.linear.x = decision.linear_x;
  output.angular.z = decision.angular_z;

  cmd_pub_->publish(output);
  publishStatus(decision.status, now);
}

SensorSnapshot CollisionGuardNode::sensorSnapshot(
  SensorIndex index,
  const rclcpp::Time & now) const
{
  const auto & sensor = sensors_[static_cast<std::size_t>(index)];
  const bool has_sample = static_cast<bool>(sensor.last_msg);
  const double age_sec = has_sample ? (now - sensor.last_received).seconds() : 0.0;
  const double range = has_sample ? sensor.last_msg->range : kUnknownDistance;
  return {sensor.name, classifySensorSample(has_sample, age_sec, range, guardConfig())};
}

GuardConfig CollisionGuardNode::guardConfig() const
{
  GuardConfig config;
  config.min_range = min_range_;
  config.max_valid_range = max_valid_range_;
  config.sentinel_invalid_range = sentinel_invalid_range_;
  config.sensor_timeout_sec = sensor_timeout_sec_;
  config.linear_deadband = linear_deadband_;
  config.reverse_deadband = reverse_deadband_;
  config.rotate_deadband = rotate_deadband_;
  config.reverse_stop_distance = reverse_stop_distance_;
  config.reverse_slow_distance = reverse_slow_distance_;
  config.side_stop_distance = side_stop_distance_;
  config.side_slow_distance = side_slow_distance_;
  config.rotate_stop_distance = rotate_stop_distance_;
  config.rotate_slow_distance = rotate_slow_distance_;
  config.reverse_limited_speed_abs = reverse_limited_speed_abs_;
  config.rotate_limited_speed_abs = rotate_limited_speed_abs_;
  return config;
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
  add_value("sensor_states", status.sensor_states);

  array.status.push_back(diagnostic);
  status_pub_->publish(array);
  last_status_signature_ = signature;
  last_status_publish_ = now;
}

std::string CollisionGuardNode::statusSignature(const GuardStatus & status) const
{
  return status.mode + "|" + status.action + "|" + boolString(status.blocked) + "|" +
         status.active_sensor + "|" + status.reason + "|" + distanceString(status.distance) + "|" +
         status.sensor_states;
}

}  // namespace sensor_components

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<sensor_components::CollisionGuardNode>());
  rclcpp::shutdown();
  return 0;
}
