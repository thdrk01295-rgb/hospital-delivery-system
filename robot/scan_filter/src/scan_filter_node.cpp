#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"

class ScanFilterNode : public rclcpp::Node
{
public:
  ScanFilterNode()
  : Node("scan_filter_node")
  {
    input_scan_topic_ = declare_parameter<std::string>("input_scan_topic", "/scan_raw");
    output_scan_topic_ = declare_parameter<std::string>("output_scan_topic", "/scan");
    declare_parameter<bool>("enabled", true);
    declare_parameter<double>("center_angle_rad", 0.0);
    declare_parameter<double>("remove_angle_width_rad", 0.06);
    declare_parameter<bool>("use_nan", true);
    declare_parameter<double>("replacement_range", 0.0);
    declare_parameter<bool>("debug_log", false);

    publisher_ = create_publisher<sensor_msgs::msg::LaserScan>(
      output_scan_topic_, rclcpp::SensorDataQoS());
    subscription_ = create_subscription<sensor_msgs::msg::LaserScan>(
      input_scan_topic_,
      rclcpp::SensorDataQoS(),
      std::bind(&ScanFilterNode::scan_callback, this, std::placeholders::_1));

    RCLCPP_INFO(
      get_logger(), "Filtering LaserScan from '%s' to '%s'",
      input_scan_topic_.c_str(), output_scan_topic_.c_str());
  }

private:
  void scan_callback(const sensor_msgs::msg::LaserScan::SharedPtr scan)
  {
    auto filtered_scan = *scan;

    if (scan->ranges.empty()) {
      RCLCPP_WARN(get_logger(), "Received LaserScan with empty ranges; publishing unchanged");
      publisher_->publish(filtered_scan);
      return;
    }

    if (scan->angle_increment == 0.0F) {
      RCLCPP_WARN(
        get_logger(), "Received LaserScan with zero angle_increment; publishing unchanged");
      publisher_->publish(filtered_scan);
      return;
    }

    const bool enabled = get_parameter("enabled").as_bool();
    if (!enabled) {
      publisher_->publish(filtered_scan);
      return;
    }

    const double center_angle_rad = get_parameter("center_angle_rad").as_double();
    const double remove_angle_width_rad =
      get_parameter("remove_angle_width_rad").as_double();
    const bool use_nan = get_parameter("use_nan").as_bool();
    const float replacement_range =
      static_cast<float>(get_parameter("replacement_range").as_double());
    const bool debug_log = get_parameter("debug_log").as_bool();

    if (remove_angle_width_rad < 0.0) {
      RCLCPP_WARN(
        get_logger(), "remove_angle_width_rad is negative; publishing unchanged");
      publisher_->publish(filtered_scan);
      return;
    }

    const double half_width = remove_angle_width_rad / 2.0;
    std::size_t removed_count = 0;

    for (std::size_t i = 0; i < filtered_scan.ranges.size(); ++i) {
      const double angle =
        static_cast<double>(scan->angle_min) +
        static_cast<double>(i) * static_cast<double>(scan->angle_increment);

      if (std::abs(angle - center_angle_rad) <= half_width) {
        filtered_scan.ranges[i] = use_nan ?
          std::numeric_limits<float>::quiet_NaN() : replacement_range;

        if (i < filtered_scan.intensities.size()) {
          filtered_scan.intensities[i] = 0.0F;
        }
        ++removed_count;
      }
    }

    if (debug_log) {
      RCLCPP_INFO(
        get_logger(),
        "Removed %zu/%zu beams around %.6f rad with total width %.6f rad",
        removed_count, filtered_scan.ranges.size(), center_angle_rad,
        remove_angle_width_rad);
    }

    publisher_->publish(filtered_scan);
  }

  std::string input_scan_topic_;
  std::string output_scan_topic_;
  rclcpp::Publisher<sensor_msgs::msg::LaserScan>::SharedPtr publisher_;
  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr subscription_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ScanFilterNode>());
  rclcpp::shutdown();
  return 0;
}
