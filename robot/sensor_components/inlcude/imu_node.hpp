#ifndef SENSOR_COMPONENTS_IMU_NODE_HPP_
#define SENSOR_COMPONENTS_IMU_NODE_HPP_

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/imu.hpp"

namespace sensor_components
{

class ImuNode : public rclcpp::Node
{
public:
  ImuNode();
  ~ImuNode() override;

private:
  static constexpr uint8_t kChipIdReg = 0x00;
  static constexpr uint8_t kOprModeReg = 0x3D;
  static constexpr uint8_t kPwrModeReg = 0x3E;
  static constexpr uint8_t kSysTriggerReg = 0x3F;
  static constexpr uint8_t kUnitSelReg = 0x3B;
  static constexpr uint8_t kQuatDataWReg = 0x20;
  static constexpr uint8_t kGyroDataXReg = 0x14;
  static constexpr uint8_t kLinearAccelDataXReg = 0x28;
  static constexpr uint8_t kBno055ChipId = 0xA0;
  static constexpr uint8_t kNormalPowerMode = 0x00;

  void timerCallback();
  bool initializeSensor();
  void closeSensor();
  bool readImu(sensor_msgs::msg::Imu & msg);
  bool operationModeFromString(const std::string & mode, uint8_t & value) const;
  bool writeRegister(uint8_t reg, uint8_t value);
  bool readRegister(uint8_t reg, uint8_t & value);
  bool readRegisters(uint8_t reg, uint8_t * data, std::size_t length);
  int16_t toInt16(uint8_t lsb, uint8_t msb) const;
  void fillCovariances(sensor_msgs::msg::Imu & msg) const;
  std::chrono::milliseconds retryInterval() const;

  int i2c_fd_;
  int i2c_bus_;
  int i2c_address_;
  std::string frame_id_;
  std::string operation_mode_;
  double publish_rate_;
  bool publish_orientation_;
  bool publish_angular_velocity_;
  bool publish_linear_acceleration_;
  bool sensor_ready_;
  rclcpp::Time last_init_attempt_;

  rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr imu_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace sensor_components

#endif  // SENSOR_COMPONENTS_IMU_NODE_HPP_
