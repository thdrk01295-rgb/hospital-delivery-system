#ifndef SENSOR_COMPONENTS_COLLISION_GUARD_NODE_HPP_
#define SENSOR_COMPONENTS_COLLISION_GUARD_NODE_HPP_

#include <array>
#include <string>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/range.hpp"
#include "sensor_components/collision_guard_policy.hpp"

namespace sensor_components
{

class CollisionGuardNode : public rclcpp::Node
{
public:
  CollisionGuardNode();

private:
  struct SensorState
  {
    std::string name;
    std::string topic;
    std::string frame_id;
    sensor_msgs::msg::Range::SharedPtr last_msg;
    rclcpp::Time last_received;
    rclcpp::Subscription<sensor_msgs::msg::Range>::SharedPtr subscription;
  };

  void cmdVelCallback(const geometry_msgs::msg::Twist::SharedPtr msg);
  void rangeCallback(const sensor_msgs::msg::Range::SharedPtr msg, SensorIndex index);
  SensorSnapshot sensorSnapshot(SensorIndex index, const rclcpp::Time & now) const;
  GuardConfig guardConfig() const;
  void publishStatus(const GuardStatus & status, const rclcpp::Time & now);
  std::string statusSignature(const GuardStatus & status) const;

  std::array<SensorState, kSensorCount> sensors_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_sub_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_pub_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr status_pub_;

  std::string input_cmd_topic_;
  std::string output_cmd_topic_;
  std::string status_topic_;
  double min_range_;
  double max_valid_range_;
  double sentinel_invalid_range_;
  double sensor_timeout_sec_;
  double status_publish_period_sec_;
  double linear_deadband_;
  double reverse_deadband_;
  double rotate_deadband_;
  double reverse_stop_distance_;
  double reverse_slow_distance_;
  double side_stop_distance_;
  double side_slow_distance_;
  double rotate_stop_distance_;
  double rotate_slow_distance_;
  double reverse_limited_speed_abs_;
  double rotate_limited_speed_abs_;
  std::string last_status_signature_;
  rclcpp::Time last_status_publish_;
};

}  // namespace sensor_components

#endif  // SENSOR_COMPONENTS_COLLISION_GUARD_NODE_HPP_
