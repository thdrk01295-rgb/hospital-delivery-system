#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32.hpp"
#include "std_msgs/msg/string.hpp"

namespace sensor_components
{
namespace
{
constexpr uint8_t kVoltageRegister = 0x02;
constexpr uint8_t kSocRegister = 0x04;
constexpr size_t kRegisterReadLength = 2;

std::string errnoMessage()
{
  return std::strerror(errno);
}

std::string jsonEscape(const std::string & input)
{
  std::ostringstream escaped;
  for (const char c : input) {
    switch (c) {
      case '\\':
        escaped << "\\\\";
        break;
      case '"':
        escaped << "\\\"";
        break;
      case '\b':
        escaped << "\\b";
        break;
      case '\f':
        escaped << "\\f";
        break;
      case '\n':
        escaped << "\\n";
        break;
      case '\r':
        escaped << "\\r";
        break;
      case '\t':
        escaped << "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          escaped << "\\u"
                  << std::hex << std::setw(4) << std::setfill('0')
                  << static_cast<int>(static_cast<unsigned char>(c))
                  << std::dec << std::setfill(' ');
        } else {
          escaped << c;
        }
        break;
    }
  }
  return escaped.str();
}
}  // namespace

class BatteryNode : public rclcpp::Node
{
public:
  explicit BatteryNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions())
  : Node("battery_node", options),
    robot_id_(declare_parameter<std::string>("robot_id", "AMR-001")),
    i2c_device_(declare_parameter<std::string>("i2c_device", "/dev/i2c-1")),
    i2c_address_(declare_parameter<int>("i2c_address", 0x36)),
    publish_rate_hz_(declare_parameter<double>("publish_rate_hz", 1.0)),
    warn_soc_percent_(declare_parameter<double>("warn_soc_percent", 20.0)),
    critical_soc_percent_(declare_parameter<double>("critical_soc_percent", 10.0))
  {
    if (i2c_address_ < 0x03 || i2c_address_ > 0x77) {
      RCLCPP_WARN(
        get_logger(),
        "i2c_address 0x%X is outside the normal 7-bit range; continuing with configured value",
        i2c_address_);
    }

    if (publish_rate_hz_ <= 0.0) {
      RCLCPP_WARN(
        get_logger(),
        "publish_rate_hz must be positive; using 1.0 Hz instead of %.3f",
        publish_rate_hz_);
      publish_rate_hz_ = 1.0;
    }

    battery_state_pub_ = create_publisher<std_msgs::msg::Float32>("/robot/battery_state", 10);
    battery_json_pub_ = create_publisher<std_msgs::msg::String>("/robot/battery", 10);

    const auto period = std::chrono::duration<double>(1.0 / publish_rate_hz_);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::nanoseconds>(period),
      std::bind(&BatteryNode::publishBattery, this));
  }

  ~BatteryNode() override
  {
    closeDevice();
  }

private:
  bool ensureDeviceOpen()
  {
    if (fd_ >= 0) {
      return true;
    }

    fd_ = ::open(i2c_device_.c_str(), O_RDWR);
    if (fd_ < 0) {
      RCLCPP_ERROR_THROTTLE(
        get_logger(), *get_clock(), 5000,
        "Failed to open I2C device %s: %s",
        i2c_device_.c_str(), errnoMessage().c_str());
      return false;
    }

    if (::ioctl(fd_, I2C_SLAVE, i2c_address_) < 0) {
      RCLCPP_ERROR_THROTTLE(
        get_logger(), *get_clock(), 5000,
        "Failed to set I2C address 0x%X on %s: %s",
        i2c_address_, i2c_device_.c_str(), errnoMessage().c_str());
      closeDevice();
      return false;
    }

    RCLCPP_INFO(
      get_logger(),
      "Opened battery gauge on %s at address 0x%X",
      i2c_device_.c_str(), i2c_address_);
    return true;
  }

  bool readRegister(uint8_t reg, std::array<uint8_t, kRegisterReadLength> & data)
  {
    if (!ensureDeviceOpen()) {
      return false;
    }

    uint8_t reg_buf = reg;
    std::array<i2c_msg, 2> messages{};
    messages[0].addr = static_cast<uint16_t>(i2c_address_);
    messages[0].flags = 0;
    messages[0].len = 1;
    messages[0].buf = &reg_buf;
    messages[1].addr = static_cast<uint16_t>(i2c_address_);
    messages[1].flags = I2C_M_RD;
    messages[1].len = data.size();
    messages[1].buf = data.data();

    i2c_rdwr_ioctl_data transaction{};
    transaction.msgs = messages.data();
    transaction.nmsgs = messages.size();

    if (::ioctl(fd_, I2C_RDWR, &transaction) < 0) {
      RCLCPP_ERROR_THROTTLE(
        get_logger(), *get_clock(), 5000,
        "Failed to read register 0x%02X from battery gauge at 0x%X on %s: %s",
        reg, i2c_address_, i2c_device_.c_str(), errnoMessage().c_str());
      closeDevice();
      return false;
    }

    return true;
  }

  bool readBattery(double & voltage, double & soc)
  {
    std::array<uint8_t, kRegisterReadLength> voltage_raw{};
    std::array<uint8_t, kRegisterReadLength> soc_raw{};

    if (!readRegister(kVoltageRegister, voltage_raw) || !readRegister(kSocRegister, soc_raw)) {
      return false;
    }

    voltage =
      (((static_cast<int>(voltage_raw[0]) << 4) | (static_cast<int>(voltage_raw[1]) >> 4)) * 1.25) /
      1000.0;
    soc = static_cast<double>(soc_raw[0]) + (static_cast<double>(soc_raw[1]) / 256.0);
    return true;
  }

  void publishBattery()
  {
    double voltage = 0.0;
    double soc = 0.0;
    if (!readBattery(voltage, soc)) {
      return;
    }

    RCLCPP_DEBUG(get_logger(), "Battery gauge read: voltage=%.3f V soc=%.1f%%", voltage, soc);

    if (soc <= critical_soc_percent_) {
      RCLCPP_WARN(
        get_logger(),
        "Battery SOC critical: %.1f%% <= %.1f%%",
        soc, critical_soc_percent_);
    } else if (soc <= warn_soc_percent_) {
      RCLCPP_WARN(
        get_logger(),
        "Battery SOC low: %.1f%% <= %.1f%%",
        soc, warn_soc_percent_);
    }

    auto state_msg = std_msgs::msg::Float32();
    state_msg.data = static_cast<float>(std::clamp(soc, 0.0, 100.0));
    battery_state_pub_->publish(state_msg);

    auto json_msg = std_msgs::msg::String();
    json_msg.data = makeBatteryJson(voltage, soc);
    battery_json_pub_->publish(json_msg);
  }

  std::string makeBatteryJson(double voltage, double soc) const
  {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);
    out << "{\"robot_id\":\"" << jsonEscape(robot_id_) << "\",";
    out << "\"voltage\":" << voltage << ",";
    out << std::setprecision(1);
    out << "\"soc\":" << soc << "}";
    return out.str();
  }

  void closeDevice()
  {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  std::string robot_id_;
  std::string i2c_device_;
  int i2c_address_;
  double publish_rate_hz_;
  double warn_soc_percent_;
  double critical_soc_percent_;
  int fd_{-1};

  rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr battery_state_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr battery_json_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace sensor_components

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<sensor_components::BatteryNode>());
  rclcpp::shutdown();
  return 0;
}
