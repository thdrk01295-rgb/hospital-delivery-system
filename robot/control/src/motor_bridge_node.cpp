#include "motor_bridge_node.hpp"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

using namespace std::chrono_literals;

namespace motor_bridge
{

static constexpr double PI_CONST = 3.14159265358979323846;

MotorBridgeNode::MotorBridgeNode()
: Node("motor_bridge_node"),
  serial_fd_(-1),
  rx_running_(false),
  current_rpm_left_(0.0),
  current_rpm_right_(0.0),
  left_pos_rad_(0.0),
  right_pos_rad_(0.0)
{
  declare_parameter<std::string>("port", "/dev/ttyUSB0");
  declare_parameter<int>("baudrate", 115200);
  declare_parameter<double>("wheel_separation", 0.38);
  declare_parameter<double>("max_linear_vel", 1.0);
  declare_parameter<double>("watchdog_timeout", 0.5);

  port_             = get_parameter("port").as_string();
  baudrate_         = get_parameter("baudrate").as_int();
  wheel_separation_ = get_parameter("wheel_separation").as_double();
  max_linear_vel_   = get_parameter("max_linear_vel").as_double();
  watchdog_timeout_ = get_parameter("watchdog_timeout").as_double();

  cmd_sub_ = create_subscription<geometry_msgs::msg::Twist>(
    "/cmd_vel", 10,
    std::bind(&MotorBridgeNode::cmdVelCallback, this, std::placeholders::_1));

  raw_pub_           = create_publisher<std_msgs::msg::String>("/motor_feedback_raw", 10);
  wheel_state_pub_   = create_publisher<std_msgs::msg::Float32MultiArray>("/wheel_state", 10);
  encoder_ticks_pub_ = create_publisher<std_msgs::msg::Int64MultiArray>("/encoder_ticks", 10);
  joint_state_pub_   = create_publisher<sensor_msgs::msg::JointState>("/joint_states", 10);

  last_cmd_time_        = now();
  last_feedback_time_   = now();
  last_joint_update_time_ = now();

  if (openSerial(port_, baudrate_)) {
    RCLCPP_INFO(get_logger(), "Serial connected: %s (%d bps)", port_.c_str(), baudrate_);
  } else {
    RCLCPP_ERROR(get_logger(), "Failed to open serial port: %s", port_.c_str());
  }

  rx_running_ = true;
  rx_thread_  = std::thread(&MotorBridgeNode::serialReadLoop, this);

  watchdog_timer_ = create_wall_timer(
    100ms, std::bind(&MotorBridgeNode::watchdogCallback, this));

  joint_timer_ = create_wall_timer(
    20ms, std::bind(&MotorBridgeNode::publishJointStateTimer, this));
}

MotorBridgeNode::~MotorBridgeNode()
{
  rx_running_ = false;
  if (rx_thread_.joinable()) {
    rx_thread_.join();
  }
  closeSerial();
}

// ---------------------------------------------------------------------------
// Serial helpers
// ---------------------------------------------------------------------------

bool MotorBridgeNode::openSerial(const std::string & port, int baudrate)
{
  serial_fd_ = open(port.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
  if (serial_fd_ < 0) {
    return false;
  }

  struct termios tty;
  std::memset(&tty, 0, sizeof(tty));
  if (tcgetattr(serial_fd_, &tty) != 0) {
    close(serial_fd_);
    serial_fd_ = -1;
    return false;
  }

  speed_t speed;
  switch (baudrate) {
    case 9600:   speed = B9600;   break;
    case 19200:  speed = B19200;  break;
    case 38400:  speed = B38400;  break;
    case 57600:  speed = B57600;  break;
    case 115200: speed = B115200; break;
    default:
      close(serial_fd_);
      serial_fd_ = -1;
      return false;
  }

  cfsetospeed(&tty, speed);
  cfsetispeed(&tty, speed);

  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
  tty.c_iflag &= ~IGNBRK;
  tty.c_lflag  = 0;
  tty.c_oflag  = 0;
  tty.c_cc[VMIN]  = 0;
  tty.c_cc[VTIME] = 1;
  tty.c_iflag &= ~(IXON | IXOFF | IXANY);
  tty.c_cflag |=  (CLOCAL | CREAD);
  tty.c_cflag &= ~(PARENB | PARODD);
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag &= ~CRTSCTS;
  tty.c_iflag &= ~(INLCR | ICRNL);
  tty.c_oflag &= ~(ONLCR | OCRNL);

  if (tcsetattr(serial_fd_, TCSANOW, &tty) != 0) {
    close(serial_fd_);
    serial_fd_ = -1;
    return false;
  }

  tcflush(serial_fd_, TCIOFLUSH);
  return true;
}

void MotorBridgeNode::closeSerial()
{
  std::lock_guard<std::mutex> lock(serial_mutex_);
  if (serial_fd_ >= 0) {
    close(serial_fd_);
    serial_fd_ = -1;
  }
}

void MotorBridgeNode::sendSerial(const std::string & data)
{
  std::lock_guard<std::mutex> lock(serial_mutex_);
  if (serial_fd_ < 0) {
    return;
  }
  const ssize_t written = write(serial_fd_, data.c_str(), data.size());
  if (written < 0) {
    RCLCPP_ERROR(get_logger(), "Serial write failed");
  }
}

// ---------------------------------------------------------------------------
// cmd_vel → serial
// ---------------------------------------------------------------------------

void MotorBridgeNode::cmdVelCallback(const geometry_msgs::msg::Twist::SharedPtr msg)
{
  const double v = msg->linear.x;
  const double w = msg->angular.z;

  const double v_left  = v - (wheel_separation_ * 0.5) * w;
  const double v_right = v + (wheel_separation_ * 0.5) * w;

  int pct_left  = static_cast<int>(std::round(v_left  / max_linear_vel_ * 100.0));
  int pct_right = static_cast<int>(std::round(v_right / max_linear_vel_ * 100.0));

  if (pct_left  >  100) pct_left  =  100;
  if (pct_left  < -100) pct_left  = -100;
  if (pct_right >  100) pct_right =  100;
  if (pct_right < -100) pct_right = -100;

  char buf[16];
  std::snprintf(buf, sizeof(buf), "%d,%d\r\n", pct_left, pct_right);
  sendSerial(buf);

  last_cmd_time_ = now();
}

void MotorBridgeNode::watchdogCallback()
{
  if ((now() - last_cmd_time_).seconds() > watchdog_timeout_) {
    sendSerial("0,0\r\n");
  }
}

// ---------------------------------------------------------------------------
// Serial RX
// ---------------------------------------------------------------------------

void MotorBridgeNode::serialReadLoop()
{
  std::string line_buf;
  char ch;

  while (rx_running_) {
    bool got_byte = false;

    {
      std::lock_guard<std::mutex> lock(serial_mutex_);
      if (serial_fd_ >= 0) {
        got_byte = (read(serial_fd_, &ch, 1) > 0);
      }
    }

    if (got_byte) {
      if (ch == '\n') {
        if (!line_buf.empty()) {
          handleIncomingLine(line_buf);
          line_buf.clear();
        }
      } else if (ch != '\r') {
        line_buf.push_back(ch);
      }
    }

    std::this_thread::sleep_for(2ms);
  }
}

void MotorBridgeNode::handleIncomingLine(const std::string & line)
{
  std_msgs::msg::String raw_msg;
  raw_msg.data = line;
  raw_pub_->publish(raw_msg);

  if (line.rfind("ENC:", 0) == 0) {
    int64_t left_delta_ticks, right_delta_ticks;
    float left_rpm, right_rpm;

    if (parseEncoderLine(line, left_delta_ticks, right_delta_ticks, left_rpm, right_rpm)) {
      publishFeedback(left_delta_ticks, right_delta_ticks, left_rpm, right_rpm);
      last_feedback_time_ = now();
    } else {
      RCLCPP_WARN(get_logger(), "Failed to parse feedback: %s", line.c_str());
    }

  } else if (line.rfind("OK", 0) == 0) {
    RCLCPP_DEBUG(get_logger(), "ACK: %s", line.c_str());

  } else if (line.rfind("ERR", 0) == 0) {
    RCLCPP_WARN(get_logger(), "STM32 error: %s", line.c_str());
  }
}

// ---------------------------------------------------------------------------
// Feedback parsing and publishing
// ---------------------------------------------------------------------------

bool MotorBridgeNode::parseEncoderLine(
  const std::string & line,
  int64_t & left_delta_ticks, int64_t & right_delta_ticks,
  float & left_rpm, float & right_rpm)
{
  long long parsed_left_ticks = 0;
  long long parsed_right_ticks = 0;

  const int n = std::sscanf(
    line.c_str(),
    "ENC:%lld,%lld,%f,%f",
    &parsed_left_ticks,
    &parsed_right_ticks,
    &left_rpm,
    &right_rpm);

  if (n != 4) {
    return false;
  }

  left_delta_ticks = static_cast<int64_t>(parsed_left_ticks);
  right_delta_ticks = static_cast<int64_t>(parsed_right_ticks);
  return true;
}

void MotorBridgeNode::publishFeedback(
  int64_t left_delta_ticks, int64_t right_delta_ticks,
  float left_rpm, float right_rpm)
{
  current_rpm_left_ = left_rpm;
  current_rpm_right_ = right_rpm;

  std_msgs::msg::Float32MultiArray wheel_msg;
  wheel_msg.data = {left_rpm, right_rpm};
  wheel_state_pub_->publish(wheel_msg);

  std_msgs::msg::Int64MultiArray tick_msg;
  tick_msg.data = {
    left_delta_ticks,
    right_delta_ticks
  };
  encoder_ticks_pub_->publish(tick_msg);
}

// ---------------------------------------------------------------------------
// Joint state (for URDF / RViz)
// ---------------------------------------------------------------------------

void MotorBridgeNode::publishJointStateTimer()
{
  const auto current_time = now();
  const double dt = (current_time - last_joint_update_time_).seconds();
  last_joint_update_time_ = current_time;

  const double vel_left  = rpmToRadPerSec(current_rpm_left_);
  const double vel_right = rpmToRadPerSec(current_rpm_right_);

  left_pos_rad_  += vel_left  * dt;
  right_pos_rad_ += vel_right * dt;

  sensor_msgs::msg::JointState msg;
  msg.header.stamp = current_time;
  msg.name         = {"left_wheel_joint", "right_wheel_joint"};
  msg.position     = {left_pos_rad_, right_pos_rad_};
  msg.velocity     = {vel_left, vel_right};

  joint_state_pub_->publish(msg);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

double MotorBridgeNode::clamp(double value, double min_val, double max_val) const
{
  if (value < min_val) return min_val;
  if (value > max_val) return max_val;
  return value;
}

double MotorBridgeNode::rpmToRadPerSec(double rpm) const
{
  return rpm * 2.0 * PI_CONST / 60.0;
}

}  // namespace motor_bridge

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<motor_bridge::MotorBridgeNode>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
