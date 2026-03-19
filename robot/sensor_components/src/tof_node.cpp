#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <i2c/smbus.h>
#include <gpiod.h>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/range.hpp"

using namespace std::chrono_literals;

class GpioLineController
{
public:
  GpioLineController(const std::string & chip_name, const std::vector<int> & offsets)
  : chip_(nullptr)
  {
    chip_ = gpiod_chip_open_by_name(chip_name.c_str());
    if (!chip_) {
      throw std::runtime_error("Failed to open gpio chip: " + chip_name);
    }

    for (int offset : offsets) {
      gpiod_line * line = gpiod_chip_get_line(chip_, offset);
      if (!line) {
        cleanup();
        throw std::runtime_error("Failed to get gpio line offset: " + std::to_string(offset));
      }

      if (gpiod_line_request_output(line, "tof_xshut", 0) < 0) {
        cleanup();
        throw std::runtime_error("Failed to request gpio line as output: " + std::to_string(offset));
      }

      lines_.push_back(line);
    }
  }

  ~GpioLineController()
  {
    cleanup();
  }

  void set_all(bool level)
  {
    for (auto * line : lines_) {
      gpiod_line_set_value(line, level ? 1 : 0);
    }
  }

  void set(size_t idx, bool level)
  {
    if (idx >= lines_.size()) {
      throw std::out_of_range("GPIO line index out of range");
    }
    gpiod_line_set_value(lines_[idx], level ? 1 : 0);
  }

  size_t size() const
  {
    return lines_.size();
  }

private:
  void cleanup()
  {
    for (auto * line : lines_) {
      if (line) {
        gpiod_line_release(line);
      }
    }
    lines_.clear();

    if (chip_) {
      gpiod_chip_close(chip_);
      chip_ = nullptr;
    }
  }

  gpiod_chip * chip_;
  std::vector<gpiod_line *> lines_;
};

class VL53L0XSensor
{
public:
  VL53L0XSensor(int bus_num, uint8_t address)
  : bus_num_(bus_num), address_(address), fd_(-1)
  {
    open_bus();
  }

  ~VL53L0XSensor()
  {
    if (fd_ >= 0) {
      close(fd_);
      fd_ = -1;
    }
  }

  bool setup()
  {
    try {
      write_reg(0x88, 0x00);
      write_reg(0x80, 0x01);
      write_reg(0xFF, 0x01);
      write_reg(0x00, 0x00);
      write_reg(0x91, read_reg(0x91));
      write_reg(0x00, 0x01);
      write_reg(0xFF, 0x00);
      write_reg(0x80, 0x00);
      write_reg(0x00, 0x01);
      return true;
    } catch (...) {
      return false;
    }
  }

  int get_distance_mm()
  {
    try {
      write_reg(0x00, 0x01);

      int count = 0;
      while ((read_reg(0x13) & 0x01) == 0) {
        std::this_thread::sleep_for(10ms);
        count++;
        if (count > 10) {
          return -1;
        }
      }

      uint8_t high = read_reg(0x1E);
      uint8_t low  = read_reg(0x1F);

      write_reg(0x0B, 0x01);

      int dist = (static_cast<int>(high) << 8) | low;
      return (dist > 20) ? dist : 0;
    } catch (...) {
      return -1;
    }
  }

  static bool change_address(int bus_num, uint8_t old_addr, uint8_t new_addr)
  {
    int fd = -1;
    try {
      std::string dev = "/dev/i2c-" + std::to_string(bus_num);
      fd = open(dev.c_str(), O_RDWR);
      if (fd < 0) {
        return false;
      }

      if (ioctl(fd, I2C_SLAVE, old_addr) < 0) {
        close(fd);
        return false;
      }

      if (i2c_smbus_write_byte_data(fd, 0x8A, new_addr & 0x7F) < 0) {
        close(fd);
        return false;
      }

      close(fd);
      return true;
    } catch (...) {
      if (fd >= 0) {
        close(fd);
      }
      return false;
    }
  }

private:
  void open_bus()
  {
    std::string dev = "/dev/i2c-" + std::to_string(bus_num_);
    fd_ = open(dev.c_str(), O_RDWR);
    if (fd_ < 0) {
      throw std::runtime_error("Failed to open " + dev);
    }

    if (ioctl(fd_, I2C_SLAVE, address_) < 0) {
      close(fd_);
      fd_ = -1;
      throw std::runtime_error("Failed to set I2C slave address");
    }
  }

  void write_reg(uint8_t reg, uint8_t value)
  {
    if (i2c_smbus_write_byte_data(fd_, reg, value) < 0) {
      throw std::runtime_error("I2C write failed");
    }
  }

  uint8_t read_reg(uint8_t reg)
  {
    int value = i2c_smbus_read_byte_data(fd_, reg);
    if (value < 0) {
      throw std::runtime_error("I2C read failed");
    }
    return static_cast<uint8_t>(value);
  }

  int bus_num_;
  uint8_t address_;
  int fd_;
};

