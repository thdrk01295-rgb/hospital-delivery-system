#ifndef MOTOR_BRIDGE__MOTOR_BRIDGE_HPP_
#define MOTOR_BRIDGE__MOTOR_BRIDGE_HPP_

#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/float32_multi_array.hpp>
#include <std_msgs/msg/int64_multi_array.hpp>
#include <sensor_msgs/msg/joint_state.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace motor_bridge
{

class MotorBridgeNode : public rclcpp::Node
{
public:
  MotorBridgeNode();
  ~MotorBridgeNode() override;

private:
  bool openSerial(const std::string & port, int baudrate);
  void closeSerial();
  void sendSerial(const std::string & data);
  void serialReadLoop();
  void handleIncomingLine(const std::string & line);

  bool parseFeedbackLine(
    const std::string & line,
    float & rpm_l, float & rpm_r,
    int32_t & stk_l, int32_t & stk_r,
    uint32_t & tot_l, uint32_t & tot_r);

  void publishFeedback(
    float rpm_l, float rpm_r,
    int32_t stk_l, int32_t stk_r,
    uint32_t tot_l, uint32_t tot_r);

  void cmdVelCallback(const geometry_msgs::msg::Twist::SharedPtr msg);
  void watchdogCallback();
  void publishJointStateTimer();

  double clamp(double value, double min_val, double max_val) const;
  double rpmToRadPerSec(double rpm) const;

  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_sub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr raw_pub_;
  rclcpp::Publisher<std_msgs::msg::Float32MultiArray>::SharedPtr wheel_state_pub_;
  rclcpp::Publisher<std_msgs::msg::Int64MultiArray>::SharedPtr encoder_ticks_pub_;
  rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr joint_state_pub_;

  rclcpp::TimerBase::SharedPtr watchdog_timer_;
  rclcpp::TimerBase::SharedPtr joint_timer_;

  std::string port_;
  int baudrate_;
  double wheel_separation_;
  double max_linear_vel_;
  double watchdog_timeout_;

  int serial_fd_;
  std::mutex serial_mutex_;
  std::thread rx_thread_;
  std::atomic<bool> rx_running_;

  rclcpp::Time last_cmd_time_;
  rclcpp::Time last_feedback_time_;
  rclcpp::Time last_joint_update_time_;

  double current_rpm_left_;
  double current_rpm_right_;

  double left_pos_rad_;
  double right_pos_rad_;
};

}  // namespace motor_bridge

#endif  // MOTOR_BRIDGE__MOTOR_BRIDGE_HPP_