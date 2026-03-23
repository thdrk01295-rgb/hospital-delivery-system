#include <cmath>
#include <memory>
#include <regex>
#include <string>
#include <vector>
#include <sstream>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "tf2_ros/transform_broadcaster.h"
#include "tf2/LinearMath/Quaternion.h"

class EncoderOdomNode : public rclcpp::Node
{
public:
  EncoderOdomNode()
  : Node("encoder_odom_node"),
    x_(0.0),
    y_(0.0),
    yaw_(0.0),
    last_time_(this->now()),
    last_total_l_(0),
    last_total_r_(0),
    has_prev_total_(false)
  {
    wheel_radius_ = this->declare_parameter<double>("wheel_radius", 0.05);
    wheel_separation_ = this->declare_parameter<double>("wheel_separation", 0.30);
    ticks_per_revolution_ = this->declare_parameter<int>("ticks_per_revolution", 2048);

    odom_topic_ = this->declare_parameter<std::string>("odom_topic", "/odom");
    feedback_topic_ = this->declare_parameter<std::string>("feedback_topic", "/motor_feedback_raw");

    odom_frame_ = this->declare_parameter<std::string>("odom_frame", "odom");
    base_frame_ = this->declare_parameter<std::string>("base_frame", "base_link");
    publish_tf_ = this->declare_parameter<bool>("publish_tf", true);

    use_total_counts_ = this->declare_parameter<bool>("use_total_counts", true);

    odom_pub_ = this->create_publisher<nav_msgs::msg::Odometry>(odom_topic_, 20);
    tf_broadcaster_ = std::make_shared<tf2_ros::TransformBroadcaster>(*this);

    feedback_sub_ = this->create_subscription<std_msgs::msg::String>(
      feedback_topic_, 50,
      std::bind(&EncoderOdomNode::feedbackCallback, this, std::placeholders::_1));

    RCLCPP_INFO(this->get_logger(), "encoder_odom_node started");
    RCLCPP_INFO(this->get_logger(), "feedback_topic: %s", feedback_topic_.c_str());
    RCLCPP_INFO(this->get_logger(), "odom_topic: %s", odom_topic_.c_str());
  }

private:
  bool parseStateLine(
    const std::string & line,
    int64_t & delta_l, int64_t & delta_r,
    int64_t & total_l, int64_t & total_r)
  {
    std::vector<std::string> tokens;
    std::stringstream ss(line);
    std::string item;

    while (std::getline(ss, item, ',')) {
      tokens.push_back(item);
    }

    if (tokens.size() < 11) {
      return false;
    }

    if (tokens[0] != "STATE") {
      return false;
    }

    try {
      delta_l = std::stoll(tokens[5]);
      delta_r = std::stoll(tokens[6]);
      total_l = std::stoll(tokens[7]);
      total_r = std::stoll(tokens[8]);
    } catch (...) {
      return false;
    }

    return true;
  }

  void feedbackCallback(const std_msgs::msg::String::SharedPtr msg)
  {
    int64_t delta_l = 0;
    int64_t delta_r = 0;
    int64_t total_l = 0;
    int64_t total_r = 0;

    if (!parseStateLine(msg->data, delta_l, delta_r, total_l, total_r)) {
      RCLCPP_WARN_THROTTLE(
        this->get_logger(), *this->get_clock(), 2000,
        "Failed to parse motor feedback: %s", msg->data.c_str());
      return;
    }

    const auto now = this->now();
    double dt = (now - last_time_).seconds();
    if (dt <= 0.0) {
      dt = 1e-3;
    }

    int64_t used_delta_l = delta_l;
    int64_t used_delta_r = delta_r;

    if (use_total_counts_) {
      if (!has_prev_total_) {
        last_total_l_ = total_l;
        last_total_r_ = total_r;
        last_time_ = now;
        has_prev_total_ = true;
        return;
      }

      used_delta_l = total_l - last_total_l_;
      used_delta_r = total_r - last_total_r_;

      last_total_l_ = total_l;
      last_total_r_ = total_r;
    }

    const double meters_per_tick =
      (2.0 * M_PI * wheel_radius_) / static_cast<double>(ticks_per_revolution_);

    const double dist_l = static_cast<double>(used_delta_l) * meters_per_tick;
    const double dist_r = static_cast<double>(used_delta_r) * meters_per_tick;

    const double dist = 0.5 * (dist_l + dist_r);
    const double dtheta = (dist_r - dist_l) / wheel_separation_;

    const double dx = dist * std::cos(yaw_ + dtheta * 0.5);
    const double dy = dist * std::sin(yaw_ + dtheta * 0.5);

    x_ += dx;
    y_ += dy;
    yaw_ += dtheta;

    const double linear_vel = dist / dt;
    const double angular_vel = dtheta / dt;

    publishOdometry(now, linear_vel, angular_vel);
    if (publish_tf_) {
      publishTf(now);
    }

    last_time_ = now;
  }

  void publishOdometry(const rclcpp::Time & stamp, double linear_vel, double angular_vel)
  {
    nav_msgs::msg::Odometry odom;
    odom.header.stamp = stamp;
    odom.header.frame_id = odom_frame_;
    odom.child_frame_id = base_frame_;

    odom.pose.pose.position.x = x_;
    odom.pose.pose.position.y = y_;
    odom.pose.pose.position.z = 0.0;

    tf2::Quaternion q;
    q.setRPY(0.0, 0.0, yaw_);
    odom.pose.pose.orientation.x = q.x();
    odom.pose.pose.orientation.y = q.y();
    odom.pose.pose.orientation.z = q.z();
    odom.pose.pose.orientation.w = q.w();

    odom.twist.twist.linear.x = linear_vel;
    odom.twist.twist.linear.y = 0.0;
    odom.twist.twist.angular.z = angular_vel;

    odom.pose.covariance[0] = 0.05;
    odom.pose.covariance[7] = 0.05;
    odom.pose.covariance[35] = 0.1;

    odom.twist.covariance[0] = 0.05;
    odom.twist.covariance[7] = 0.05;
    odom.twist.covariance[35] = 0.1;

    odom_pub_->publish(odom);
  }

  void publishTf(const rclcpp::Time & stamp)
  {
    geometry_msgs::msg::TransformStamped tf_msg;
    tf_msg.header.stamp = stamp;
    tf_msg.header.frame_id = odom_frame_;
    tf_msg.child_frame_id = base_frame_;

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

  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr feedback_sub_;
  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odom_pub_;
  std::shared_ptr<tf2_ros::TransformBroadcaster> tf_broadcaster_;

  double wheel_radius_;
  double wheel_separation_;
  int ticks_per_revolution_;

  std::string odom_topic_;
  std::string feedback_topic_;
  std::string odom_frame_;
  std::string base_frame_;
  bool publish_tf_;
  bool use_total_counts_;

  double x_;
  double y_;
  double yaw_;
  rclcpp::Time last_time_;

  int64_t last_total_l_;
  int64_t last_total_r_;
  bool has_prev_total_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<EncoderOdomNode>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}