class ToFNode : public rclcpp::Node
{
public:
  ToFNode()
  : Node("tof_node")
  {
    declare_parameter<int>("i2c_bus", 1);
    declare_parameter<std::string>("gpio_chip", "gpiochip4");
    declare_parameter<std::vector<int64_t>>("xshut_pins", {17, 27, 22, 23});
    declare_parameter<std::vector<int64_t>>("sensor_addresses", {0x30, 0x31, 0x32, 0x33});
    declare_parameter<std::vector<std::string>>("sensor_names", {"tof0", "tof1", "tof2", "tof3"});
    declare_parameter<std::string>("frame_prefix", "tof_link_");
    declare_parameter<double>("publish_rate_hz", 20.0);
    declare_parameter<double>("field_of_view_rad", 0.436);
    declare_parameter<double>("min_range_m", 0.02);
    declare_parameter<double>("max_range_m", 2.00);
    declare_parameter<int>("radiation_type", 1);

    i2c_bus_ = get_parameter("i2c_bus").as_int();
    gpio_chip_ = get_parameter("gpio_chip").as_string();
    publish_rate_hz_ = get_parameter("publish_rate_hz").as_double();
    field_of_view_rad_ = get_parameter("field_of_view_rad").as_double();
    min_range_m_ = get_parameter("min_range_m").as_double();
    max_range_m_ = get_parameter("max_range_m").as_double();
    radiation_type_ = get_parameter("radiation_type").as_int();
    frame_prefix_ = get_parameter("frame_prefix").as_string();

    auto pin_params = get_parameter("xshut_pins").as_integer_array();
    auto addr_params = get_parameter("sensor_addresses").as_integer_array();
    sensor_names_ = get_parameter("sensor_names").as_string_array();

    if (pin_params.size() != addr_params.size() || pin_params.size() != sensor_names_.size()) {
      throw std::runtime_error("xshut_pins, sensor_addresses, sensor_names sizes must match");
    }

    for (auto p : pin_params) {
      xshut_pins_.push_back(static_cast<int>(p));
    }
    for (auto a : addr_params) {
      sensor_addresses_.push_back(static_cast<uint8_t>(a));
    }

    gpio_ = std::make_unique<GpioLineController>(gpio_chip_, xshut_pins_);

    initialize_sensors();
    create_publishers();

    auto period = std::chrono::duration<double>(1.0 / publish_rate_hz_);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      std::bind(&ToFNode::publish_ranges, this));

    RCLCPP_INFO(get_logger(), "ToF node started with %zu sensors", sensors_.size());
  }

private:
  void initialize_sensors()
  {
    gpio_->set_all(false);
    std::this_thread::sleep_for(500ms);

    for (size_t i = 0; i < xshut_pins_.size(); ++i) {
      gpio_->set(i, true);
      std::this_thread::sleep_for(200ms);

      bool addr_ok = VL53L0XSensor::change_address(i2c_bus_, 0x29, sensor_addresses_[i]);
      if (!addr_ok) {
        RCLCPP_ERROR(
          get_logger(),
          "Failed to change address for sensor %zu -> 0x%02X",
          i, sensor_addresses_[i]);
        continue;
      }

      auto sensor = std::make_shared<VL53L0XSensor>(i2c_bus_, sensor_addresses_[i]);
      if (!sensor->setup()) {
        RCLCPP_ERROR(
          get_logger(),
          "Failed to setup sensor %zu at 0x%02X",
          i, sensor_addresses_[i]);
        continue;
      }

      sensors_.push_back(sensor);

      RCLCPP_INFO(
        get_logger(),
        "Sensor %zu initialized at address 0x%02X",
        i, sensor_addresses_[i]);
    }
  }

  void create_publishers()
  {
    for (size_t i = 0; i < sensors_.size(); ++i) {
      std::string topic = "/" + sensor_names_[i] + "/range";
      auto pub = create_publisher<sensor_msgs::msg::Range>(topic, 10);
      publishers_.push_back(pub);

      RCLCPP_INFO(get_logger(), "Publishing %s", topic.c_str());
    }
  }

  void publish_ranges()
  {
    const auto stamp = now();

    for (size_t i = 0; i < sensors_.size(); ++i) {
      int dist_mm = sensors_[i]->get_distance_mm();

      sensor_msgs::msg::Range msg;
      msg.header.stamp = stamp;
      msg.header.frame_id = frame_prefix_ + std::to_string(i);
      msg.radiation_type = static_cast<uint8_t>(radiation_type_);
      msg.field_of_view = static_cast<float>(field_of_view_rad_);
      msg.min_range = static_cast<float>(min_range_m_);
      msg.max_range = static_cast<float>(max_range_m_);

      if (dist_mm < 0) {
        msg.range = std::numeric_limits<float>::quiet_NaN();
      } else {
        msg.range = static_cast<float>(dist_mm) / 1000.0f;
      }

      publishers_[i]->publish(msg);
    }
  }

  int i2c_bus_;
  std::string gpio_chip_;
  std::string frame_prefix_;
  double publish_rate_hz_;
  double field_of_view_rad_;
  double min_range_m_;
  double max_range_m_;
  int radiation_type_;

  std::vector<int> xshut_pins_;
  std::vector<uint8_t> sensor_addresses_;
  std::vector<std::string> sensor_names_;

  std::unique_ptr<GpioLineController> gpio_;
  std::vector<std::shared_ptr<VL53L0XSensor>> sensors_;
  std::vector<rclcpp::Publisher<sensor_msgs::msg::Range>::SharedPtr> publishers_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  try {
    auto node = std::make_shared<ToFNode>();
    rclcpp::spin(node);
  } catch (const std::exception & e) {
    std::fprintf(stderr, "Fatal error: %s\n", e.what());
  }

  rclcpp::shutdown();
  return 0;
}