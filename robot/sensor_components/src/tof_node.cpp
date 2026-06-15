#include <fcntl.h>
#include <gpiod.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/range.hpp"

namespace sensor_components
{
namespace
{
constexpr uint8_t kDefaultAddress = 0x29;
constexpr uint8_t kI2cAddressReg = 0x8A;
constexpr uint8_t kResultInterruptStatusReg = 0x13;
constexpr uint8_t kResultRangeStatusReg = 0x14;
constexpr uint8_t kSystemInterruptClearReg = 0x0B;
constexpr uint8_t kSysrangeStartReg = 0x00;
constexpr uint8_t kModelIdReg = 0xC0;
constexpr uint8_t kExpectedModelId = 0xEE;
constexpr double kMinPublishRate = 1.0;
constexpr double kMaxPublishRate = 100.0;
constexpr double kDefaultMaxRange = 1.2;
constexpr uint16_t kMaxValidRangeMm = 1200;
constexpr uint16_t kSentinelInvalidRangeMm = 8000;

enum class RangeReadStatus
{
  kOk,
  kNotReady,
  kI2cError,
  kTimeout,
  kSignalFail,
  kPhaseFail,
  kMinRangeFail,
  kOutOfRange,
  kSentinelRange,
  kHardwareFail,
  kUnknownFailure,
};

const char * statusToString(RangeReadStatus status)
{
  switch (status) {
    case RangeReadStatus::kOk:
      return "ok";
    case RangeReadStatus::kNotReady:
      return "not ready";
    case RangeReadStatus::kI2cError:
      return "i2c error";
    case RangeReadStatus::kTimeout:
      return "timeout";
    case RangeReadStatus::kSignalFail:
      return "signal fail";
    case RangeReadStatus::kPhaseFail:
      return "phase fail";
    case RangeReadStatus::kMinRangeFail:
      return "min range fail";
    case RangeReadStatus::kOutOfRange:
      return "out of range";
    case RangeReadStatus::kSentinelRange:
      return "sentinel range";
    case RangeReadStatus::kHardwareFail:
      return "hardware fail";
    case RangeReadStatus::kUnknownFailure:
      return "unknown range status";
  }
  return "unknown";
}

RangeReadStatus decodeRangeStatus(uint8_t range_status)
{
  const uint8_t status = (range_status & 0x78) >> 3;
  switch (status) {
    case 0:
      return RangeReadStatus::kOk;
    case 1:
      return RangeReadStatus::kSignalFail;
    case 2:
      return RangeReadStatus::kSignalFail;
    case 3:
      return RangeReadStatus::kMinRangeFail;
    case 4:
      return RangeReadStatus::kPhaseFail;
    case 5:
      return RangeReadStatus::kHardwareFail;
    default:
      return RangeReadStatus::kUnknownFailure;
  }
}

}  // namespace

class GpiodGpio
{
public:
  GpiodGpio(std::string chip_path, int line_offset)
  : chip_path_(std::move(chip_path)), line_offset_(line_offset), chip_(nullptr), line_(nullptr)
  {
  }

  ~GpiodGpio()
  {
    if (line_ != nullptr) {
      gpiod_line_release(line_);
    }
    if (chip_ != nullptr) {
      gpiod_chip_close(chip_);
    }
  }

  bool requestOutput()
  {
    chip_ = gpiod_chip_open(chip_path_.c_str());
    if (chip_ == nullptr) {
      return false;
    }

    line_ = gpiod_chip_get_line(chip_, line_offset_);
    if (line_ == nullptr) {
      return false;
    }

    return gpiod_line_request_output(line_, "tof_node", 0) == 0;
  }

  bool setValue(bool high)
  {
    return line_ != nullptr && gpiod_line_set_value(line_, high ? 1 : 0) == 0;
  }

private:
  std::string chip_path_;
  int line_offset_;
  gpiod_chip * chip_;
  gpiod_line * line_;
};

struct SensorConfig
{
  std::string name;
  int xshut_gpio;
  uint8_t i2c_address;
  std::string frame_id;
  std::string topic;
};

class VL53L0XDevice
{
public:
  VL53L0XDevice(SensorConfig config, int i2c_bus)
  : config_(std::move(config)), i2c_bus_(i2c_bus), i2c_fd_(-1), ready_(false)
  {
  }

