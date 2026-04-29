#include "imu_node.hpp"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

namespace sensor_components
{
namespace
{
constexpr double kQuaternionScale = 1.0 / 16384.0;
constexpr double kAccelScale = 1.0 / 100.0;
constexpr double kGyroScale = M_PI / (180.0 * 16.0);
constexpr double kMinPublishRate = 1.0;
constexpr double kMaxPublishRate = 400.0;
}  // namespace

ImuNode::ImuNode()
: Node("imu_node"),
  i2c_fd_(-1),
  i2c_bus_(declare_parameter<int>("i2c_bus", 1)),
  i2c_address_(declare_parameter<int>("i2c_address", 0x28)),
  frame_id_(declare_parameter<std::string>("frame_id", "imu_link")),
  operation_mode_(declare_parameter<std::string>("operation_mode", "IMUPLUS")),
  publish_rate_(declare_parameter<double>("publish_rate", 50.0)),
  publish_orientation_(declare_parameter<bool>("publish_orientation", true)),
  publish_angular_velocity_(declare_parameter<bool>("publish_angular_velocity", true)),
  publish_linear_acceleration_(declare_parameter<bool>("publish_linear_acceleration", true)),
  sensor_ready_(false),
  last_init_attempt_(0, 0, get_clock()->get_clock_type())
{
  publish_rate_ = std::clamp(publish_rate_, kMinPublishRate, kMaxPublishRate);

  imu_pub_ = create_publisher<sensor_msgs::msg::Imu>("/imu/data", rclcpp::SensorDataQoS());

  const auto period =
    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(1.0 / publish_rate_));
  timer_ = create_wall_timer(period, std::bind(&ImuNode::timerCallback, this));

  sensor_ready_ = initializeSensor();
}

ImuNode::~ImuNode()
{
  closeSensor();
}

void ImuNode::timerCallback()
{
  if (!sensor_ready_) {
    const auto now = get_clock()->now();
    if ((now - last_init_attempt_).nanoseconds() >=
      std::chrono::duration_cast<std::chrono::nanoseconds>(retryInterval()).count())
    {
      sensor_ready_ = initializeSensor();
    }
    return;
  }

  sensor_msgs::msg::Imu msg;
  if (!readImu(msg)) {
    RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 5000, "Failed to read BNO055 IMU data");
    sensor_ready_ = false;
    closeSensor();
    return;
  }

  msg.header.stamp = get_clock()->now();
  msg.header.frame_id = frame_id_;
  fillCovariances(msg);
  imu_pub_->publish(msg);
}

bool ImuNode::initializeSensor()
{
  last_init_attempt_ = get_clock()->now();
  closeSensor();

  uint8_t operation_mode = 0x00;
  if (!operationModeFromString(operation_mode_, operation_mode)) {
    RCLCPP_ERROR(get_logger(), "Unsupported BNO055 operation_mode: %s", operation_mode_.c_str());
    return false;
  }

  const std::string device = "/dev/i2c-" + std::to_string(i2c_bus_);
  i2c_fd_ = ::open(device.c_str(), O_RDWR);
  if (i2c_fd_ < 0) {
    RCLCPP_ERROR_THROTTLE(
      get_logger(), *get_clock(), 5000, "Failed to open %s: %s", device.c_str(), std::strerror(errno));
    return false;
  }

  if (::ioctl(i2c_fd_, I2C_SLAVE, i2c_address_) < 0) {
    RCLCPP_ERROR_THROTTLE(
      get_logger(), *get_clock(), 5000, "Failed to select BNO055 I2C address 0x%02X: %s",
      i2c_address_, std::strerror(errno));
    closeSensor();
    return false;
  }

  uint8_t chip_id = 0;
  if (!readRegister(kChipIdReg, chip_id) || chip_id != kBno055ChipId) {
    RCLCPP_ERROR_THROTTLE(
      get_logger(), *get_clock(), 5000, "BNO055 not detected on %s at 0x%02X", device.c_str(), i2c_address_);
    closeSensor();
    return false;
  }

  if (!writeRegister(kOprModeReg, 0x00)) {
    closeSensor();
    return false;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(25));

  if (!writeRegister(kPwrModeReg, kNormalPowerMode) ||
    !writeRegister(kSysTriggerReg, 0x00) ||
    !writeRegister(kUnitSelReg, 0x00) ||
    !writeRegister(kOprModeReg, operation_mode))
  {
    RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 5000, "Failed to configure BNO055");
    closeSensor();
    return false;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  RCLCPP_INFO(
    get_logger(), "BNO055 initialized on %s at 0x%02X in %s mode",
    device.c_str(), i2c_address_, operation_mode_.c_str());
  return true;
}

void ImuNode::closeSensor()
{
  if (i2c_fd_ >= 0) {
    ::close(i2c_fd_);
    i2c_fd_ = -1;
  }
}

