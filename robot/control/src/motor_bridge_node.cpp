#include "motor_bridge_node.hpp"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <regex>
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
  target_rpm_left_(0.0),
  target_rpm_right_(0.0),
  total_tick_left_(0),
  total_tick_right_(0),
  left_pos_rad_(0.0),
  right_pos_rad_(0.0)
{
  declare_parameter<std::string>("port", "/dev/ttyUSB0");
  declare_parameter<int>("baudrate", 115200);
  declare_parameter<double>("wheel_radius", 0.0625);
  declare_parameter<double>("wheel_separation", 0.330);
  declare_parameter<double>("max_rpm", 60.0);
  declare_parameter<double>("watchdog_timeout", 0.5);
  declare_parameter<bool>("invert_left", false);
  declare_parameter<bool>("invert_right", false);

  port_ = get_parameter("port").as_string();
  baudrate_ = get_parameter("baudrate").as_int();
  wheel_radius_ = get_parameter("wheel_radius").as_double();
  wheel_separation_ = get_parameter("wheel_separation").as_double();
  max_rpm_ = get_parameter("max_rpm").as_double();
  watchdog_timeout_ = get_parameter("watchdog_timeout").as_double();
  invert_left_ = get_parameter("invert_left").as_bool();
  invert_right_ = get_parameter("invert_right").as_bool();

  cmd_sub_ = create_subscription<geometry_msgs::msg::Twist>(
    "/cmd_vel", 10,
    std::bind(&MotorBridgeNode::cmdVelCallback, this, std::placeholders::_1));

  raw_pub_ = create_publisher<std_msgs::msg::String>("/motor_feedback_raw", 10);
  wheel_state_pub_ = create_publisher<std_msgs::msg::Float32MultiArray>("/wheel_state", 10);
  encoder_ticks_pub_ = create_publisher<std_msgs::msg::Int64MultiArray>("/encoder_ticks", 10);
  motor_status_pub_ = create_publisher<std_msgs::msg::Int32MultiArray>("/motor_status", 10);
  joint_state_pub_ = create_publisher<sensor_msgs::msg::JointState>("/joint_states", 10);

  last_cmd_time_ = now();
  last_feedback_time_ = now();
  last_joint_update_time_ = now();

  if (openSerial(port_, baudrate_)) {
    RCLCPP_INFO(get_logger(), "Serial connected: %s (%d)", port_.c_str(), baudrate_);
  } else {
    RCLCPP_ERROR(get_logger(), "Failed to open serial port: %s", port_.c_str());
  }

  rx_running_ = true;
  rx_thread_ = std::thread(&MotorBridgeNode::serialReadLoop, this);

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
    case 9600: speed = B9600; break;
    case 19200: speed = B19200; break;
    case 38400: speed = B38400; break;
    case 57600: speed = B57600; break;
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
  tty.c_lflag = 0;
  tty.c_oflag = 0;
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 1;
  tty.c_iflag &= ~(IXON | IXOFF | IXANY);
  tty.c_cflag |= (CLOCAL | CREAD);
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

double MotorBridgeNode::clamp(double value, double min_val, double max_val) const
{
  if (value < min_val) return min_val;
  if (value > max_val) return max_val;
  return value;
}

double MotorBridgeNode::linearVelToRpm(double v_mps) const
{
  const double rad_per_sec = v_mps / wheel_radius_;
  return rad_per_sec * 60.0 / (2.0 * PI_CONST);
}

double MotorBridgeNode::rpmToRadPerSec(double rpm) const
{
  return rpm * 2.0 * PI_CONST / 60.0;
}

void MotorBridgeNode::cmdVelCallback(const geometry_msgs::msg::Twist::SharedPtr msg)
{
  const double v = msg->linear.x;
  const double w = msg->angular.z;

  double v_left = v - (wheel_separation_ * 0.5) * w;
  double v_right = v + (wheel_separation_ * 0.5) * w;

  double rpm_left = linearVelToRpm(v_left);
  double rpm_right = linearVelToRpm(v_right);

  if (invert_left_) rpm_left = -rpm_left;
  if (invert_right_) rpm_right = -rpm_right;

  rpm_left = clamp(rpm_left, -max_rpm_, max_rpm_);
  rpm_right = clamp(rpm_right, -max_rpm_, max_rpm_);

  target_rpm_left_ = rpm_left;
  target_rpm_right_ = rpm_right;

  std::ostringstream oss;
  oss.setf(std::ios::fixed);
  oss.precision(2);
  oss << "CMD_VEL," << rpm_left << "," << rpm_right << "\n";

  sendSerial(oss.str());
  last_cmd_time_ = now();
}

void MotorBridgeNode::watchdogCallback()
{
  if ((now() - last_cmd_time_).seconds() > watchdog_timeout_) {
    sendSerial("MODE,STOP\n");
  }
}

void MotorBridgeNode::serialReadLoop()
{
  std::string line_buffer;
  char ch;

  while (rx_running_) {
    bool got_byte = false;

    {
      std::lock_guard<std::mutex> lock(serial_mutex_);
      if (serial_fd_ >= 0) {
        const ssize_t n = read(serial_fd_, &ch, 1);
        if (n > 0) {
          got_byte = true;
        }
      }
    }

    if (got_byte) {
      if (ch == '\n') {
        if (!line_buffer.empty()) {
          handleIncomingLine(line_buffer);
          line_buffer.clear();
        }
      } else if (ch != '\r') {
        line_buffer.push_back(ch);
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

  double cur_l, cur_r, tar_l, tar_r;
  int64_t delta_l, delta_r, total_l, total_r;
  int mode, error;

  if (parseStateLine(
      line,
      cur_l, cur_r,
      tar_l, tar_r,
      delta_l, delta_r,
      total_l, total_r,
      mode, error))
  {
    publishParsedState(
      cur_l, cur_r,
      tar_l, tar_r,
      delta_l, delta_r,
      total_l, total_r,
      mode, error);
    last_feedback_time_ = now();
  }
}

bool MotorBridgeNode::parseStateLine(
  const std::string & line,
  double & cur_l, double & cur_r,
  double & tar_l, double & tar_r,
  int64_t & delta_l, int64_t & delta_r,
  int64_t & total_l, int64_t & total_r,
  int & mode, int & error)
{
  static const std::regex pattern(
    R"(STATE,([+-]?\d+(?:\.\d+)?),([+-]?\d+(?:\.\d+)?),([+-]?\d+(?:\.\d+)?),([+-]?\d+(?:\.\d+)?),([+-]?\d+),([+-]?\d+),([+-]?\d+),([+-]?\d+),([+-]?\d+),([+-]?\d+))"
  );

  std::smatch match;
  if (!std::regex_search(line, match, pattern)) {
    return false;
  }

  cur_l = std::stod(match[1].str());
  cur_r = std::stod(match[2].str());
  tar_l = std::stod(match[3].str());
  tar_r = std::stod(match[4].str());
  delta_l = std::stoll(match[5].str());
  delta_r = std::stoll(match[6].str());
  total_l = std::stoll(match[7].str());
  total_r = std::stoll(match[8].str());
  mode = std::stoi(match[9].str());
  error = std::stoi(match[10].str());

  return true;
}

void MotorBridgeNode::publishParsedState(
  double cur_l, double cur_r,
  double tar_l, double tar_r,
  int64_t delta_l, int64_t delta_r,
  int64_t total_l, int64_t total_r,
  int mode, int error)
{
  current_rpm_left_ = cur_l;
  current_rpm_right_ = cur_r;
  target_rpm_left_ = tar_l;
  target_rpm_right_ = tar_r;
  total_tick_left_ = total_l;
  total_tick_right_ = total_r;

  std_msgs::msg::Float32MultiArray wheel_msg;
  wheel_msg.data = {
    static_cast<float>(cur_l),
    static_cast<float>(cur_r),
    static_cast<float>(tar_l),
    static_cast<float>(tar_r)
  };
  wheel_state_pub_->publish(wheel_msg);

  std_msgs::msg::Int64MultiArray tick_msg;
  tick_msg.data = {delta_l, delta_r, total_l, total_r};
  encoder_ticks_pub_->publish(tick_msg);

  std_msgs::msg::Int32MultiArray status_msg;
  status_msg.data = {mode, error};
  motor_status_pub_->publish(status_msg);
}

void MotorBridgeNode::publishJointStateTimer()
{
  const auto current_time = now();
  const double dt = (current_time - last_joint_update_time_).seconds();
  last_joint_update_time_ = current_time;

  double vel_left = rpmToRadPerSec(current_rpm_left_);
  double vel_right = rpmToRadPerSec(current_rpm_right_);

  if (invert_left_) vel_left = -vel_left;
  if (invert_right_) vel_right = -vel_right;

  left_pos_rad_ += vel_left * dt;
  right_pos_rad_ += vel_right * dt;

  sensor_msgs::msg::JointState msg;
  msg.header.stamp = current_time;
  msg.name = {"left_wheel_joint", "right_wheel_joint"};
  msg.position = {left_pos_rad_, right_pos_rad_};
  msg.velocity = {vel_left, vel_right};

  joint_state_pub_->publish(msg);
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