  ~VL53L0XDevice()
  {
    closeBus();
  }

  const SensorConfig & config() const
  {
    return config_;
  }

  bool ready() const
  {
    return ready_;
  }

  void setReady(bool ready)
  {
    ready_ = ready;
  }

  bool openAt(uint8_t address)
  {
    closeBus();
    const std::string device = "/dev/i2c-" + std::to_string(i2c_bus_);
    i2c_fd_ = ::open(device.c_str(), O_RDWR);
    if (i2c_fd_ < 0) {
      return false;
    }
    return selectAddress(address);
  }

  bool initializeFromDefaultAddress()
  {
    if (!openAt(kDefaultAddress)) {
      return false;
    }

    uint8_t model_id = 0;
    if (!readRegister(kModelIdReg, model_id) || model_id != kExpectedModelId) {
      closeBus();
      return false;
    }

    if (!setAddress(config_.i2c_address) || !initializeRanging()) {
      closeBus();
      return false;
    }

    ready_ = true;
    return true;
  }

  RangeReadStatus readRangeMeters(double & range_m)
  {
    if (!ready_) {
      return RangeReadStatus::kNotReady;
    }

    uint8_t interrupt_status = 0;
    for (int i = 0; i < 10; ++i) {
      if (!readRegister(kResultInterruptStatusReg, interrupt_status)) {
        return RangeReadStatus::kI2cError;
      }
      if ((interrupt_status & 0x07) != 0) {
        uint8_t range_status = 0;
        uint16_t range_mm = 0;
        if (!readRegister(kResultRangeStatusReg, range_status) ||
          !readRegister16(kResultRangeStatusReg + 10, range_mm))
        {
          writeRegister(kSystemInterruptClearReg, 0x01);
          return RangeReadStatus::kI2cError;
        }
        writeRegister(kSystemInterruptClearReg, 0x01);
        const auto decoded_status = decodeRangeStatus(range_status);
        if (decoded_status != RangeReadStatus::kOk) {
          return decoded_status;
        }
        if (range_mm >= kSentinelInvalidRangeMm) {
          return RangeReadStatus::kSentinelRange;
        }
        if (range_mm > kMaxValidRangeMm) {
          return RangeReadStatus::kOutOfRange;
        }
        range_m = static_cast<double>(range_mm) / 1000.0;
        return RangeReadStatus::kOk;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    return RangeReadStatus::kTimeout;
  }

private:
  bool selectAddress(uint8_t address)
  {
    if (i2c_fd_ < 0) {
      return false;
    }
    return ::ioctl(i2c_fd_, I2C_SLAVE, address) >= 0;
  }

  bool setAddress(uint8_t new_address)
  {
    if (!writeRegister(kI2cAddressReg, new_address & 0x7F)) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return selectAddress(new_address);
  }

  bool initializeRanging()
  {
    const std::pair<uint8_t, uint8_t> init_sequence[] = {
      {0x88, 0x00}, {0x80, 0x01}, {0xFF, 0x01}, {0x00, 0x00},
      {0x91, 0x3C}, {0x00, 0x01}, {0xFF, 0x00}, {0x80, 0x00},
      {0x60, 0x00}, {0x01, 0xFF}, {0x00, 0x02},
    };

    for (const auto & entry : init_sequence) {
      if (!writeRegister(entry.first, entry.second)) {
        return false;
      }
    }

    return startContinuous();
  }

  bool startContinuous()
  {
    const std::pair<uint8_t, uint8_t> sequence[] = {
      {0x80, 0x01}, {0xFF, 0x01}, {0x00, 0x00}, {0x91, 0x3C},
      {0x00, 0x01}, {0xFF, 0x00}, {0x80, 0x00}, {kSysrangeStartReg, 0x02},
    };

    for (const auto & entry : sequence) {
      if (!writeRegister(entry.first, entry.second)) {
        return false;
      }
    }
    return true;
  }

  bool writeRegister(uint8_t reg, uint8_t value)
  {
    const uint8_t data[2] = {reg, value};
    return ::write(i2c_fd_, data, sizeof(data)) == static_cast<ssize_t>(sizeof(data));
  }

  bool readRegister(uint8_t reg, uint8_t & value)
  {
    if (::write(i2c_fd_, &reg, 1) != 1) {
      return false;
    }
    return ::read(i2c_fd_, &value, 1) == 1;
  }

  bool readRegister16(uint8_t reg, uint16_t & value)
  {
    uint8_t data[2] = {0, 0};
    if (::write(i2c_fd_, &reg, 1) != 1) {
      return false;
    }
    if (::read(i2c_fd_, data, sizeof(data)) != static_cast<ssize_t>(sizeof(data))) {
      return false;
    }
    value = static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
    return true;
  }

  void closeBus()
  {
    if (i2c_fd_ >= 0) {
      ::close(i2c_fd_);
      i2c_fd_ = -1;
    }
  }

  SensorConfig config_;
  int i2c_bus_;
  int i2c_fd_;
  bool ready_;
};

class TofNode : public rclcpp::Node
{
public:
  TofNode()
  : Node("tof_node"),
    gpio_chip_(declare_parameter<std::string>("gpio_chip", "/dev/gpiochip4")),
    i2c_bus_(declare_parameter<int>("i2c_bus", 1)),
    publish_rate_(declare_parameter<double>("publish_rate", 20.0)),
    min_range_(declare_parameter<double>("min_range", 0.03)),
    max_range_(declare_parameter<double>("max_range", kDefaultMaxRange)),
    field_of_view_(declare_parameter<double>("field_of_view", 0.436))
  {
    publish_rate_ = std::clamp(publish_rate_, kMinPublishRate, kMaxPublishRate);
    loadSensors();
    initializeSensors();

    const auto period =
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(1.0 / publish_rate_));
    timer_ = create_wall_timer(period, std::bind(&TofNode::timerCallback, this));
  }

private:
  void loadSensors()
  {
    const std::vector<std::string> default_sensors = {
      "front_right", "front_left", "rear_right", "rear_left"};
    const auto sensor_names = declare_parameter<std::vector<std::string>>("sensors", default_sensors);

    for (const auto & name : sensor_names) {
      SensorConfig config;
      config.name = name;
      config.xshut_gpio = declare_parameter<int>(name + ".xshut_gpio", defaultXshut(name));
      config.i2c_address = static_cast<uint8_t>(declare_parameter<int>(name + ".i2c_address", defaultAddress(name)));
      config.frame_id = declare_parameter<std::string>(name + ".frame_id", defaultFrameId(name));
      config.topic = declare_parameter<std::string>(name + ".topic", "/tof/" + name);

      auto publisher = create_publisher<sensor_msgs::msg::Range>(config.topic, rclcpp::SensorDataQoS());
      sensors_.push_back(std::make_unique<VL53L0XDevice>(config, i2c_bus_));
      publishers_.push_back(publisher);
    }
  }

  void initializeSensors()
  {
    gpios_.clear();
    gpios_.reserve(sensors_.size());

    bool gpio_ok = true;
    for (const auto & sensor : sensors_) {
      auto gpio = std::make_unique<GpiodGpio>(gpio_chip_, sensor->config().xshut_gpio);
      if (!gpio->requestOutput() || !gpio->setValue(false)) {
        RCLCPP_ERROR(
          get_logger(), "Failed to drive XSHUT GPIO %d on %s for %s",
          sensor->config().xshut_gpio, gpio_chip_.c_str(), sensor->config().name.c_str());
        gpio_ok = false;
      }
      gpios_.push_back(std::move(gpio));
    }

    if (!gpio_ok) {
      RCLCPP_ERROR(get_logger(), "ToF GPIO setup incomplete; node will keep running without exiting");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    for (std::size_t i = 0; i < sensors_.size(); ++i) {
      const auto & config = sensors_[i]->config();
      if (!gpios_[i]->setValue(true)) {
        RCLCPP_ERROR(get_logger(), "Failed to enable ToF sensor %s", config.name.c_str());
        sensors_[i]->setReady(false);
        continue;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      if (!sensors_[i]->initializeFromDefaultAddress()) {
        RCLCPP_ERROR(
          get_logger(), "Failed to initialize ToF sensor %s at reassigned address 0x%02X",
          config.name.c_str(), config.i2c_address);
        gpios_[i]->setValue(false);
        sensors_[i]->setReady(false);
        continue;
      }

      RCLCPP_INFO(
        get_logger(), "Initialized ToF sensor %s at 0x%02X on /dev/i2c-%d",
        config.name.c_str(), config.i2c_address, i2c_bus_);
    }
  }

  void timerCallback()
  {
    const auto stamp = get_clock()->now();

    for (std::size_t i = 0; i < sensors_.size(); ++i) {
      double range = 0.0;
      const auto status = sensors_[i]->readRangeMeters(range);
      auto msg = makeRangeMessage(stamp, sensors_[i]->config().frame_id);

      if (status != RangeReadStatus::kOk) {
        msg.range = invalidRangeForStatus(status);
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 5000, "Invalid ToF reading from %s: %s",
          sensors_[i]->config().name.c_str(), statusToString(status));
        publishers_[i]->publish(msg);
        continue;
      }

      if (range <= 0.0) {
        msg.range = -std::numeric_limits<float>::infinity();
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 5000, "Invalid ToF reading from %s: non-positive range %.3f m",
          sensors_[i]->config().name.c_str(), range);
        publishers_[i]->publish(msg);
        continue;
      }

      if (range < min_range_) {
        msg.range = -std::numeric_limits<float>::infinity();
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 5000, "Invalid ToF reading from %s: below min range %.3f m",
          sensors_[i]->config().name.c_str(), range);
        publishers_[i]->publish(msg);
        continue;
      }

      if (range > max_range_) {
        msg.range = std::numeric_limits<float>::infinity();
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 5000, "Invalid ToF reading from %s: above max range %.3f m",
          sensors_[i]->config().name.c_str(), range);
        publishers_[i]->publish(msg);
        continue;
      }

      msg.range = static_cast<float>(range);
      publishers_[i]->publish(msg);
    }
  }

