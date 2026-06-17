#ifndef SENSOR_COMPONENTS_COLLISION_GUARD_NODE_HPP_
#define SENSOR_COMPONENTS_COLLISION_GUARD_NODE_HPP_

#include <array>
#include <cstddef>
#include <string>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/range.hpp"

namespace sensor_components
{

class CollisionGuardNode : public rclcpp::Node
{
public:
  CollisionGuardNode();

private:
  enum class SensorIndex : std::size_t
  {
    kFrontLeft = 0,
    kFrontRight = 1,
    kRearLeft = 2,
    kRearRight = 3,
    kRearCenter = 4,
  };

  static constexpr std::size_t kSensorCount = 5;

  enum class SensorValidity
  {
    kValid,
    kMissing,
    kTimeout,
    kInvalid,
  };

  struct SensorState
  {
    std::string name;
    std::string topic;
    std::string frame_id;
    sensor_msgs::msg::Range::SharedPtr last_msg;
    rclcpp::Time last_received;
    rclcpp::Subscription<sensor_msgs::msg::Range>::SharedPtr subscription;
  };

  struct SensorView
  {
    SensorValidity validity;
    double distance;
    std::string reason;
  };

  struct GuardStatus
  {
    std::string mode;
    std::string action;
    bool blocked;
    std::string active_sensor;
    double distance;
    std::string reason;
  };

  void cmdVelCallback(const geometry_msgs::msg::Twist::SharedPtr msg);
  void rangeCallback(const sensor_msgs::msg::Range::SharedPtr msg, SensorIndex index);
  SensorView sensorView(SensorIndex index, const rclcpp::Time & now) const;
  bool sensorIsStop(const SensorView & sensor, double stop_distance) const;
  bool sensorIsSlow(const SensorView & sensor, double slow_distance) const;
  bool sensorIsInvalidForMotion(const SensorView & sensor) const;
  void applyReverseGuard(
    geometry_msgs::msg::Twist & output,
    const std::array<SensorView, kSensorCount> & views,
    GuardStatus & status) const;
  void applyRotateGuard(
    geometry_msgs::msg::Twist & output,
    const std::array<SensorView, kSensorCount> & views,
    GuardStatus & status) const;
  void publishStatus(const GuardStatus & status, const rclcpp::Time & now);
  void updateStatus(
    GuardStatus & status,
    const std::string & mode,
    const std::string & action,
    bool blocked,
    const std::string & active_sensor,
    double distance,
    const std::string & reason) const;
  std::string statusSignature(const GuardStatus & status) const;
  std::string sensorName(SensorIndex index) const;
  std::string joinSensors(const std::string & first, const std::string & second) const;

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
