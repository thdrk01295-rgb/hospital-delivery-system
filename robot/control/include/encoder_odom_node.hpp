#ifndef MOTOR_BRIDGE__ENCODER_ODOM_HPP_
#define MOTOR_BRIDGE__ENCODER_ODOM_HPP_

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/int64_multi_array.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <geometry_msgs/msg/transform_stamped.hpp>
#include <tf2_ros/transform_broadcaster.h>
#include <tf2/LinearMath/Quaternion.h>

#include <memory>
#include <string>

namespace motor_bridge
{

class EncoderOdomNode : public rclcpp::Node
{
public:
  EncoderOdomNode();

private:
  void encoderTicksCallback(const std_msgs::msg::Int64MultiArray::SharedPtr msg);
  void publishOdometry(const rclcpp::Time & stamp, double linear_vel, double angular_vel);
  void publishTf(const rclcpp::Time & stamp);

  rclcpp::Subscription<std_msgs::msg::Int64MultiArray>::SharedPtr encoder_ticks_sub_;
  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odom_pub_;
  std::unique_ptr<tf2_ros::TransformBroadcaster> tf_broadcaster_;

  std::string encoder_ticks_topic_;
  std::string odom_topic_;
  std::string odom_frame_;
  std::string base_frame_;

  double wheel_radius_;
  double wheel_separation_;
  double ticks_per_revolution_;
  double left_tick_sign_;
  double right_tick_sign_;
  bool   publish_tf_;

  double x_;
  double y_;
  double yaw_;

  rclcpp::Time last_time_;
  bool first_msg_;
};

}  // namespace motor_bridge

#endif  // MOTOR_BRIDGE__ENCODER_ODOM_HPP_
