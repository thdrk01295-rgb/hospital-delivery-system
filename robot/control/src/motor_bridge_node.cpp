#include "motor_bridge_node.hpp"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <vector>

using namespace std::chrono_literals;

namespace motor_bridge
{

static constexpr double PI_CONST = 3.14159265358979323846;

namespace
{

std::string trim(std::string value)
{
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }

  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

bool parseInt64Token(const std::string & token, int64_t & value)
{
  const std::string trimmed = trim(token);
  if (trimmed.empty()) {
    return false;
  }

  char * end = nullptr;
  errno = 0;
  const long long parsed = std::strtoll(trimmed.c_str(), &end, 10);
  if (errno != 0 || end == trimmed.c_str() || *end != '\0') {
    return false;
  }

  value = static_cast<int64_t>(parsed);
  return true;
}

bool parseFloatToken(const std::string & token, float & value)
{
  const std::string trimmed = trim(token);
  if (trimmed.empty()) {
    return false;
  }

  char * end = nullptr;
  errno = 0;
  const float parsed = std::strtof(trimmed.c_str(), &end);
  if (errno != 0 || end == trimmed.c_str() || *end != '\0') {
    return false;
  }

  value = parsed;
  return true;
}

bool startsWithKnownFeedbackPrefix(const std::string & line)
{
  return line.rfind("ENC:", 0) == 0 ||
         line.rfind("OK:", 0) == 0 ||
         line.rfind("ERR:", 0) == 0;
}

std::string resyncFeedbackLine(const std::string & line, bool & resynced)
{
  resynced = false;

  const size_t first_enc = line.find("ENC:");
  if (first_enc != std::string::npos) {
    const size_t last_enc = line.rfind("ENC:");
    if (last_enc != first_enc) {
      resynced = true;
      return line.substr(last_enc);
    }
  }

  if (startsWithKnownFeedbackPrefix(line)) {
    return line;
  }

  size_t best_pos = std::string::npos;
  for (const char * prefix : {"ENC:", "OK:", "ERR:"}) {
    const size_t pos = line.find(prefix);
    if (pos != std::string::npos && (best_pos == std::string::npos || pos < best_pos)) {
      best_pos = pos;
    }
  }

  if (best_pos != std::string::npos) {
    resynced = true;
    return line.substr(best_pos);
  }

  return line;
}

}  // namespace

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
  declare_parameter<double>("wheel_separation", 0.35);
  declare_parameter<double>("wheel_radius", 0.0625);
  declare_parameter<double>("max_motor_rpm", 204.0);
  declare_parameter<double>("watchdog_timeout", 0.5);

  port_             = get_parameter("port").as_string();
  baudrate_         = get_parameter("baudrate").as_int();
  wheel_separation_ = get_parameter("wheel_separation").as_double();
  wheel_radius_     = get_parameter("wheel_radius").as_double();
  max_motor_rpm_    = get_parameter("max_motor_rpm").as_double();
  watchdog_timeout_ = get_parameter("watchdog_timeout").as_double();

  if (wheel_radius_ <= 0.0) {
    RCLCPP_ERROR(get_logger(), "wheel_radius must be greater than 0.0");
  }
  if (max_motor_rpm_ <= 0.0) {
    RCLCPP_ERROR(get_logger(), "max_motor_rpm must be greater than 0.0");
  }

  cmd_sub_ = create_subscription<geometry_msgs::msg::Twist>(
    "/cmd_vel", 10,
    std::bind(&MotorBridgeNode::cmdVelCallback, this, std::placeholders::_1));