bool ImuNode::readImu(sensor_msgs::msg::Imu & msg)
{
  if (publish_orientation_) {
    std::array<uint8_t, 8> quat{};
    if (!readRegisters(kQuatDataWReg, quat.data(), quat.size())) {
      return false;
    }

    msg.orientation.w = static_cast<double>(toInt16(quat[0], quat[1])) * kQuaternionScale;
    msg.orientation.x = static_cast<double>(toInt16(quat[2], quat[3])) * kQuaternionScale;
    msg.orientation.y = static_cast<double>(toInt16(quat[4], quat[5])) * kQuaternionScale;
    msg.orientation.z = static_cast<double>(toInt16(quat[6], quat[7])) * kQuaternionScale;
  }

  if (publish_angular_velocity_) {
    std::array<uint8_t, 6> gyro{};
    if (!readRegisters(kGyroDataXReg, gyro.data(), gyro.size())) {
      return false;
    }

    msg.angular_velocity.x = static_cast<double>(toInt16(gyro[0], gyro[1])) * kGyroScale;
    msg.angular_velocity.y = static_cast<double>(toInt16(gyro[2], gyro[3])) * kGyroScale;
    msg.angular_velocity.z = static_cast<double>(toInt16(gyro[4], gyro[5])) * kGyroScale;
  }

  if (publish_linear_acceleration_) {
    std::array<uint8_t, 6> linear_accel{};
    if (!readRegisters(kLinearAccelDataXReg, linear_accel.data(), linear_accel.size())) {
      return false;
    }

    msg.linear_acceleration.x = static_cast<double>(toInt16(linear_accel[0], linear_accel[1])) * kAccelScale;
    msg.linear_acceleration.y = static_cast<double>(toInt16(linear_accel[2], linear_accel[3])) * kAccelScale;
    msg.linear_acceleration.z = static_cast<double>(toInt16(linear_accel[4], linear_accel[5])) * kAccelScale;
  }

  return true;
}

bool ImuNode::operationModeFromString(const std::string & mode, uint8_t & value) const
{
  static const std::unordered_map<std::string, uint8_t> modes = {
    {"CONFIG", 0x00},
    {"ACCONLY", 0x01},
    {"MAGONLY", 0x02},
    {"GYRONLY", 0x03},
    {"ACCMAG", 0x04},
    {"ACCGYRO", 0x05},
    {"MAGGYRO", 0x06},
    {"AMG", 0x07},
    {"IMUPLUS", 0x08},
    {"COMPASS", 0x09},
    {"M4G", 0x0A},
    {"NDOF_FMC_OFF", 0x0B},
    {"NDOF", 0x0C},
  };

  const auto it = modes.find(mode);
  if (it == modes.end()) {
    return false;
  }

  value = it->second;
  return true;
}

bool ImuNode::writeRegister(uint8_t reg, uint8_t value)
{
  const uint8_t data[2] = {reg, value};
  if (::write(i2c_fd_, data, sizeof(data)) != static_cast<ssize_t>(sizeof(data))) {
    RCLCPP_ERROR_THROTTLE(
      get_logger(), *get_clock(), 5000, "I2C write failed at register 0x%02X: %s", reg, std::strerror(errno));
    return false;
  }
  return true;
}

bool ImuNode::readRegister(uint8_t reg, uint8_t & value)
{
  return readRegisters(reg, &value, 1);
}

bool ImuNode::readRegisters(uint8_t reg, uint8_t * data, std::size_t length)
{
  if (::write(i2c_fd_, &reg, 1) != 1) {
    return false;
  }

  return ::read(i2c_fd_, data, length) == static_cast<ssize_t>(length);
}

int16_t ImuNode::toInt16(uint8_t lsb, uint8_t msb) const
{
  return static_cast<int16_t>((static_cast<uint16_t>(msb) << 8) | lsb);
}

void ImuNode::fillCovariances(sensor_msgs::msg::Imu & msg) const
{
  if (publish_orientation_) {
    msg.orientation_covariance = {
      0.01, 0.0, 0.0,
      0.0, 0.01, 0.0,
      0.0, 0.0, 0.01};
  } else {
    msg.orientation_covariance[0] = -1.0;
  }

  if (publish_angular_velocity_) {
    msg.angular_velocity_covariance = {
      0.001, 0.0, 0.0,
      0.0, 0.001, 0.0,
      0.0, 0.0, 0.001};
  } else {
    msg.angular_velocity_covariance[0] = -1.0;
  }

  if (publish_linear_acceleration_) {
    msg.linear_acceleration_covariance = {
      0.05, 0.0, 0.0,
      0.0, 0.05, 0.0,
      0.0, 0.0, 0.05};
  } else {
    msg.linear_acceleration_covariance[0] = -1.0;
  }
}

std::chrono::milliseconds ImuNode::retryInterval() const
{
  return std::chrono::milliseconds(2000);
}

}  // namespace sensor_components

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<sensor_components::ImuNode>());
  rclcpp::shutdown();
  return 0;
}
