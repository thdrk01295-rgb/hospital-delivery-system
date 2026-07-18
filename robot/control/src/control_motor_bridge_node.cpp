#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <rclcpp/executors/multi_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_srvs/srv/trigger.hpp>

#include "control/srv/motor_command.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace control
{

namespace
{

constexpr const char * kReadyLine = "[READY] MOTOR_CONTROLLER_READY";

std::string trimLineEnding(std::string line)
{
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
    line.pop_back();
  }
  return line;
}

std::string trimWhitespace(const std::string & value)
{
  size_t first = 0;
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
    ++first;
  }

  size_t last = value.size();
  while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
    --last;
  }

  return value.substr(first, last - first);
}

bool baudRateToTermios(int baud_rate, speed_t & speed)
{
  switch (baud_rate) {
    case 9600:
      speed = B9600;
      return true;
    case 19200:
      speed = B19200;
      return true;
    case 38400:
      speed = B38400;
      return true;
    case 57600:
      speed = B57600;
      return true;
    case 115200:
      speed = B115200;
      return true;
    default:
      return false;
  }
}

std::string toLowerAscii(std::string value)
{
  for (char & c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

bool startsWith(const std::string & value, const std::string & prefix)
{
  return value.rfind(prefix, 0) == 0;
}

bool isLogMoveDone(const std::string & line)
{
  return trimWhitespace(line) == "[LOG] 구동 동작 정상 완료.";
}

bool isOptionalHomeDone(const std::string & line)
{
  return trimWhitespace(line) == "[HOME] 선택적 원점 정렬 완료.";
}

bool isFailureResponse(const std::string & line)
{
  const std::string trimmed_line = trimWhitespace(line);
  return startsWith(trimmed_line, "[ERR]") || startsWith(trimmed_line, "[ERROR]");
}

bool isServoSuccessLine(const std::string & command, const std::string & line)
{
  if (command.size() != 2 || command[0] < '1' || command[0] > '5') {
    return false;
  }

  const char servo_command = command[1];
  if (servo_command == 'o') {
    return startsWith(line, "[DONE] SERVO:" + std::string(1, command[0]) + ":OPEN");
  }
  if (servo_command == 'c') {
    return startsWith(line, "[DONE] SERVO:" + std::string(1, command[0]) + ":CLOSE");
  }

  return false;
}

bool isDoorSuccessLine(const std::string & command, const std::string & line)
{
  return (command == "o" || command == "c") && isLogMoveDone(line);
}

bool isStepperSuccessLine(const std::string & command, const std::string & line)
{
  if (command == "w" || command == "s" || command == "e" ||
    command == "d" || command == "r" || command == "f")
  {
    return isLogMoveDone(line) || isOptionalHomeDone(line);
  }

  return false;
}

bool isLiftSuccessLine(const std::string & command, const std::string & line)
{
  if (command == "1l" || command == "2l" || command == "3l" ||
    command == "tl" || startsWith(command, "lift:"))
  {
    return startsWith(line, "[DONE] LIFT:DONE");
  }
  if (command == "hl") {
    return startsWith(line, "[DONE] LIFT:HOME");
  }
  if (command == "h") {
    return trimWhitespace(line) == "[LIFT] 엔코더 원점(0) 리셋.";
  }
  if (command == "k") {
    return startsWith(line, "[DONE] LIFT:STOP");
  }

  return false;
}

bool isResetSuccessLine(const std::string & command, const std::string & line)
{
  return command == "reset" && trimWhitespace(line) == "[HOME] 초기화 완료.";
}

bool isSuccessResponse(const std::string & command, const std::string & line)
{
  const std::string normalized_command = toLowerAscii(trimWhitespace(command));
  return isServoSuccessLine(normalized_command, line) ||
         isDoorSuccessLine(normalized_command, line) ||
         isStepperSuccessLine(normalized_command, line) ||
         isLiftSuccessLine(normalized_command, line) ||
         isResetSuccessLine(normalized_command, line);
}

}  // namespace

class ControlMotorBridgeNode : public rclcpp::Node
{
public:
  ControlMotorBridgeNode()
  : Node("control_motor_bridge_node"),
    serial_fd_(-1),
    running_(true),
    ready_(false),
    connected_(false),
    command_in_progress_(false),
    command_finished_(false),
    command_success_(false)
  {
    serial_port_ = declare_parameter<std::string>("serial_port", "/dev/ttyACM1");
    baud_rate_ = declare_parameter<int>("baud_rate", 115200);
    timeout_sec_ = declare_parameter<double>("timeout_sec", 10.0);
    lift_command_timeout_sec_ = declare_parameter<double>("lift_command_timeout_sec", 15.0);
    reset_command_timeout_sec_ = declare_parameter<double>("reset_command_timeout_sec", 25.0);
    require_ready_ = declare_parameter<bool>("require_ready", true);
    emergency_stop_command_ = declare_parameter<std::string>("emergency_stop_command", "k");

    service_callback_group_ = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    ready_timer_callback_group_ =
      create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);

    feedback_pub_ = create_publisher<std_msgs::msg::String>("/control/motor_feedback", 50);
    ready_pub_ = create_publisher<std_msgs::msg::Bool>(
      "/control/motor_ready", rclcpp::QoS(1).transient_local().reliable());
    command_srv_ = create_service<control::srv::MotorCommand>(
      "/control/motor_command",
      std::bind(
        &ControlMotorBridgeNode::handleCommand, this,
        std::placeholders::_1, std::placeholders::_2),
      rclcpp::ServicesQoS(),
      service_callback_group_);
    emergency_stop_srv_ = create_service<std_srvs::srv::Trigger>(
      "/control/motor_emergency_stop",
      std::bind(
        &ControlMotorBridgeNode::handleEmergencyStop, this,
        std::placeholders::_1, std::placeholders::_2),
      rclcpp::ServicesQoS(),
      service_callback_group_);
    ready_timer_ = create_wall_timer(
      1s,
      std::bind(&ControlMotorBridgeNode::publishPeriodicReady, this),
      ready_timer_callback_group_);

    publishReady(false);
    read_thread_ = std::thread(&ControlMotorBridgeNode::serialReadLoop, this);
  }

  ~ControlMotorBridgeNode() override
  {
    running_ = false;
    if (ready_timer_) {
      ready_timer_->cancel();
    }
    if (read_thread_.joinable()) {
      read_thread_.join();
    }
    closeSerial();
    publishReady(false);
  }

private:
  void handleCommand(
    const std::shared_ptr<control::srv::MotorCommand::Request> request,
    std::shared_ptr<control::srv::MotorCommand::Response> response)
  {
    const std::string command = trimWhitespace(request->command);
    if (command.empty()) {
      response->success = false;
      response->response = "command is empty";
      return;
    }

    if (require_ready_ && !ready_.load()) {
      response->success = false;
      response->response = "motor controller is not ready";
      return;
    }

    if (!connected_.load()) {
      response->success = false;
      response->response = "serial port is not connected";
      return;
    }

    {
      std::lock_guard<std::mutex> lock(command_mutex_);
      if (command_in_progress_) {
        response->success = false;
        response->response = "motor controller is busy";
        return;
      }

      command_in_progress_ = true;
      command_finished_ = false;
      command_success_ = false;
      command_response_.clear();
      current_command_ = command;
      last_command_line_.clear();
    }

    if (!writeSerial(command + "\n")) {
      std::lock_guard<std::mutex> lock(command_mutex_);
      command_in_progress_ = false;
      current_command_.clear();
      response->success = false;
      response->response = "failed to write command to serial port";
      return;
    }

    const double applied_timeout_sec = timeoutForCommand(command);

    std::unique_lock<std::mutex> lock(command_mutex_);
    const bool completed = command_cv_.wait_for(
      lock, std::chrono::duration<double>(applied_timeout_sec),
      [this]() { return command_finished_; });

    if (completed) {
      response->success = command_success_;
      response->response = command_response_;
    } else {
      response->success = false;
      response->response = "timeout waiting for motor controller response";
      RCLCPP_WARN(
        get_logger(),
        "Timeout waiting for motor controller response: command='%s', timeout=%.3f sec",
        command.c_str(),
        applied_timeout_sec);
    }

    command_in_progress_ = false;
    command_finished_ = false;
    current_command_.clear();
  }

  void handleEmergencyStop(
    const std::shared_ptr<std_srvs::srv::Trigger::Request> request,
    std::shared_ptr<std_srvs::srv::Trigger::Response> response)
  {
    (void)request;

    const std::string command = trimWhitespace(emergency_stop_command_);
    if (command.empty()) {
      response->success = false;
      response->message = "emergency stop command is empty";
      return;
    }

    if (!connected_.load()) {
      response->success = false;
      response->message = "motor serial is not connected";
      return;
    }

    if (!writeSerial(command + "\n")) {
      response->success = false;
      response->message = "failed to write lift emergency stop command";
      return;
    }

    response->success = true;
    response->message = "lift emergency stop command sent";
  }

  bool openSerial()
  {
    speed_t speed;
    if (!baudRateToTermios(baud_rate_, speed)) {
      RCLCPP_ERROR(get_logger(), "Unsupported baud_rate: %d", baud_rate_);
      return false;
    }

    const int fd = open(serial_port_.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
      RCLCPP_ERROR_THROTTLE(
        get_logger(), *get_clock(), 5000,
        "Failed to open serial port %s: %s", serial_port_.c_str(), std::strerror(errno));
      return false;
    }

    termios tty;
    std::memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0) {
      RCLCPP_ERROR(
        get_logger(), "tcgetattr failed for %s: %s", serial_port_.c_str(), std::strerror(errno));
      close(fd);
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

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
      RCLCPP_ERROR(
        get_logger(), "tcsetattr failed for %s: %s", serial_port_.c_str(), std::strerror(errno));
      close(fd);
      return false;
    }

    tcflush(fd, TCIOFLUSH);

    {
      std::lock_guard<std::mutex> lock(serial_mutex_);
      serial_fd_ = fd;
    }

    connected_ = true;
    ready_ = false;
    publishReady(false);
    RCLCPP_INFO(get_logger(), "Serial connected: %s (%d bps)", serial_port_.c_str(), baud_rate_);
    return true;
  }

  void closeSerial()
  {
    std::lock_guard<std::mutex> lock(serial_mutex_);
    if (serial_fd_ >= 0) {
      close(serial_fd_);
      serial_fd_ = -1;
    }
    ready_ = false;
    connected_ = false;
  }

  bool writeSerial(const std::string & data)
  {
    std::lock_guard<std::mutex> write_lock(serial_write_mutex_);
    std::lock_guard<std::mutex> lock(serial_mutex_);
    if (serial_fd_ < 0) {
      return false;
    }

    size_t total_written = 0;
    while (total_written < data.size()) {
      const ssize_t written = write(
        serial_fd_, data.data() + total_written, data.size() - total_written);
      if (written < 0) {
        if (errno == EINTR) {
          continue;
        }
        RCLCPP_ERROR(get_logger(), "Serial write failed: %s", std::strerror(errno));
        return false;
      }
      total_written += static_cast<size_t>(written);
    }
    return true;
  }

  void serialReadLoop()
  {
    std::string buffer;
    char read_buffer[256];

    while (running_) {
      if (!connected_.load()) {
        ready_ = false;
        if (!openSerial()) {
          std::this_thread::sleep_for(1s);
        }
        continue;
      }

      int fd = -1;
      {
        std::lock_guard<std::mutex> lock(serial_mutex_);
        fd = serial_fd_;
      }

      if (fd < 0) {
        connected_ = false;
        continue;
      }

      const ssize_t bytes_read = read(fd, read_buffer, sizeof(read_buffer));
      if (bytes_read > 0) {
        buffer.append(read_buffer, static_cast<size_t>(bytes_read));
        processBuffer(buffer);
      } else if (bytes_read < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
          continue;
        }

        RCLCPP_ERROR(get_logger(), "Serial read failed: %s", std::strerror(errno));
        closeSerial();
        ready_ = false;
        publishReady(false);
        failActiveCommand("serial connection lost");
        buffer.clear();
      }
    }

    ready_ = false;
    connected_ = false;
    publishReady(false);
  }

  void processBuffer(std::string & buffer)
  {
    size_t newline_pos = buffer.find('\n');
    while (newline_pos != std::string::npos) {
      std::string line = trimLineEnding(buffer.substr(0, newline_pos + 1));
      buffer.erase(0, newline_pos + 1);
      handleSerialLine(line);
      newline_pos = buffer.find('\n');
    }
  }

  void handleSerialLine(const std::string & line)
  {
    std_msgs::msg::String feedback_msg;
    feedback_msg.data = line;
    feedback_pub_->publish(feedback_msg);

    if (line.find(kReadyLine) != std::string::npos) {
      ready_ = true;
      publishReady(true);
    }

    std::lock_guard<std::mutex> lock(command_mutex_);
    if (!command_in_progress_ || command_finished_) {
      return;
    }

    last_command_line_ = line;
    if (isFailureResponse(line)) {
      command_success_ = false;
      command_response_ = line;
      command_finished_ = true;
      command_cv_.notify_all();
    } else if (isSuccessResponse(current_command_, line)) {
      command_success_ = true;
      command_response_ = line;
      command_finished_ = true;
      command_cv_.notify_all();
    }
  }

  void failActiveCommand(const std::string & response)
  {
    std::lock_guard<std::mutex> lock(command_mutex_);
    if (!command_in_progress_ || command_finished_) {
      return;
    }

    command_success_ = false;
    command_response_ = response;
    last_command_line_ = response;
    command_finished_ = true;
    command_cv_.notify_all();
  }

  void publishReady(bool ready)
  {
    std_msgs::msg::Bool msg;
    msg.data = ready;
    ready_pub_->publish(msg);
  }

  void publishPeriodicReady()
  {
    publishReady(connected_.load() && ready_.load());
  }

  double timeoutForCommand(const std::string & command) const
  {
    if (command == "reset") {
      return reset_command_timeout_sec_;
    }
    if (usesLiftCommandTimeout(command)) {
      return lift_command_timeout_sec_;
    }
    return timeout_sec_;
  }

  bool usesLiftCommandTimeout(const std::string & command) const
  {
    return command == "1l" || command == "2l" || command == "3l" ||
           command == "tl" || command == "hl";
  }

  std::string serial_port_;
  int baud_rate_;
  double timeout_sec_;
  double lift_command_timeout_sec_;
  double reset_command_timeout_sec_;
  bool require_ready_;
  std::string emergency_stop_command_;

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr feedback_pub_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr ready_pub_;
  rclcpp::Service<control::srv::MotorCommand>::SharedPtr command_srv_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr emergency_stop_srv_;
  rclcpp::TimerBase::SharedPtr ready_timer_;
  rclcpp::CallbackGroup::SharedPtr service_callback_group_;
  rclcpp::CallbackGroup::SharedPtr ready_timer_callback_group_;

  int serial_fd_;
  std::mutex serial_mutex_;
  std::mutex serial_write_mutex_;
  std::thread read_thread_;
  std::atomic<bool> running_;
  std::atomic<bool> ready_;
  std::atomic<bool> connected_;

  std::mutex command_mutex_;
  std::condition_variable command_cv_;
  bool command_in_progress_;
  bool command_finished_;
  bool command_success_;
  std::string command_response_;
  std::string current_command_;
  std::string last_command_line_;
};

}  // namespace control

#ifndef CONTROL_MOTOR_BRIDGE_NODE_DISABLE_MAIN
int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<control::ControlMotorBridgeNode>();
  rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions(), 2);
  executor.add_node(node);
  executor.spin();
  rclcpp::shutdown();
  return 0;
}
#endif
