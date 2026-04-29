#include "encoder_odom_node.hpp"

#include <cmath>

namespace motor_bridge
{

EncoderOdomNode::EncoderOdomNode()
: Node("encoder_odom_node"),
  x_(0.0),
  y_(0.0),
  yaw_(0.0),
  last_time_(now()),
  first_msg_(true)
{
  encoder_ticks_topic_ = declare_parameter<std::string>("encoder_ticks_topic", "/encoder_ticks");
  odom_topic_ = declare_parameter<std::string>("odom_topic", "/odom_raw");
  odom_frame_ = declare_parameter<std::string>("odom_frame", "odom");
  base_frame_ = declare_parameter<std::string>("base_frame", "base_link");
  wheel_radius_ = declare_parameter<double>("wheel_radius", 0.0625);
  wheel_separation_ = declare_parameter<double>("wheel_separation", 0.38);
  ticks_per_revolution_ = declare_parameter<double>("ticks_per_revolution", 75.0);
  publish_tf_ = declare_parameter<bool>("publish_tf", false);
  left_tick_sign_ = declare_parameter<double>("left_tick_sign", 1.0);
  right_tick_sign_ = declare_parameter<double>("right_tick_sign", 1.0);

  odom_pub_ = create_publisher<nav_msgs::msg::Odometry>(odom_topic_, 20);
  tf_broadcaster_ = std::make_unique<tf2_ros::TransformBroadcaster>(*this);

  encoder_ticks_sub_ = create_subscription<std_msgs::msg::Int64MultiArray>(
    encoder_ticks_topic_, 50,
    std::bind(&EncoderOdomNode::encoderTicksCallback, this, std::placeholders::_1));
}

void EncoderOdomNode::encoderTicksCallback(
  const std_msgs::msg::Int64MultiArray::SharedPtr msg)
{
  if (msg->data.size() < 2) {
    RCLCPP_ERROR(get_logger(), "encoder_ticks: expected at least 2 fields, got %zu", msg->data.size());
    return;
  }

  const auto now_time = now();

  if (first_msg_) {
    last_time_ = now_time;
    first_msg_ = false;
    return;
  }

  const double dt = (now_time - last_time_).seconds();
  if (dt <= 0.0) {
    return;
  }

  const double left_delta_ticks = static_cast<double>(msg->data[0]) * left_tick_sign_;
  const double right_delta_ticks = static_cast<double>(msg->data[1]) * right_tick_sign_;

  const double meters_per_tick = (2.0 * M_PI * wheel_radius_) / ticks_per_revolution_;
  const double left_distance = left_delta_ticks * meters_per_tick;
  const double right_distance = right_delta_ticks * meters_per_tick;

  const double delta_s = (right_distance + left_distance) * 0.5;
  const double delta_theta = (right_distance - left_distance) / wheel_separation_;

  x_ += delta_s * std::cos(yaw_ + delta_theta * 0.5);
  y_ += delta_s * std::sin(yaw_ + delta_theta * 0.5);
  yaw_ += delta_theta;

  const double linear_vel = delta_s / dt;
  const double angular_vel = delta_theta / dt;

  publishOdometry(now_time, linear_vel, angular_vel);
  if (publish_tf_) {
    publishTf(now_time);
  }

  last_time_ = now_time;
}

void EncoderOdomNode::publishOdometry(
  const rclcpp::Time & stamp, double linear_vel, double angular_vel)
{
  nav_msgs::msg::Odometry odom;
  odom.header.stamp    = stamp;
  odom.header.frame_id = odom_frame_;
  odom.child_frame_id  = base_frame_;

  odom.pose.pose.position.x = x_;
  odom.pose.pose.position.y = y_;
  odom.pose.pose.position.z = 0.0;

  tf2::Quaternion q;
  q.setRPY(0.0, 0.0, yaw_);
  odom.pose.pose.orientation.x = q.x();
  odom.pose.pose.orientation.y = q.y();
  odom.pose.pose.orientation.z = q.z();
  odom.pose.pose.orientation.w = q.w();

  odom.twist.twist.linear.x  = linear_vel;
  odom.twist.twist.linear.y  = 0.0;
  odom.twist.twist.angular.z = angular_vel;

  odom.pose.covariance[0]  = 0.05;
  odom.pose.covariance[7]  = 0.05;
  odom.pose.covariance[35] = 0.1;

  odom.twist.covariance[0]  = 0.05;
  odom.twist.covariance[7]  = 0.05;
  odom.twist.covariance[35] = 0.1;

  odom_pub_->publish(odom);
}

void EncoderOdomNode::publishTf(const rclcpp::Time & stamp)
{
  geometry_msgs::msg::TransformStamped tf_msg;
  tf_msg.header.stamp    = stamp;
  tf_msg.header.frame_id = odom_frame_;
  tf_msg.child_frame_id  = base_frame_;

  tf_msg.transform.translation.x = x_;
  tf_msg.transform.translation.y = y_;
  tf_msg.transform.translation.z = 0.0;

  tf2::Quaternion q;
  q.setRPY(0.0, 0.0, yaw_);
  tf_msg.transform.rotation.x = q.x();
  tf_msg.transform.rotation.y = q.y();
  tf_msg.transform.rotation.z = q.z();
  tf_msg.transform.rotation.w = q.w();

  tf_broadcaster_->sendTransform(tf_msg);
}

}  // namespace motor_bridge

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<motor_bridge::EncoderOdomNode>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
