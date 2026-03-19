#include "encoder_odom_node.hpp"

#include <geometry_msgs/msg/quaternion.hpp>
#include <geometry_msgs/msg/transform_stamped.hpp>
#include <tf2/LinearMath/Quaternion.h>

#include <cmath>

namespace motor_bridge
{

static constexpr double PI_CONST = 3.14159265358979323846;

EncoderOdomNode::EncoderOdomNode()
: Node("encoder_odom_node"),
  x_(0.0),
  y_(0.0),
  yaw_(0.0),
  first_msg_(true)
{
  declare_parameter<double>("wheel_radius", 0.0625);
  declare_parameter<double>("wheel_separation", 0.330);
  declare_parameter<double>("ticks_per_rev_left", 77.0);
  declare_parameter<double>("ticks_per_rev_right", 77.0);
  declare_parameter<bool>("invert_left", false);
  declare_parameter<bool>("invert_right", false);
  declare_parameter<bool>("publish_tf", true);
  declare_parameter<std::string>("odom_frame", "odom");
  declare_parameter<std::string>("base_frame", "base_link");

  wheel_radius_ = get_parameter("wheel_radius").as_double();
  wheel_separation_ = get_parameter("wheel_separation").as_double();
  ticks_per_rev_left_ = get_parameter("ticks_per_rev_left").as_double();
  ticks_per_rev_right_ = get_parameter("ticks_per_rev_right").as_double();
  invert_left_ = get_parameter("invert_left").as_bool();
  invert_right_ = get_parameter("invert_right").as_bool();
  publish_tf_ = get_parameter("publish_tf").as_bool();
  odom_frame_ = get_parameter("odom_frame").as_string();
  base_frame_ = get_parameter("base_frame").as_string();

  encoder_ticks_sub_ = create_subscription<std_msgs::msg::Int64MultiArray>(
    "/encoder_ticks", 10,
    std::bind(&EncoderOdomNode::encoderTicksCallback, this, std::placeholders::_1));

  odom_pub_ = create_publisher<nav_msgs::msg::Odometry>("/odom", 10);

  if (publish_tf_) {
    tf_broadcaster_ = std::make_unique<tf2_ros::TransformBroadcaster>(*this);
  }
}

geometry_msgs::msg::Quaternion EncoderOdomNode::yawToQuaternion(double yaw) const
{
  tf2::Quaternion q;
  q.setRPY(0.0, 0.0, yaw);

  geometry_msgs::msg::Quaternion quat;
  quat.x = q.x();
  quat.y = q.y();
  quat.z = q.z();
  quat.w = q.w();
  return quat;
}

void EncoderOdomNode::encoderTicksCallback(const std_msgs::msg::Int64MultiArray::SharedPtr msg)
{
  if (msg->data.size() < 4) {
    return;
  }

  const auto stamp = now();

  if (first_msg_) {
    first_msg_ = false;
    last_time_ = stamp;
    return;
  }

  const double dt = (stamp - last_time_).seconds();
  last_time_ = stamp;

  int64_t delta_l = msg->data[0];
  int64_t delta_r = msg->data[1];

  if (invert_left_) delta_l = -delta_l;
  if (invert_right_) delta_r = -delta_r;

  const double wheel_angle_left = 2.0 * PI_CONST * (static_cast<double>(delta_l) / ticks_per_rev_left_);
  const double wheel_angle_right = 2.0 * PI_CONST * (static_cast<double>(delta_r) / ticks_per_rev_right_);

  const double d_left = wheel_radius_ * wheel_angle_left;
  const double d_right = wheel_radius_ * wheel_angle_right;

  publishOdometry(d_left, d_right, stamp);

  (void)dt;
}

void EncoderOdomNode::publishOdometry(
  double d_left, double d_right,
  const rclcpp::Time & stamp)
{
  const double d_center = 0.5 * (d_left + d_right);
  const double d_theta = (d_right - d_left) / wheel_separation_;

  const double yaw_mid = yaw_ + 0.5 * d_theta;

  x_ += d_center * std::cos(yaw_mid);
  y_ += d_center * std::sin(yaw_mid);
  yaw_ += d_theta;

  nav_msgs::msg::Odometry odom;
  odom.header.stamp = stamp;
  odom.header.frame_id = odom_frame_;
  odom.child_frame_id = base_frame_;

  odom.pose.pose.position.x = x_;
  odom.pose.pose.position.y = y_;
  odom.pose.pose.position.z = 0.0;
  odom.pose.pose.orientation = yawToQuaternion(yaw_);

  odom.twist.twist.linear.x = d_center;
  odom.twist.twist.linear.y = 0.0;
  odom.twist.twist.angular.z = d_theta;

  odom_pub_->publish(odom);

  if (publish_tf_ && tf_broadcaster_) {
    geometry_msgs::msg::TransformStamped tf_msg;
    tf_msg.header.stamp = stamp;
    tf_msg.header.frame_id = odom_frame_;
    tf_msg.child_frame_id = base_frame_;
    tf_msg.transform.translation.x = x_;
    tf_msg.transform.translation.y = y_;
    tf_msg.transform.translation.z = 0.0;
    tf_msg.transform.rotation = yawToQuaternion(yaw_);
    tf_broadcaster_->sendTransform(tf_msg);
  }
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