  sensor_msgs::msg::Range makeRangeMessage(
    const rclcpp::Time & stamp,
    const std::string & frame_id) const
  {
    sensor_msgs::msg::Range msg;
    msg.header.stamp = stamp;
    msg.header.frame_id = frame_id;
    msg.radiation_type = sensor_msgs::msg::Range::INFRARED;
    msg.field_of_view = static_cast<float>(field_of_view_);
    msg.min_range = static_cast<float>(min_range_);
    msg.max_range = static_cast<float>(max_range_);
    return msg;
  }

  float invalidRangeForStatus(RangeReadStatus status) const
  {
    if (status == RangeReadStatus::kMinRangeFail) {
      return -std::numeric_limits<float>::infinity();
    }
    return std::numeric_limits<float>::infinity();
  }

  int defaultXshut(const std::string & name) const
  {
    if (name == "front_right") {
      return 17;
    }
    if (name == "front_left") {
      return 27;
    }
    if (name == "rear_right") {
      return 22;
    }
    if (name == "rear_left") {
      return 23;
    }
    return 0;
  }

  int defaultAddress(const std::string & name) const
  {
    if (name == "front_right") {
      return 0x30;
    }
    if (name == "front_left") {
      return 0x31;
    }
    if (name == "rear_right") {
      return 0x32;
    }
    if (name == "rear_left") {
      return 0x33;
    }
    return 0x30;
  }

  std::string defaultFrameId(const std::string & name) const
  {
    if (name == "front_right") {
      return "tof_front_right_link";
    }
    if (name == "front_left") {
      return "tof_front_left_link";
    }
    if (name == "rear_right") {
      return "tof_rear_right_link";
    }
    if (name == "rear_left") {
      return "tof_rear_left_link";
    }
    return "tof_" + name + "_link";
  }

  std::string gpio_chip_;
  int i2c_bus_;
  double publish_rate_;
  double min_range_;
  double max_range_;
  double field_of_view_;
  std::vector<std::unique_ptr<VL53L0XDevice>> sensors_;
  std::vector<std::unique_ptr<GpiodGpio>> gpios_;
  std::vector<rclcpp::Publisher<sensor_msgs::msg::Range>::SharedPtr> publishers_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace sensor_components

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<sensor_components::TofNode>());
  rclcpp::shutdown();
  return 0;
}