  raw_pub_           = create_publisher<std_msgs::msg::String>("/motor_feedback_raw", 10);
  command_raw_pub_   = create_publisher<std_msgs::msg::String>("/motor_command_raw", 10);
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

void MotorBridgeNode::sendMotorCommand(int left_pct, int right_pct)
{
  left_pct = static_cast<int>(clamp(left_pct, -100.0, 100.0));
  right_pct = static_cast<int>(clamp(right_pct, -100.0, 100.0));

  char buf[24];
  std::snprintf(buf, sizeof(buf), "CMD:%d,%d", left_pct, right_pct);

  std_msgs::msg::String msg;
  msg.data = buf;
  command_raw_pub_->publish(msg);

  sendSerial(msg.data + "\n");
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

  if (wheel_radius_ <= 0.0 || max_motor_rpm_ <= 0.0) {
    RCLCPP_WARN_THROTTLE(
      get_logger(), *get_clock(), 1000,
      "wheel_radius and max_motor_rpm must be greater than 0. Sending stop command.");
    sendMotorCommand(0, 0);
    last_cmd_time_ = now();
    return;
  }

  const double wheel_circumference = 2.0 * PI_CONST * wheel_radius_;
  const double rpm_left = v_left / wheel_circumference * 60.0;
  const double rpm_right = v_right / wheel_circumference * 60.0;

  const int pct_left = static_cast<int>(std::round(rpm_left / max_motor_rpm_ * 100.0));
  const int pct_right = static_cast<int>(std::round(rpm_right / max_motor_rpm_ * 100.0));
  const int cmd_left = static_cast<int>(clamp(pct_left, -100.0, 100.0));
  const int cmd_right = static_cast<int>(clamp(pct_right, -100.0, 100.0));

  RCLCPP_DEBUG_THROTTLE(
    get_logger(), *get_clock(), 1000,
    "cmd_vel v=%.3f w=%.3f wheel_v=(%.3f, %.3f) rpm=(%.3f, %.3f) cmd=(%d, %d)",
    v, w, v_left, v_right, rpm_left, rpm_right, cmd_left, cmd_right);

  sendMotorCommand(cmd_left, cmd_right);

  last_cmd_time_ = now();
}

void MotorBridgeNode::watchdogCallback()
{
  if ((now() - last_cmd_time_).seconds() > watchdog_timeout_) {
    sendMotorCommand(0, 0);
  }
}

// ---------------------------------------------------------------------------
// Serial RX
// ---------------------------------------------------------------------------

void MotorBridgeNode::serialReadLoop()
{
  char read_buf[256];

  while (rx_running_) {
    ssize_t bytes_read = 0;

    {
      std::lock_guard<std::mutex> lock(serial_mutex_);
      if (serial_fd_ >= 0) {
        bytes_read = read(serial_fd_, read_buf, sizeof(read_buf));
      }
    }

    if (bytes_read > 0) {
      serial_rx_buffer_.append(read_buf, static_cast<size_t>(bytes_read));

      size_t newline_pos = serial_rx_buffer_.find('\n');
      while (newline_pos != std::string::npos) {
        std::string line = trim(serial_rx_buffer_.substr(0, newline_pos));
        serial_rx_buffer_.erase(0, newline_pos + 1);

        if (!line.empty()) {
          handleIncomingLine(line);
        }

        newline_pos = serial_rx_buffer_.find('\n');
      }

      if (serial_rx_buffer_.size() > 4096) {
        RCLCPP_WARN(
          get_logger(),
          "Serial feedback line exceeded 4096 bytes without newline. Dropping buffered data.");
        serial_rx_buffer_.clear();
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

  bool resynced = false;
  const std::string feedback_line = resyncFeedbackLine(line, resynced);
  if (resynced) {
    RCLCPP_DEBUG(
      get_logger(), "Resynchronized STM32 feedback line: %s -> %s",
      line.c_str(), feedback_line.c_str());
  }

  if (feedback_line.rfind("ENC:", 0) == 0) {
    int64_t left_delta_ticks, right_delta_ticks;
    float left_rpm, right_rpm;

    if (parseEncoderLine(feedback_line, left_delta_ticks, right_delta_ticks, left_rpm, right_rpm)) {
      publishFeedback(left_delta_ticks, right_delta_ticks, left_rpm, right_rpm);
      last_feedback_time_ = now();
    } else {
      RCLCPP_WARN(get_logger(), "Failed to parse feedback: %s", feedback_line.c_str());
    }

  } else if (feedback_line.rfind("OK:", 0) == 0) {
    RCLCPP_DEBUG(get_logger(), "STM32 OK: %s", feedback_line.c_str());

  } else if (feedback_line.rfind("ERR:", 0) == 0) {
    RCLCPP_WARN(get_logger(), "STM32 error: %s", feedback_line.c_str());

  } else {
    RCLCPP_WARN(get_logger(), "Unknown STM32 feedback line: %s", feedback_line.c_str());
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
  if (line.rfind("ENC:", 0) != 0) {
    return false;
  }

  std::vector<std::string> tokens;
  std::stringstream stream(line.substr(4));
  std::string token;

  while (std::getline(stream, token, ',')) {
    tokens.push_back(token);
  }

  return tokens.size() >= 4 &&
         parseInt64Token(tokens[0], left_delta_ticks) &&
         parseInt64Token(tokens[1], right_delta_ticks) &&
         parseFloatToken(tokens[2], left_rpm) &&
         parseFloatToken(tokens[3], right_rpm);
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
