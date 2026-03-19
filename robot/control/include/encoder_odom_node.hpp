#ifndef MOTOR_BRIDGE__ENCODER_ODOM_HPP_
#define MOTOR_BRIDGE__ENCODER_ODOM_HPP_

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/int64_multi_array.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <tf2_ros/transform_broadcaster.h>

namespace motor_bridge
{

class EncoderOdomNode : public rclcpp::Node
{
public:
  EncoderOdomNode();

private:
  void encoderTicksCallback(const std_msgs::msg::Int64MultiArray::SharedPtr msg);
  void publishOdometry(
    double d_left, double d_right,
    const rclcpp::Time & stamp);
  geometry_msgs::msg::Quaternion yawToQuaternion(double yaw) const;

  rclcpp::Subscription<std_msgs::msg::Int64MultiArray>::SharedPtr encoder_ticks_sub_;
  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odom_pub_;
  std::unique_ptr<tf2_ros::TransformBroadcaster> tf_broadcaster_;

  double wheel_radius_;
  double wheel_separation_;
  double ticks_per_rev_left_;
  double ticks_per_rev_right_;
  bool invert_left_;
  bool invert_right_;
  bool publish_tf_;

  std::string odom_frame_;
  std::string base_frame_;

  double x_;
  double y_;
  double yaw_;

  bool first_msg_;
  rclcpp::Time last_time_;
};

}  // namespace motor_bridge

#endif  // MOTOR_BRIDGE__ENCODER_ODOM_HPP_