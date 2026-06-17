#include <fcntl.h>
#include <gpiod.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
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
constexpr uint8_t kSystemSequenceConfigReg = 0x01;
constexpr uint8_t kSystemInterruptConfigGpioReg = 0x0A;
constexpr uint8_t kGpioHvMuxActiveHighReg = 0x84;
constexpr uint8_t kMsrcConfigControlReg = 0x60;
constexpr uint8_t kFinalRangeConfigMinCountRateRtnLimitReg = 0x44;
constexpr uint8_t kDynamicSpadNumRequestedRefSpadReg = 0x4E;
constexpr uint8_t kDynamicSpadRefEnStartOffsetReg = 0x4F;
constexpr uint8_t kGlobalConfigSpadEnablesRef0Reg = 0xB0;
constexpr uint8_t kGlobalConfigRefEnStartSelectReg = 0xB6;
constexpr uint8_t kModelIdReg = 0xC0;
constexpr uint8_t kExpectedModelId = 0xEE;
constexpr uint16_t kVl53l1xI2cAddressReg = 0x0001;
constexpr uint16_t kVl53l1xSoftResetReg = 0x0000;
constexpr uint16_t kVl53l1xVhvTimeoutReg = 0x0008;
constexpr uint16_t kVl53l1xUnknownInitReg = 0x000B;
constexpr uint16_t kVl53l1xInitBlockStartReg = 0x002D;
constexpr uint16_t kVl53l1xGpioHvMuxCtrlReg = 0x0030;
constexpr uint16_t kVl53l1xFirmwareSystemStatusReg = 0x00E5;
constexpr uint16_t kVl53l1xIdentificationModelIdReg = 0x010F;
constexpr uint16_t kVl53l1xSystemInterruptClearReg = 0x0086;
constexpr uint16_t kVl53l1xSystemModeStartReg = 0x0087;
constexpr uint16_t kVl53l1xGpioTioHvStatusReg = 0x0031;
constexpr uint16_t kVl53l1xRangeConfigTimeoutMacroAReg = 0x005E;
constexpr uint16_t kVl53l1xRangeConfigTimeoutMacroBReg = 0x0061;
constexpr uint16_t kVl53l1xResultRangeStatusReg = 0x0089;
constexpr uint16_t kVl53l1xResultFinalRangeMmReg = 0x0096;
constexpr uint16_t kVl53l1xInterMeasurementPeriodReg = 0x006C;
constexpr uint16_t kExpectedVl53l1xModelId = 0xEACC;
constexpr double kMinPublishRate = 1.0;
constexpr double kMaxPublishRate = 100.0;
constexpr double kDefaultMaxRange = 1.2;
constexpr uint16_t kSentinelInvalidRangeMm = 8000;
constexpr auto kAllXshutLowDelay = std::chrono::milliseconds(100);
constexpr auto kSensorPowerUpDelay = std::chrono::milliseconds(50);
constexpr auto kVl53l0xInitTimeout = std::chrono::milliseconds(500);

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

enum class SensorType
{
  kVl53l0x,
  kVl53l1x,
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

std::string errnoString()
{
  return std::strerror(errno);
}

RangeReadStatus decodeRangeStatus(uint8_t range_status)
{
  const uint8_t status = (range_status >> 3) & 0x0F;
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
    case 6:
      return RangeReadStatus::kPhaseFail;
    case 7:
      return RangeReadStatus::kHardwareFail;
    case 8:
      return RangeReadStatus::kMinRangeFail;
    case 9:
      return RangeReadStatus::kOutOfRange;
    case 10:
      return RangeReadStatus::kSignalFail;
    case 11:
      return RangeReadStatus::kOutOfRange;
    case 12:
      return RangeReadStatus::kMinRangeFail;
    default:
      return RangeReadStatus::kUnknownFailure;
  }
}

bool rangeStatusIsInvalid(RangeReadStatus status)
{
  switch (status) {
    case RangeReadStatus::kSignalFail:
    case RangeReadStatus::kPhaseFail:
    case RangeReadStatus::kMinRangeFail:
    case RangeReadStatus::kOutOfRange:
    case RangeReadStatus::kHardwareFail:
    case RangeReadStatus::kUnknownFailure:
      return true;
    case RangeReadStatus::kOk:
    case RangeReadStatus::kNotReady:
    case RangeReadStatus::kI2cError:
    case RangeReadStatus::kTimeout:
    case RangeReadStatus::kSentinelRange:
      return false;
  }
  return false;
}

const char * invalidReasonForStatus(RangeReadStatus status)
{
  switch (status) {
    case RangeReadStatus::kTimeout:
      return "read_timeout";
    case RangeReadStatus::kSignalFail:
    case RangeReadStatus::kPhaseFail:
    case RangeReadStatus::kMinRangeFail:
    case RangeReadStatus::kOutOfRange:
    case RangeReadStatus::kHardwareFail:
    case RangeReadStatus::kUnknownFailure:
      return "range_status_invalid";
    case RangeReadStatus::kSentinelRange:
      return "above_max";
    case RangeReadStatus::kNotReady:
      return "not_ready";
    case RangeReadStatus::kI2cError:
      return "i2c_error";
    case RangeReadStatus::kOk:
      return "ok";
  }
  return "unknown";
}

RangeReadStatus decodeVl53l1xRangeStatus(uint8_t range_status)
{
  switch (range_status) {
    case 0x09:
      return RangeReadStatus::kOk;
    case 0x01:
    case 0x02:
      return RangeReadStatus::kSignalFail;
    case 0x04:
      return RangeReadStatus::kOutOfRange;
    case 0x05:
      return RangeReadStatus::kHardwareFail;
    case 0x07:
      return RangeReadStatus::kPhaseFail;
    case 0x08:
      return RangeReadStatus::kMinRangeFail;
    default:
      return RangeReadStatus::kUnknownFailure;
  }
}

SensorType sensorTypeFromString(const std::string & type)
{
  if (type == "VL53L1X" || type == "vl53l1x") {
    return SensorType::kVl53l1x;
  }
  return SensorType::kVl53l0x;
}

const char * sensorTypeToString(SensorType type)
{
  switch (type) {
    case SensorType::kVl53l0x:
      return "VL53L0X";
    case SensorType::kVl53l1x:
      return "VL53L1X";
  }
  return "unknown";
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
  SensorType type;
  int xshut_gpio;
  uint8_t i2c_address;
  std::string frame_id;
  std::string topic;
  double max_range_m;
  int read_timeout_ms;
  int timing_budget_ms;
  int inter_measurement_ms;
};

class TofDevice
{
public:
  virtual ~TofDevice() = default;
  virtual const SensorConfig & config() const = 0;
  virtual bool ready() const = 0;
  virtual bool initializeFromDefaultAddress(std::string & failure_reason) = 0;
  virtual RangeReadStatus readRangeMeters(double & range_m) = 0;
  virtual RangeReadStatus pollRangeMeters(double & range_m)
  {
    return readRangeMeters(range_m);
  }
  virtual std::string debugState() const
  {
    return {};
  }
};

