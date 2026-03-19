#ifndef MOTOR_BRIDGE__MOTOR_BRIDGE_HPP_
#define MOTOR_BRIDGE__MOTOR_BRIDGE_HPP_

#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/float32_multi_array.hpp>
#include <std_msgs/msg/int64_multi_array.hpp>
#include <std_msgs/msg/int32_multi_array.hpp>
#include <sensor_msgs/msg/joint_state.hpp>

#include <atomic>
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
  bool parseStateLine(
    const std::string & line,
    double & cur_l, double & cur_r,
    double & tar_l, double & tar_r,
    int64_t & delta_l, int64_t & delta_r,
    int64_t & total_l, int64_t & total_r,
    int & mode, int & error);
  void publishParsedState(
    double cur_l, double cur_r,
    double tar_l, double tar_r,
    int64_t delta_l, int64_t delta_r,
    int64_t total_l, int64_t total_r,
    int mode, int error);
  void cmdVelCallback(const geometry_msgs::msg::Twist::SharedPtr msg);
  void watchdogCallback();
  void publishJointStateTimer();

  double clamp(double value, double min_val, double max_val) const;
  double linearVelToRpm(double v_mps) const;
  double rpmToRadPerSec(double rpm) const;

  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_sub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr raw_pub_;
  rclcpp::Publisher<std_msgs::msg::Float32MultiArray>::SharedPtr wheel_state_pub_;
  rclcpp::Publisher<std_msgs::msg::Int64MultiArray>::SharedPtr encoder_ticks_pub_;
  rclcpp::Publisher<std_msgs::msg::Int32MultiArray>::SharedPtr motor_status_pub_;
  rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr joint_state_pub_;

  rclcpp::TimerBase::SharedPtr watchdog_timer_;
  rclcpp::TimerBase::SharedPtr joint_timer_;

  std::string port_;
  int baudrate_;
  double wheel_radius_;
  double wheel_separation_;
  double max_rpm_;
  double watchdog_timeout_;
  bool invert_left_;
  bool invert_right_;

  int serial_fd_;
  std::mutex serial_mutex_;
  std::thread rx_thread_;
  std::atomic<bool> rx_running_;

  rclcpp::Time last_cmd_time_;
  rclcpp::Time last_feedback_time_;
  rclcpp::Time last_joint_update_time_;

  double current_rpm_left_;
  double current_rpm_right_;
  double target_rpm_left_;
  double target_rpm_right_;

  int64_t total_tick_left_;
  int64_t total_tick_right_;

  double left_pos_rad_;
  double right_pos_rad_;
};

}  // namespace motor_bridge

#endif  // MOTOR_BRIDGE__MOTOR_BRIDGE_HPP_