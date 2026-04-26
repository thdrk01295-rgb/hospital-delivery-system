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
  wheel_radius_     = declare_parameter<double>("wheel_radius", 0.0625);
  wheel_separation_ = declare_parameter<double>("wheel_separation", 0.38);
  ticks_per_rev_    = declare_parameter<int>("ticks_per_rev", 75);
  publish_tf_       = declare_parameter<bool>("publish_tf", true);
  odom_frame_       = declare_parameter<std::string>("odom_frame", "odom");
  base_frame_       = declare_parameter<std::string>("base_frame", "base_link");

  odom_pub_       = create_publisher<nav_msgs::msg::Odometry>("/odom", 20);
  tf_broadcaster_ = std::make_unique<tf2_ros::TransformBroadcaster>(*this);

  encoder_ticks_sub_ = create_subscription<std_msgs::msg::Int64MultiArray>(
    "/encoder_ticks", 50,
    std::bind(&EncoderOdomNode::encoderTicksCallback, this, std::placeholders::_1));
}

// ---------------------------------------------------------------------------
// Odometry update
// ---------------------------------------------------------------------------

void EncoderOdomNode::encoderTicksCallback(
  const std_msgs::msg::Int64MultiArray::SharedPtr msg)
{
  // Layout published by motor_bridge_node: [stk_l, stk_r, tot_l, tot_r]
  if (msg->data.size() < 4) {
    RCLCPP_WARN(get_logger(), "encoder_ticks: expected 4 fields, got %zu", msg->data.size());
    return;
  }

  const auto now_time = now();

  if (first_msg_) {
    last_time_ = now_time;
    first_msg_ = false;
    return;
  }

  // STK is signed body-frame tick: +forward, -reverse
  const int64_t stk_l = msg->data[0];
  const int64_t stk_r = msg->data[1];

  const double meters_per_tick =
    (2.0 * M_PI * wheel_radius_) / static_cast<double>(ticks_per_rev_);

  const double dist_l = static_cast<double>(stk_l) * meters_per_tick;
  const double dist_r = static_cast<double>(stk_r) * meters_per_tick;

  const double dist   = 0.5 * (dist_l + dist_r);
  const double dtheta = (dist_r - dist_l) / wheel_separation_;

  x_   += dist * std::cos(yaw_ + dtheta * 0.5);
  y_   += dist * std::sin(yaw_ + dtheta * 0.5);
  yaw_ += dtheta;

  double dt = (now_time - last_time_).seconds();
  if (dt <= 0.0) dt = 1e-3;

  const double linear_vel  = dist   / dt;
  const double angular_vel = dtheta / dt;

  publishOdometry(now_time, linear_vel, angular_vel);
  if (publish_tf_) {
    publishTf(now_time);
  }

  last_time_ = now_time;
}

// ---------------------------------------------------------------------------
// Publishers
// ---------------------------------------------------------------------------

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