class VL53L0XDevice : public TofDevice
{
public:
  VL53L0XDevice(SensorConfig config, int i2c_bus)
  : config_(std::move(config)), i2c_bus_(i2c_bus), i2c_fd_(-1), ready_(false), stop_variable_(0),
    last_interrupt_status_(0), last_range_status_(0), last_sysrange_start_(0), last_range_mm_(0)
  {
  }

  ~VL53L0XDevice()
  {
    closeBus();
  }

  const SensorConfig & config() const override
  {
    return config_;
  }

  bool ready() const override
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
    std::string reason;
    return initializeFromDefaultAddress(reason);
  }

  bool initializeFromDefaultAddress(std::string & failure_reason) override
  {
    if (!openAt(kDefaultAddress)) {
      failure_reason = "failed to open default I2C address 0x29: " + errnoString();
      return false;
    }

    uint8_t model_id = 0;
    if (!readRegister(kModelIdReg, model_id)) {
      closeBus();
      failure_reason = "failed to read model ID at default address 0x29";
      return false;
    }
    if (model_id != kExpectedModelId) {
      closeBus();
      failure_reason = "unexpected model ID at default address 0x29";
      return false;
    }

    if (!initializeRanging()) {
      closeBus();
      failure_reason = "failed to initialize VL53L0X ranging";
      return false;
    }

    if (!setAddress(config_.i2c_address)) {
      closeBus();
      failure_reason = "failed to change address from 0x29 to target address";
      return false;
    }

    if (!readRegister(kModelIdReg, model_id)) {
      closeBus();
      failure_reason = "target address read test failed";
      return false;
    }
    if (model_id != kExpectedModelId) {
      closeBus();
      failure_reason = "target address read test returned unexpected model ID";
      return false;
    }

    ready_ = true;
    return true;
  }

  RangeReadStatus readRangeMeters(double & range_m) override
  {
    if (!ready_) {
      return RangeReadStatus::kNotReady;
    }

    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(config_.read_timeout_ms);
    const auto start_status = startSingleMeasurement(deadline);
    if (start_status != RangeReadStatus::kOk) {
      return start_status;
    }

    while (std::chrono::steady_clock::now() < deadline) {
      uint8_t interrupt_status = 0;
      if (!readRegister(kResultInterruptStatusReg, interrupt_status)) {
        return RangeReadStatus::kI2cError;
      }
      last_interrupt_status_ = interrupt_status;
      if ((interrupt_status & 0x07) != 0) {
        uint8_t range_status = 0;
        uint16_t range_mm = 0;
        if (!readRegister(kResultRangeStatusReg, range_status) ||
          !readRegister16(kResultRangeStatusReg + 10, range_mm))
        {
          writeRegister(kSystemInterruptClearReg, 0x01);
          return RangeReadStatus::kI2cError;
        }
        last_range_status_ = range_status;
        last_range_mm_ = range_mm;
        writeRegister(kSystemInterruptClearReg, 0x01);
        range_m = static_cast<double>(range_mm) / 1000.0;
        const auto decoded_status = decodeRangeStatus(range_status);
        if (decoded_status != RangeReadStatus::kOk) {
          return decoded_status;
        }
        if (range_mm >= kSentinelInvalidRangeMm) {
          return RangeReadStatus::kSentinelRange;
        }
        return RangeReadStatus::kOk;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return RangeReadStatus::kTimeout;
  }

  RangeReadStatus pollRangeMeters(double & range_m) override
  {
    if (!ready_) {
      return RangeReadStatus::kNotReady;
    }

    uint8_t interrupt_status = 0;
    if (!readRegister(kResultInterruptStatusReg, interrupt_status)) {
      return RangeReadStatus::kI2cError;
    }
    last_interrupt_status_ = interrupt_status;
    if ((interrupt_status & 0x07) == 0) {
      return RangeReadStatus::kTimeout;
    }

    uint8_t range_status = 0;
    uint16_t range_mm = 0;
    if (!readRegister(kResultRangeStatusReg, range_status) ||
      !readRegister16(kResultRangeStatusReg + 10, range_mm))
    {
      writeRegister(kSystemInterruptClearReg, 0x01);
      return RangeReadStatus::kI2cError;
    }
    last_range_status_ = range_status;
    last_range_mm_ = range_mm;
    writeRegister(kSystemInterruptClearReg, 0x01);
    range_m = static_cast<double>(range_mm) / 1000.0;

    const auto decoded_status = decodeRangeStatus(range_status);
    if (decoded_status != RangeReadStatus::kOk) {
      return decoded_status;
    }
    if (range_mm >= kSentinelInvalidRangeMm) {
      return RangeReadStatus::kSentinelRange;
    }
    return RangeReadStatus::kOk;
  }

  std::string debugState() const override
  {
    char buffer[144];
    std::snprintf(
      buffer, sizeof(buffer),
      "vl53l0x_regs=sysrange_start=0x%02X interrupt_status=0x%02X range_status_raw=0x%02X range_status_decoded=%u raw_range_mm=%u",
      last_sysrange_start_, last_interrupt_status_, last_range_status_,
      static_cast<unsigned>((last_range_status_ >> 3) & 0x0F), static_cast<unsigned>(last_range_mm_));
    return buffer;
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
    const std::pair<uint8_t, uint8_t> access_sequence[] = {
      {0x88, 0x00}, {0x80, 0x01}, {0xFF, 0x01}, {0x00, 0x00},
    };
    for (const auto & entry : access_sequence) {
      if (!writeRegister(entry.first, entry.second)) {
        return false;
      }
    }
    if (!readRegister(0x91, stop_variable_)) {
      return false;
    }
    const std::pair<uint8_t, uint8_t> close_access_sequence[] = {
      {0x00, 0x01}, {0xFF, 0x00}, {0x80, 0x00},
    };
    for (const auto & entry : close_access_sequence) {
      if (!writeRegister(entry.first, entry.second)) {
        return false;
      }
    }

    uint8_t config_control = 0;
    if (!readRegister(kMsrcConfigControlReg, config_control) ||
      !writeRegister(kMsrcConfigControlReg, config_control | 0x12) ||
      !writeRegister16(kFinalRangeConfigMinCountRateRtnLimitReg, 32) ||
      !writeRegister(kSystemSequenceConfigReg, 0xFF))
    {
      return false;
    }

    uint8_t spad_count = 0;
    bool spad_is_aperture = false;
    if (!getSpadInfo(spad_count, spad_is_aperture)) {
      return false;
    }

    uint8_t ref_spad_map[6] = {};
    if (!readRegisters(kGlobalConfigSpadEnablesRef0Reg, ref_spad_map, sizeof(ref_spad_map))) {
      return false;
    }

    const std::pair<uint8_t, uint8_t> spad_sequence[] = {
      {0xFF, 0x01}, {kDynamicSpadRefEnStartOffsetReg, 0x00},
      {kDynamicSpadNumRequestedRefSpadReg, 0x2C}, {0xFF, 0x00},
      {kGlobalConfigRefEnStartSelectReg, 0xB4},
    };
    for (const auto & entry : spad_sequence) {
      if (!writeRegister(entry.first, entry.second)) {
        return false;
      }
    }

    const uint8_t first_spad_to_enable = spad_is_aperture ? 12 : 0;
    uint8_t spads_enabled = 0;
    for (uint8_t i = 0; i < 48; ++i) {
      const uint8_t byte_index = i / 8;
      const uint8_t bit_mask = static_cast<uint8_t>(1U << (i % 8));
      if (i < first_spad_to_enable || spads_enabled == spad_count) {
        ref_spad_map[byte_index] &= static_cast<uint8_t>(~bit_mask);
      } else if ((ref_spad_map[byte_index] & bit_mask) != 0) {
        ++spads_enabled;
      }
    }
    if (!writeRegisters(kGlobalConfigSpadEnablesRef0Reg, ref_spad_map, sizeof(ref_spad_map))) {
      return false;
    }

    const std::pair<uint8_t, uint8_t> tuning_sequence[] = {
      {0xFF, 0x01}, {0x00, 0x00}, {0xFF, 0x00}, {0x09, 0x00}, {0x10, 0x00},
      {0x11, 0x00}, {0x24, 0x01}, {0x25, 0xFF}, {0x75, 0x00}, {0xFF, 0x01},
      {0x4E, 0x2C}, {0x48, 0x00}, {0x30, 0x20}, {0xFF, 0x00}, {0x30, 0x09},
      {0x54, 0x00}, {0x31, 0x04}, {0x32, 0x03}, {0x40, 0x83}, {0x46, 0x25},
      {0x60, 0x00}, {0x27, 0x00}, {0x50, 0x06}, {0x51, 0x00}, {0x52, 0x96},
      {0x56, 0x08}, {0x57, 0x30}, {0x61, 0x00}, {0x62, 0x00}, {0x64, 0x00},
      {0x65, 0x00}, {0x66, 0xA0}, {0xFF, 0x01}, {0x22, 0x32}, {0x47, 0x14},
      {0x49, 0xFF}, {0x4A, 0x00}, {0xFF, 0x00}, {0x7A, 0x0A}, {0x7B, 0x00},
      {0x78, 0x21}, {0xFF, 0x01}, {0x23, 0x34}, {0x42, 0x00}, {0x44, 0xFF},
      {0x45, 0x26}, {0x46, 0x05}, {0x40, 0x40}, {0x0E, 0x06}, {0x20, 0x1A},
      {0x43, 0x40}, {0xFF, 0x00}, {0x34, 0x03}, {0x35, 0x44}, {0xFF, 0x01},
      {0x31, 0x04}, {0x4B, 0x09}, {0x4C, 0x05}, {0x4D, 0x04}, {0xFF, 0x00},
      {0x44, 0x00}, {0x45, 0x20}, {0x47, 0x08}, {0x48, 0x28}, {0x67, 0x00},
      {0x70, 0x04}, {0x71, 0x01}, {0x72, 0xFE}, {0x76, 0x00}, {0x77, 0x00},
      {0xFF, 0x01}, {0x0D, 0x01}, {0xFF, 0x00}, {0x80, 0x01}, {0x01, 0xF8},
      {0xFF, 0x01}, {0x8E, 0x01}, {0x00, 0x01}, {0xFF, 0x00}, {0x80, 0x00},
    };

    for (const auto & entry : tuning_sequence) {
      if (!writeRegister(entry.first, entry.second)) {
        return false;
      }
    }

    uint8_t gpio_hv_mux = 0;
    if (!writeRegister(kSystemInterruptConfigGpioReg, 0x04) ||
      !readRegister(kGpioHvMuxActiveHighReg, gpio_hv_mux) ||
      !writeRegister(kGpioHvMuxActiveHighReg, gpio_hv_mux & static_cast<uint8_t>(~0x10)) ||
      !writeRegister(kSystemInterruptClearReg, 0x01) ||
      !writeRegister(kSystemSequenceConfigReg, 0xE8) ||
      !writeRegister(kSystemSequenceConfigReg, 0x01) ||
      !performSingleRefCalibration(0x40) ||
      !writeRegister(kSystemSequenceConfigReg, 0x02) ||
      !performSingleRefCalibration(0x00) ||
      !writeRegister(kSystemSequenceConfigReg, 0xE8))
    {
      return false;
    }
    return true;
  }

  RangeReadStatus startSingleMeasurement(std::chrono::steady_clock::time_point deadline)
  {
    const std::pair<uint8_t, uint8_t> sequence[] = {
      {0x80, 0x01}, {0xFF, 0x01}, {0x00, 0x00}, {0x91, stop_variable_},
      {0x00, 0x01}, {0xFF, 0x00}, {0x80, 0x00}, {kSysrangeStartReg, 0x01},
    };
    for (const auto & entry : sequence) {
      if (!writeRegister(entry.first, entry.second)) {
        return RangeReadStatus::kI2cError;
      }
    }

    do {
      if (!readRegister(kSysrangeStartReg, last_sysrange_start_)) {
        return RangeReadStatus::kI2cError;
      }
      if ((last_sysrange_start_ & 0x01) == 0) {
        return RangeReadStatus::kOk;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);

    return RangeReadStatus::kTimeout;
  }

  bool getSpadInfo(uint8_t & count, bool & is_aperture)
  {
    const std::pair<uint8_t, uint8_t> start_sequence[] = {
      {0x80, 0x01}, {0xFF, 0x01}, {0x00, 0x00}, {0xFF, 0x06},
    };
    for (const auto & entry : start_sequence) {
      if (!writeRegister(entry.first, entry.second)) {
        return false;
      }
    }

    uint8_t value = 0;
    if (!readRegister(0x83, value) || !writeRegister(0x83, value | 0x04)) {
      return false;
    }

    const std::pair<uint8_t, uint8_t> request_sequence[] = {
      {0xFF, 0x07}, {0x81, 0x01}, {0x80, 0x01}, {0x94, 0x6B}, {0x83, 0x00},
    };
    for (const auto & entry : request_sequence) {
      if (!writeRegister(entry.first, entry.second)) {
        return false;
      }
    }

    const auto deadline = std::chrono::steady_clock::now() + kVl53l0xInitTimeout;
    do {
      if (!readRegister(0x83, value)) {
        return false;
      }
      if (value != 0x00) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    if (value == 0x00) {
      return false;
    }

    uint8_t spad_info = 0;
    if (!writeRegister(0x83, 0x01) || !readRegister(0x92, spad_info)) {
      return false;
    }
    count = spad_info & 0x7F;
    is_aperture = ((spad_info >> 7) & 0x01) != 0;

    if (!writeRegister(0x81, 0x00) || !writeRegister(0xFF, 0x06) ||
      !readRegister(0x83, value) || !writeRegister(0x83, value & static_cast<uint8_t>(~0x04)))
    {
      return false;
    }

    const std::pair<uint8_t, uint8_t> restore_sequence[] = {
      {0xFF, 0x01}, {0x00, 0x01}, {0xFF, 0x00}, {0x80, 0x00},
    };
    for (const auto & entry : restore_sequence) {
      if (!writeRegister(entry.first, entry.second)) {
        return false;
      }
    }
    return true;
  }

  bool performSingleRefCalibration(uint8_t vhv_init_byte)
  {
    if (!writeRegister(kSysrangeStartReg, static_cast<uint8_t>(0x01 | vhv_init_byte))) {
      return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + kVl53l0xInitTimeout;
    do {
      if (!readRegister(kResultInterruptStatusReg, last_interrupt_status_)) {
        return false;
      }
      if ((last_interrupt_status_ & 0x07) != 0) {
        return writeRegister(kSystemInterruptClearReg, 0x01) &&
               writeRegister(kSysrangeStartReg, 0x00);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);

    return false;
  }

  bool writeRegister(uint8_t reg, uint8_t value)
  {
    const uint8_t data[2] = {reg, value};
    return ::write(i2c_fd_, data, sizeof(data)) == static_cast<ssize_t>(sizeof(data));
  }

  bool writeRegister16(uint8_t reg, uint16_t value)
  {
    const uint8_t data[3] = {
      reg, static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>(value & 0xFF)};
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

  bool readRegisters(uint8_t reg, uint8_t * values, std::size_t length)
  {
    if (::write(i2c_fd_, &reg, 1) != 1) {
      return false;
    }
    return ::read(i2c_fd_, values, length) == static_cast<ssize_t>(length);
  }

  bool writeRegisters(uint8_t reg, const uint8_t * values, std::size_t length)
  {
    std::vector<uint8_t> data(length + 1);
    data[0] = reg;
    std::copy(values, values + length, data.begin() + 1);
    return ::write(i2c_fd_, data.data(), data.size()) == static_cast<ssize_t>(data.size());
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
  uint8_t stop_variable_;
  uint8_t last_interrupt_status_;
  uint8_t last_range_status_;
  uint8_t last_sysrange_start_;
  uint16_t last_range_mm_;
};

class VL53L1XDevice : public TofDevice
{
public:
  VL53L1XDevice(SensorConfig config, int i2c_bus)
  : config_(std::move(config)), i2c_bus_(i2c_bus), i2c_fd_(-1), ready_(false),
    last_data_ready_(false), last_mux_ctrl_(0), last_gpio_status_(0), last_mode_start_(0),
    last_range_status_(0), last_range_mm_(0), last_inter_measurement_raw_(0)
  {
  }

  ~VL53L1XDevice()
  {
    closeBus();
  }

  const SensorConfig & config() const override
  {
    return config_;
  }

  bool ready() const override
  {
    return ready_;
  }

  bool initializeFromDefaultAddress(std::string & failure_reason) override
  {
    if (!openAt(kDefaultAddress)) {
      failure_reason = "failed to open default I2C address 0x29: " + errnoString();
      return false;
    }

    if (!softwareReset()) {
      closeBus();
      failure_reason = "software reset or boot wait failed";
      return false;
    }

    uint16_t model_id = 0;
    if (!readRegister16(kVl53l1xIdentificationModelIdReg, model_id)) {
      closeBus();
      failure_reason = "failed to read VL53L1X model ID at default address 0x29";
      return false;
    }
    if (model_id != kExpectedVl53l1xModelId) {
      closeBus();
      failure_reason = "unexpected VL53L1X model ID at default address 0x29";
      return false;
    }

    if (!setAddress(config_.i2c_address)) {
      closeBus();
      failure_reason = "failed to change address from 0x29 to target address";
      return false;
    }

    if (!readRegister16(kVl53l1xIdentificationModelIdReg, model_id)) {
      closeBus();
      failure_reason = "target address read test failed";
      return false;
    }
    if (model_id != kExpectedVl53l1xModelId) {
      closeBus();
      failure_reason = "target address read test returned unexpected VL53L1X model ID";
      return false;
    }

    if (!initializeRanging()) {
      closeBus();
      failure_reason = "failed to start ranging";
      return false;
    }

    ready_ = true;
    return true;
  }

  RangeReadStatus readRangeMeters(double & range_m) override
  {
    if (!ready_) {
      return RangeReadStatus::kNotReady;
    }

    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(config_.read_timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      bool ready = false;
      if (!dataReady(ready)) {
        return RangeReadStatus::kI2cError;
      }
      if (ready) {
        uint8_t range_status = 0;
        uint16_t range_mm = 0;
        if (!readRegister(kVl53l1xResultRangeStatusReg, range_status) ||
          !readRegister16(kVl53l1xResultFinalRangeMmReg, range_mm))
        {
          clearInterrupt();
          return RangeReadStatus::kI2cError;
        }
        last_range_status_ = range_status;
        last_range_mm_ = range_mm;
        clearInterrupt();
        range_m = static_cast<double>(range_mm) / 1000.0;
        const auto decoded_status = decodeVl53l1xRangeStatus(range_status);
        if (decoded_status != RangeReadStatus::kOk) {
          return decoded_status;
        }
        if (range_mm >= kSentinelInvalidRangeMm) {
          return RangeReadStatus::kSentinelRange;
        }
        return RangeReadStatus::kOk;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return RangeReadStatus::kTimeout;
  }

  RangeReadStatus pollRangeMeters(double & range_m) override
  {
    if (!ready_) {
      return RangeReadStatus::kNotReady;
    }

    bool ready = false;
    if (!dataReady(ready)) {
      return RangeReadStatus::kI2cError;
    }
    if (!ready) {
      return RangeReadStatus::kTimeout;
    }

    uint8_t range_status = 0;
    uint16_t range_mm = 0;
    if (!readRegister(kVl53l1xResultRangeStatusReg, range_status) ||
      !readRegister16(kVl53l1xResultFinalRangeMmReg, range_mm))
    {
      clearInterrupt();
      return RangeReadStatus::kI2cError;
    }
    last_range_status_ = range_status;
    last_range_mm_ = range_mm;
    clearInterrupt();
    range_m = static_cast<double>(range_mm) / 1000.0;

    const auto decoded_status = decodeVl53l1xRangeStatus(range_status);
    if (decoded_status != RangeReadStatus::kOk) {
      return decoded_status;
    }
    if (range_mm >= kSentinelInvalidRangeMm) {
      return RangeReadStatus::kSentinelRange;
    }
    return RangeReadStatus::kOk;
  }

  std::string debugState() const override
  {
    char buffer[192];
    std::snprintf(
      buffer, sizeof(buffer),
      "vl53l1x_regs=data_ready=%s mux_ctrl=0x%02X gpio_status=0x%02X mode_start=0x%02X range_status_raw=0x%02X raw_range_mm=%u timing_budget_ms=%d inter_measurement_raw=0x%08X",
      last_data_ready_ ? "true" : "false", last_mux_ctrl_, last_gpio_status_, last_mode_start_,
      last_range_status_, static_cast<unsigned>(last_range_mm_), config_.timing_budget_ms,
      static_cast<unsigned>(last_inter_measurement_raw_));
    return buffer;
  }

private:
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

  bool selectAddress(uint8_t address)
  {
    if (i2c_fd_ < 0) {
      return false;
    }
    return ::ioctl(i2c_fd_, I2C_SLAVE, address) >= 0;
  }

  bool softwareReset()
  {
    if (!writeRegister(kVl53l1xSoftResetReg, 0x00)) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!writeRegister(kVl53l1xSoftResetReg, 0x01)) {
      return false;
    }

    for (int i = 0; i < 100; ++i) {
      uint8_t boot_status = 0;
      if (readRegister(kVl53l1xFirmwareSystemStatusReg, boot_status) && (boot_status & 0x01) != 0) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
  }

  bool setAddress(uint8_t new_address)
  {
    if (!writeRegister(kVl53l1xI2cAddressReg, new_address & 0x7F)) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return selectAddress(new_address);
  }

  bool initializeRanging()
  {
    const std::vector<uint8_t> init_sequence = {
      0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x02, 0x08,
      0x00, 0x08, 0x10, 0x01, 0x01, 0x00, 0x00, 0x00,
      0x00, 0xFF, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x20, 0x0B, 0x00, 0x00, 0x02, 0x0A, 0x21,
      0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0xC8,
      0x00, 0x00, 0x38, 0xFF, 0x01, 0x00, 0x08, 0x00,
      0x00, 0x01, 0xCC, 0x0F, 0x01, 0xF1, 0x0D, 0x01,
      0x68, 0x00, 0x80, 0x08, 0xB8, 0x00, 0x00, 0x00,
      0x00, 0x0F, 0x89, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x01, 0x0F, 0x0D, 0x0E, 0x0E, 0x00,
      0x00, 0x02, 0xC7, 0xFF, 0x9B, 0x00, 0x00, 0x00,
      0x01, 0x00, 0x00,
    };

    if (!writeRegisters(kVl53l1xInitBlockStartReg, init_sequence)) {
      return false;
    }
    if (!startRanging() || !waitDataReady(std::chrono::milliseconds(500))) {
      return false;
    }
    if (!clearInterrupt() || !stopRanging()) {
      return false;
    }
    if (!writeRegister(kVl53l1xVhvTimeoutReg, 0x09) ||
      !writeRegister(kVl53l1xUnknownInitReg, 0x00))
    {
      return false;
    }
    if (!readRegister32(kVl53l1xInterMeasurementPeriodReg, last_inter_measurement_raw_)) {
      return false;
    }
    return configureTimingBudget() &&
           startRanging();
  }

  bool configureTimingBudget()
  {
    uint16_t timeout_a = 0x00AD;
    uint16_t timeout_b = 0x00C6;
    switch (config_.timing_budget_ms) {
      case 20:
        timeout_a = 0x001E;
        timeout_b = 0x0022;
        break;
      case 33:
        timeout_a = 0x0060;
        timeout_b = 0x006E;
        break;
      case 50:
        timeout_a = 0x00AD;
        timeout_b = 0x00C6;
        break;
      case 100:
        timeout_a = 0x01CC;
        timeout_b = 0x01EA;
        break;
      case 200:
        timeout_a = 0x02D9;
        timeout_b = 0x02F8;
        break;
      case 500:
        timeout_a = 0x048F;
        timeout_b = 0x04A4;
        break;
      default:
        return false;
    }
    return writeRegister16(kVl53l1xRangeConfigTimeoutMacroAReg, timeout_a) &&
           writeRegister16(kVl53l1xRangeConfigTimeoutMacroBReg, timeout_b);
  }

  bool startRanging()
  {
    if (!writeRegister(kVl53l1xSystemModeStartReg, 0x40)) {
      return false;
    }
    return readRegister(kVl53l1xSystemModeStartReg, last_mode_start_);
  }

  bool stopRanging()
  {
    if (!writeRegister(kVl53l1xSystemModeStartReg, 0x00)) {
      return false;
    }
    return readRegister(kVl53l1xSystemModeStartReg, last_mode_start_);
  }

  bool clearInterrupt()
  {
    return writeRegister(kVl53l1xSystemInterruptClearReg, 0x01);
  }

  bool waitDataReady(std::chrono::milliseconds timeout)
  {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      bool ready = false;
      if (!dataReady(ready)) {
        return false;
      }
      if (ready) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
  }

  bool dataReady(bool & ready)
  {
    uint8_t mux_ctrl = 0;
    uint8_t gpio_status = 0;
    if (!readRegister(kVl53l1xGpioHvMuxCtrlReg, mux_ctrl) ||
      !readRegister(kVl53l1xGpioTioHvStatusReg, gpio_status))
    {
      return false;
    }
    last_mux_ctrl_ = mux_ctrl;
    last_gpio_status_ = gpio_status;
    const uint8_t interrupt_polarity = (mux_ctrl & 0x10) != 0 ? 0 : 1;
    ready = (gpio_status & 0x01) == interrupt_polarity;
    last_data_ready_ = ready;
    return true;
  }

  bool writeRegister(uint16_t reg, uint8_t value)
  {
    const uint8_t data[3] = {
      static_cast<uint8_t>(reg >> 8),
      static_cast<uint8_t>(reg & 0xFF),
      value,
    };
    return ::write(i2c_fd_, data, sizeof(data)) == static_cast<ssize_t>(sizeof(data));
  }

  bool writeRegister16(uint16_t reg, uint16_t value)
  {
    const uint8_t data[4] = {
      static_cast<uint8_t>(reg >> 8),
      static_cast<uint8_t>(reg & 0xFF),
      static_cast<uint8_t>(value >> 8),
      static_cast<uint8_t>(value & 0xFF),
    };
    return ::write(i2c_fd_, data, sizeof(data)) == static_cast<ssize_t>(sizeof(data));
  }

  bool writeRegisters(uint16_t reg, const std::vector<uint8_t> & values)
  {
    std::vector<uint8_t> data;
    data.reserve(values.size() + 2);
    data.push_back(static_cast<uint8_t>(reg >> 8));
    data.push_back(static_cast<uint8_t>(reg & 0xFF));
    data.insert(data.end(), values.begin(), values.end());
    return ::write(i2c_fd_, data.data(), data.size()) == static_cast<ssize_t>(data.size());
  }

  bool readRegister(uint16_t reg, uint8_t & value)
  {
    const uint8_t reg_bytes[2] = {
      static_cast<uint8_t>(reg >> 8),
      static_cast<uint8_t>(reg & 0xFF),
    };
    if (::write(i2c_fd_, reg_bytes, sizeof(reg_bytes)) != static_cast<ssize_t>(sizeof(reg_bytes))) {
      return false;
    }
    return ::read(i2c_fd_, &value, 1) == 1;
  }

  bool readRegister16(uint16_t reg, uint16_t & value)
  {
    uint8_t data[2] = {0, 0};
    const uint8_t reg_bytes[2] = {
      static_cast<uint8_t>(reg >> 8),
      static_cast<uint8_t>(reg & 0xFF),
    };
    if (::write(i2c_fd_, reg_bytes, sizeof(reg_bytes)) != static_cast<ssize_t>(sizeof(reg_bytes))) {
      return false;
    }
    if (::read(i2c_fd_, data, sizeof(data)) != static_cast<ssize_t>(sizeof(data))) {
      return false;
    }
    value = static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
    return true;
  }

  bool readRegister32(uint16_t reg, uint32_t & value)
  {
    uint8_t data[4] = {0, 0, 0, 0};
    const uint8_t reg_bytes[2] = {
      static_cast<uint8_t>(reg >> 8),
      static_cast<uint8_t>(reg & 0xFF),
    };
    if (::write(i2c_fd_, reg_bytes, sizeof(reg_bytes)) != static_cast<ssize_t>(sizeof(reg_bytes))) {
      return false;
    }
    if (::read(i2c_fd_, data, sizeof(data)) != static_cast<ssize_t>(sizeof(data))) {
      return false;
    }
    value = (static_cast<uint32_t>(data[0]) << 24) |
            (static_cast<uint32_t>(data[1]) << 16) |
            (static_cast<uint32_t>(data[2]) << 8) |
            static_cast<uint32_t>(data[3]);
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
  bool last_data_ready_;
  uint8_t last_mux_ctrl_;
  uint8_t last_gpio_status_;
  uint8_t last_mode_start_;
  uint8_t last_range_status_;
  uint16_t last_range_mm_;
  uint32_t last_inter_measurement_raw_;
};

class TofNode : public rclcpp::Node
{
public:
  TofNode()
  : Node("tof_node"),
    gpio_chip_(declare_parameter<std::string>("gpio_chip", "/dev/gpiochip4")),
    i2c_bus_(declare_parameter<int>("i2c_bus", 1)),
    publish_rate_(declare_parameter<double>(
      "publish_rate_hz", declare_parameter<double>("publish_rate", 20.0))),
    timeout_sec_(declare_parameter<double>("timeout_sec", 0.5)),
    read_timeout_ms_(declare_parameter<int>("read_timeout_ms", 500)),
    accept_invalid_status_range_(declare_parameter<bool>("accept_invalid_status_range", false)),
    min_range_(declare_parameter<double>("min_range", 0.03)),
    max_range_(declare_parameter<double>("max_range", kDefaultMaxRange)),
    field_of_view_(declare_parameter<double>("field_of_view", 0.436))
  {
    publish_rate_ = std::clamp(publish_rate_, kMinPublishRate, kMaxPublishRate);
    publish_period_ms_ = 1000.0 / publish_rate_;
    timeout_sec_ = std::max(0.001, timeout_sec_);
    read_timeout_ms_ = std::max(1, read_timeout_ms_);
    loadSensors();
    initializeSensors();

    const auto period =
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(1.0 / publish_rate_));
    timer_ = create_wall_timer(period, std::bind(&TofNode::timerCallback, this));
  }

  ~TofNode() override
  {
    stopSensorWorkers();
  }

private:
  struct SensorRuntime
  {
    SensorConfig config;
    std::unique_ptr<TofDevice> device;
    rclcpp::Publisher<sensor_msgs::msg::Range>::SharedPtr publisher;
    std::mutex mutex;
    std::thread worker;
    bool worker_running = false;
    bool available = false;
    bool has_read = false;
    RangeReadStatus status = RangeReadStatus::kNotReady;
    RangeReadStatus last_valid_status = RangeReadStatus::kNotReady;
    double range_m = std::numeric_limits<double>::quiet_NaN();
    double last_valid_range_m = std::numeric_limits<double>::quiet_NaN();
    double read_elapsed_ms = 0.0;
    rclcpp::Time last_read_time;
    rclcpp::Time last_ok_time;
  };

  void loadSensors()
  {
    const std::vector<std::string> default_sensors = {
      "front_right", "front_left", "rear_right", "rear_left", "rear_center"};
    const auto sensor_names = declare_parameter<std::vector<std::string>>("sensors", default_sensors);

    for (const auto & name : orderedSensorNames(sensor_names)) {
      SensorConfig config;
      config.name = name;
      config.type = sensorTypeFromString(declare_parameter<std::string>(name + ".type", defaultType(name)));
      config.xshut_gpio = declare_parameter<int>(name + ".xshut_gpio", defaultXshut(name));
      config.i2c_address = static_cast<uint8_t>(declare_parameter<int>(name + ".i2c_address", defaultAddress(name)));
      config.frame_id = declare_parameter<std::string>(name + ".frame_id", defaultFrameId(name));
      config.topic = declare_parameter<std::string>(name + ".topic", "/tof/" + name);
      config.max_range_m = declare_parameter<double>(name + ".max_range_m", defaultMaxRange(name));
      config.read_timeout_ms = std::max(
        1, static_cast<int>(declare_parameter<int>(name + ".read_timeout_ms", read_timeout_ms_)));
      config.timing_budget_ms = std::max(
        1, static_cast<int>(declare_parameter<int>(name + ".timing_budget_ms", defaultTimingBudgetMs(name))));
      config.inter_measurement_ms = std::max(
        1, static_cast<int>(
          declare_parameter<int>(name + ".inter_measurement_ms", defaultInterMeasurementMs(name))));
      if (static_cast<double>(config.read_timeout_ms) > publish_period_ms_) {
        RCLCPP_WARN(
          get_logger(),
          "%s(0x%02X) read_timeout_ms=%d is longer than publish period %.1f ms",
          config.name.c_str(), config.i2c_address, config.read_timeout_ms, publish_period_ms_);
      }
      if (config.type == SensorType::kVl53l1x && config.read_timeout_ms <= config.timing_budget_ms) {
        RCLCPP_WARN(
          get_logger(),
          "%s(0x%02X) VL53L1X read_timeout_ms=%d is not greater than timing_budget_ms=%d; data_ready may be missed",
          config.name.c_str(), config.i2c_address, config.read_timeout_ms, config.timing_budget_ms);
      }
      if (config.type == SensorType::kVl53l1x &&
        publish_period_ms_ < static_cast<double>(config.inter_measurement_ms))
      {
        RCLCPP_WARN(
          get_logger(),
          "%s(0x%02X) VL53L1X publish period %.1f ms is shorter than inter_measurement_ms=%d; some cycles may skip data_ready",
          config.name.c_str(), config.i2c_address, publish_period_ms_, config.inter_measurement_ms);
      }

      sensor_configs_.push_back(config);
    }
  }

  void initializeSensors()
  {
    sensor_runtimes_.clear();
    gpios_.clear();
    gpios_.reserve(sensor_configs_.size());
    sensor_runtimes_.reserve(sensor_configs_.size());

    for (const auto & config : sensor_configs_) {
      auto gpio = std::make_unique<GpiodGpio>(gpio_chip_, config.xshut_gpio);
      if (!gpio->requestOutput() || !gpio->setValue(false)) {
        RCLCPP_ERROR(
          get_logger(),
          "Failed ToF GPIO setup: sensor=%s type=%s xshut_gpio=%d target_address=0x%02X reason=%s",
          config.name.c_str(), sensorTypeToString(config.type), config.xshut_gpio, config.i2c_address,
          "failed to request GPIO output LOW");
      }
      gpios_.push_back(std::move(gpio));

      auto runtime = std::make_shared<SensorRuntime>();
      runtime->config = config;
      runtime->publisher = create_publisher<sensor_msgs::msg::Range>(config.topic, rclcpp::SensorDataQoS());
      runtime->last_read_time = rclcpp::Time(0, 0, get_clock()->get_clock_type());
      runtime->last_ok_time = rclcpp::Time(0, 0, get_clock()->get_clock_type());
      sensor_runtimes_.push_back(runtime);
    }

    RCLCPP_INFO(
      get_logger(), "Requested all ToF XSHUT GPIO lines LOW on %s; waiting %lld ms",
      gpio_chip_.c_str(), static_cast<long long>(kAllXshutLowDelay.count()));

    std::this_thread::sleep_for(kAllXshutLowDelay);

    for (std::size_t i = 0; i < sensor_configs_.size(); ++i) {
      const auto & config = sensor_configs_[i];
      if (!gpios_[i]->setValue(true)) {
        RCLCPP_ERROR(
          get_logger(), "Failed ToF init: sensor=%s type=%s xshut_gpio=%d target_address=0x%02X reason=%s",
          config.name.c_str(), sensorTypeToString(config.type), config.xshut_gpio, config.i2c_address,
          "failed to drive XSHUT HIGH");
        continue;
      }

      RCLCPP_INFO(
        get_logger(), "Enabled ToF sensor: sensor=%s type=%s xshut_gpio=%d target_address=0x%02X",
        config.name.c_str(), sensorTypeToString(config.type), config.xshut_gpio, config.i2c_address);
      std::this_thread::sleep_for(kSensorPowerUpDelay);

      auto sensor = makeDevice(config);
      std::string failure_reason;
      if (!sensor->initializeFromDefaultAddress(failure_reason)) {
        RCLCPP_ERROR(
          get_logger(), "Failed ToF init: sensor=%s type=%s xshut_gpio=%d target_address=0x%02X reason=%s",
          config.name.c_str(), sensorTypeToString(config.type), config.xshut_gpio, config.i2c_address,
          failure_reason.c_str());
        gpios_[i]->setValue(false);
        continue;
      }

      RCLCPP_INFO(
        get_logger(),
        "Initialized ToF sensor: %s(0x%02X) type=%s xshut_gpio=%d i2c_bus=/dev/i2c-%d read_timeout_ms=%d timing_budget_ms=%d inter_measurement_ms=%d",
        config.name.c_str(), config.i2c_address, sensorTypeToString(config.type), config.xshut_gpio, i2c_bus_,
        config.read_timeout_ms, config.timing_budget_ms, config.inter_measurement_ms);
      sensor_runtimes_[i]->device = std::move(sensor);
      sensor_runtimes_[i]->available = true;
      startSensorWorker(sensor_runtimes_[i]);
    }

    const auto ready_count = std::count_if(
      sensor_runtimes_.begin(), sensor_runtimes_.end(),
      [](const std::shared_ptr<SensorRuntime> & runtime) {
        return runtime->device && runtime->device->ready();
      });
    if (ready_count == 0) {
      RCLCPP_ERROR(get_logger(), "No ToF sensors initialized successfully; node will keep running");
    }
  }

  void timerCallback()
  {
    const auto stamp = get_clock()->now();

    for (auto & runtime : sensor_runtimes_) {
      auto msg = makeRangeMessage(stamp, runtime->config);

      SensorConfig config;
      bool available = false;
      bool has_read = false;
      RangeReadStatus status = RangeReadStatus::kNotReady;
      double range = std::numeric_limits<double>::quiet_NaN();
      double last_valid_range = std::numeric_limits<double>::quiet_NaN();
      double read_elapsed_ms = 0.0;
      RangeReadStatus last_valid_status = RangeReadStatus::kNotReady;
      rclcpp::Time last_ok_time(0, 0, get_clock()->get_clock_type());

      {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        config = runtime->config;
        available = runtime->available && runtime->device && runtime->device->ready();
        has_read = runtime->has_read;
        status = runtime->status;
        range = runtime->range_m;
        last_valid_status = runtime->last_valid_status;
        last_valid_range = runtime->last_valid_range_m;
        read_elapsed_ms = runtime->read_elapsed_ms;
        last_ok_time = runtime->last_ok_time;
      }

      if (!available) {
        msg.range = std::numeric_limits<float>::quiet_NaN();
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 5000, "Invalid ToF reading from %s(0x%02X): sensor unavailable",
          config.name.c_str(), config.i2c_address);
        runtime->publisher->publish(msg);
        continue;
      }

      const bool accepted_as_valid =
        accept_invalid_status_range_ && rangeStatusIsInvalid(status) &&
        range >= min_range_ && range <= config.max_range_m;
      if (accepted_as_valid) {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        runtime->last_ok_time = stamp;
        runtime->last_valid_status = status;
        runtime->last_valid_range_m = range;
        last_ok_time = stamp;
        last_valid_status = status;
        last_valid_range = range;
      }

      const bool has_valid_sample = last_ok_time.nanoseconds() > 0;
      if (!has_read) {
        msg.range = std::numeric_limits<float>::quiet_NaN();
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 5000,
          "Invalid ToF reading from %s(0x%02X): no_read_yet",
          config.name.c_str(), config.i2c_address);
        runtime->publisher->publish(msg);
        continue;
      }

      if (has_valid_sample && (stamp - last_ok_time).seconds() > timeout_sec_) {
        msg.range = std::numeric_limits<float>::quiet_NaN();
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 5000,
          "Invalid ToF reading from %s(0x%02X): stale_timeout last_valid_age=%.3f sec last_valid_status=%s last_valid_range=%.3f m current_status=%s",
          config.name.c_str(), config.i2c_address, (stamp - last_ok_time).seconds(),
          statusToString(last_valid_status), last_valid_range, statusToString(status));
        runtime->publisher->publish(msg);
        continue;
      }

      if (status != RangeReadStatus::kOk) {
        if (std::isfinite(range) && range < min_range_) {
          msg.range = -std::numeric_limits<float>::infinity();
          RCLCPP_WARN_THROTTLE(
            get_logger(), *get_clock(), 5000,
            "Invalid ToF reading from %s(0x%02X): below_min status=%s range=%.3f m",
            config.name.c_str(), config.i2c_address, statusToString(status), range);
          runtime->publisher->publish(msg);
          continue;
        }

        if (std::isfinite(range) && range > config.max_range_m) {
          msg.range = std::numeric_limits<float>::infinity();
          RCLCPP_WARN_THROTTLE(
            get_logger(), *get_clock(), 5000,
            "Invalid ToF reading from %s(0x%02X): above_max status=%s range=%.3f m",
            config.name.c_str(), config.i2c_address, statusToString(status), range);
          runtime->publisher->publish(msg);
          continue;
        }

        if (accepted_as_valid) {
          RCLCPP_WARN_THROTTLE(
            get_logger(), *get_clock(), 5000,
            "ToF reading from %s(0x%02X) accepted_as_valid despite range_status_invalid: status=%s range=%.3f m",
            config.name.c_str(), config.i2c_address, statusToString(status), range);
          msg.range = static_cast<float>(range);
          runtime->publisher->publish(msg);
          continue;
        }

        msg.range = invalidRangeForStatus(status);
        const char * reason = invalidReasonForStatus(status);
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 5000, "Invalid ToF reading from %s(0x%02X): %s status=%s range=%.3f m",
          config.name.c_str(), config.i2c_address, reason, statusToString(status), range);
        runtime->publisher->publish(msg);
        continue;
      }

      if (range <= 0.0) {
        msg.range = -std::numeric_limits<float>::infinity();
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 5000, "Invalid ToF reading from %s(0x%02X): non-positive range %.3f m",
          config.name.c_str(), config.i2c_address, range);
        runtime->publisher->publish(msg);
        continue;
      }

      if (range < min_range_) {
        msg.range = -std::numeric_limits<float>::infinity();
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 5000, "Invalid ToF reading from %s(0x%02X): below min range %.3f m",
          config.name.c_str(), config.i2c_address, range);
        runtime->publisher->publish(msg);
        continue;
      }

      if (range > config.max_range_m) {
        msg.range = std::numeric_limits<float>::infinity();
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 5000, "Invalid ToF reading from %s(0x%02X): above max range %.3f m",
          config.name.c_str(), config.i2c_address, range);
        runtime->publisher->publish(msg);
        continue;
      }

      if (read_elapsed_ms > publish_period_ms_) {
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 5000,
          "ToF read from %s(0x%02X) took %.1f ms, longer than publish period %.1f ms",
          config.name.c_str(), config.i2c_address, read_elapsed_ms, publish_period_ms_);
      }

      msg.range = static_cast<float>(range);
      runtime->publisher->publish(msg);
    }
  }

  void startSensorWorker(const std::shared_ptr<SensorRuntime> & runtime)
  {
    runtime->worker_running = true;
    runtime->worker = std::thread([this, runtime]() {
      const auto read_period = std::chrono::duration<double>(1.0 / publish_rate_);
      while (!stop_workers_.load()) {
        const auto cycle_start = std::chrono::steady_clock::now();
        double range = std::numeric_limits<double>::quiet_NaN();
        RangeReadStatus status = RangeReadStatus::kNotReady;
        if (runtime->device && runtime->device->ready()) {
          status = runtime->device->readRangeMeters(range);
        }
        const std::string debug_state = runtime->device ? runtime->device->debugState() : std::string();
        const auto elapsed_ms = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - cycle_start).count();
        const auto now = get_clock()->now();

        {
          std::lock_guard<std::mutex> lock(runtime->mutex);
          const bool accepted_invalid_status =
            accept_invalid_status_range_ && rangeStatusIsInvalid(status) &&
            range >= min_range_ && range <= runtime->config.max_range_m;
          runtime->has_read = true;
          runtime->status = status;
          runtime->range_m = std::isfinite(range) ? range : std::numeric_limits<double>::quiet_NaN();
          runtime->read_elapsed_ms = elapsed_ms;
          runtime->last_read_time = now;
          if (status == RangeReadStatus::kOk || accepted_invalid_status) {
            runtime->last_ok_time = now;
            runtime->last_valid_status = status;
            runtime->last_valid_range_m = range;
          }
        }

        RCLCPP_DEBUG(
          get_logger(), "ToF read %s(0x%02X): status=%s duration=%.1f ms read_timeout_ms=%d %s",
          runtime->config.name.c_str(), runtime->config.i2c_address, statusToString(status), elapsed_ms,
          runtime->config.read_timeout_ms, debug_state.c_str());

        if (elapsed_ms > publish_period_ms_) {
          RCLCPP_WARN_THROTTLE(
            get_logger(), *get_clock(), 5000,
            "ToF read from %s(0x%02X) took %.1f ms, longer than publish period %.1f ms",
            runtime->config.name.c_str(), runtime->config.i2c_address, elapsed_ms, publish_period_ms_);
        }

        const auto elapsed = std::chrono::steady_clock::now() - cycle_start;
        if (elapsed < read_period) {
          std::this_thread::sleep_for(read_period - elapsed);
        }
      }
    });
  }

  void stopSensorWorkers()
  {
    stop_workers_.store(true);
    for (auto & runtime : sensor_runtimes_) {
      if (runtime->worker.joinable()) {
        runtime->worker.join();
      }
    }
  }

  sensor_msgs::msg::Range makeRangeMessage(
    const rclcpp::Time & stamp,
    const SensorConfig & config) const
  {
    sensor_msgs::msg::Range msg;
    msg.header.stamp = stamp;
    msg.header.frame_id = config.frame_id;
    msg.radiation_type = sensor_msgs::msg::Range::INFRARED;
    msg.field_of_view = static_cast<float>(field_of_view_);
    msg.min_range = static_cast<float>(min_range_);
    msg.max_range = static_cast<float>(config.max_range_m);
    return msg;
  }

  float invalidRangeForStatus(RangeReadStatus status) const
  {
    if (status == RangeReadStatus::kMinRangeFail) {
      return -std::numeric_limits<float>::infinity();
    }
    return std::numeric_limits<float>::infinity();
  }

  std::unique_ptr<TofDevice> makeDevice(const SensorConfig & config) const
  {
    if (config.type == SensorType::kVl53l1x) {
      return std::make_unique<VL53L1XDevice>(config, i2c_bus_);
    }
    return std::make_unique<VL53L0XDevice>(config, i2c_bus_);
  }

  std::string defaultType(const std::string & name) const
  {
    if (name == "rear_center" || name == "new") {
      return "VL53L1X";
    }
    return "VL53L0X";
  }

  double defaultMaxRange(const std::string & name) const
  {
    if (name == "rear_center" || name == "new") {
      return 2.0;
    }
    return max_range_;
  }

  int defaultTimingBudgetMs(const std::string & name) const
  {
    if (defaultType(name) == "VL53L1X") {
      return 50;
    }
    return 20;
  }

  int defaultInterMeasurementMs(const std::string & name) const
  {
    if (defaultType(name) == "VL53L1X") {
      return 50;
    }
    return 20;
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
    if (name == "rear_center" || name == "new") {
      return 25;
    }
    return 0;
  }

  std::vector<std::string> orderedSensorNames(const std::vector<std::string> & sensor_names) const
  {
    const std::vector<std::string> init_order = {
      "front_right", "front_left", "rear_right", "rear_left", "rear_center"};
    std::vector<std::string> ordered_names;
    ordered_names.reserve(sensor_names.size());

    for (const auto & ordered_name : init_order) {
      if (std::find(sensor_names.begin(), sensor_names.end(), ordered_name) != sensor_names.end()) {
        ordered_names.push_back(ordered_name);
      }
    }

    for (const auto & name : sensor_names) {
      if (std::find(init_order.begin(), init_order.end(), name) == init_order.end()) {
        ordered_names.push_back(name);
      }
    }

    return ordered_names;
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
    if (name == "rear_center" || name == "new") {
      return 0x34;
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
    if (name == "rear_center" || name == "new") {
      return "tof_rear_center_link";
    }
    return "tof_" + name + "_link";
  }

  std::string gpio_chip_;
  int i2c_bus_;
  double publish_rate_;
  double publish_period_ms_;
  double timeout_sec_;
  int read_timeout_ms_;
  bool accept_invalid_status_range_;
  double min_range_;
  double max_range_;
  double field_of_view_;
  std::vector<SensorConfig> sensor_configs_;
  std::vector<std::shared_ptr<SensorRuntime>> sensor_runtimes_;
  std::vector<std::unique_ptr<GpiodGpio>> gpios_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::atomic_bool stop_workers_{false};
};

}  // namespace sensor_components

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<sensor_components::TofNode>());
  rclcpp::shutdown();
  return 0;
}
