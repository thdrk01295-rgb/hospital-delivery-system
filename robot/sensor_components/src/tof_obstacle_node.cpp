#include "sensor_components/tof_obstacle_node.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "sensor_msgs/point_cloud2_iterator.hpp"
#include "tf2/exceptions.h"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"

namespace sensor_components
{
namespace
{
constexpr double kMinPublishRate = 1.0;
constexpr double kMaxPublishRate = 100.0;
constexpr double kDefaultMaxValidRange = 1.2;

rclcpp::Time messageTimeOrNow(const sensor_msgs::msg::Range & msg, const rclcpp::Time & now)
{
  if (msg.header.stamp.sec == 0 && msg.header.stamp.nanosec == 0) {
    return now;
  }
  return rclcpp::Time(msg.header.stamp);
}
}  // namespace

TofObstacleNode::TofObstacleNode()
: Node("tof_obstacle_node"),
  output_topic_(declare_parameter<std::string>("output_topic", "/tof/obstacles")),
  output_frame_(declare_parameter<std::string>("output_frame", "base_link")),
  publish_rate_(declare_parameter<double>("publish_rate", 20.0)),
  timeout_sec_(declare_parameter<double>("timeout_sec", 0.2)),
  min_valid_range_(declare_parameter<double>("min_valid_range", 0.03)),
  max_valid_range_(declare_parameter<double>("max_valid_range", kDefaultMaxValidRange))
{
  publish_rate_ = std::clamp(publish_rate_, kMinPublishRate, kMaxPublishRate);
  timeout_sec_ = std::max(0.0, timeout_sec_);

  tf_buffer_ = std::make_shared<tf2_ros::Buffer>(get_clock());
  tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);

  loadSensors();

  cloud_pub_ = create_publisher<sensor_msgs::msg::PointCloud2>(output_topic_, rclcpp::SensorDataQoS());

  const auto period =
    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(1.0 / publish_rate_));
  timer_ = create_wall_timer(period, std::bind(&TofObstacleNode::timerCallback, this));
}

void TofObstacleNode::loadSensors()
{
  const std::vector<std::string> default_sensor_names = {
    "front_left",
    "front_right",
    "rear_left",
    "rear_right",
    "rear_center",
  };
  const std::vector<std::string> sensor_names =
    declare_parameter<std::vector<std::string>>("sensors", default_sensor_names);

  sensors_.reserve(sensor_names.size());
  for (const auto & name : sensor_names) {
    const std::string prefix = "sensors." + name + ".";
    SensorState sensor;
    sensor.name = name;
    sensor.topic = declare_parameter<std::string>(prefix + "topic", "/tof/" + name);
    sensor.frame_id = declare_parameter<std::string>(prefix + "frame_id", "tof_" + name + "_link");
    sensor.max_valid_range = declare_parameter<double>(prefix + "max_valid_range", max_valid_range_);
    sensor.last_received = rclcpp::Time(0, 0, get_clock()->get_clock_type());

    sensors_.push_back(sensor);
  }

  for (std::size_t i = 0; i < sensors_.size(); ++i) {
    sensors_[i].subscription = create_subscription<sensor_msgs::msg::Range>(
      sensors_[i].topic,
      rclcpp::SensorDataQoS(),
      [this, i](const sensor_msgs::msg::Range::SharedPtr msg) {
        rangeCallback(msg, i);
      });
  }
}

void TofObstacleNode::rangeCallback(const sensor_msgs::msg::Range::SharedPtr msg, std::size_t sensor_index)
{
  if (sensor_index >= sensors_.size()) {
    return;
  }

  sensors_[sensor_index].last_msg = msg;
  sensors_[sensor_index].last_received = get_clock()->now();
}

void TofObstacleNode::timerCallback()
{
  const auto now = get_clock()->now();
  std::vector<geometry_msgs::msg::PointStamped> points;
  points.reserve(sensors_.size());

  for (const auto & sensor : sensors_) {
    if (!sensor.last_msg || !sensorIsFresh(sensor, now) || !rangeIsValid(sensor, *sensor.last_msg)) {
      continue;
    }

    geometry_msgs::msg::PointStamped point;
    if (transformObstaclePoint(sensor, *sensor.last_msg, point)) {
      points.push_back(point);
    }
  }

  cloud_pub_->publish(makePointCloud(points, now));
}

bool TofObstacleNode::rangeIsValid(
  const SensorState & sensor,
  const sensor_msgs::msg::Range & msg) const
{
  return std::isfinite(msg.range) && msg.range >= min_valid_range_ && msg.range <= sensor.max_valid_range;
}

bool TofObstacleNode::sensorIsFresh(const SensorState & sensor, const rclcpp::Time & now) const
{
  return (now - sensor.last_received).seconds() <= timeout_sec_;
}

bool TofObstacleNode::transformObstaclePoint(
  const SensorState & sensor,
  const sensor_msgs::msg::Range & msg,
  geometry_msgs::msg::PointStamped & output_point)
{
  geometry_msgs::msg::PointStamped sensor_point;
  sensor_point.header.stamp = messageTimeOrNow(msg, get_clock()->now());
  sensor_point.header.frame_id = sensor.frame_id;
  sensor_point.point.x = msg.range;
  sensor_point.point.y = 0.0;
  sensor_point.point.z = 0.0;

  try {
    output_point = tf_buffer_->transform(sensor_point, output_frame_, tf2::durationFromSec(0.01));
    return true;
  } catch (const tf2::TransformException & ex) {
    RCLCPP_WARN_THROTTLE(
      get_logger(), *get_clock(), 2000, "Failed to transform %s obstacle point to %s: %s",
      sensor.name.c_str(), output_frame_.c_str(), ex.what());
    return false;
  }
}

sensor_msgs::msg::PointCloud2 TofObstacleNode::makePointCloud(
  const std::vector<geometry_msgs::msg::PointStamped> & points,
  const rclcpp::Time & stamp) const
{
  sensor_msgs::msg::PointCloud2 cloud;
  cloud.header.stamp = stamp;
  cloud.header.frame_id = output_frame_;
  cloud.height = 1;
  cloud.width = static_cast<uint32_t>(points.size());
  cloud.is_bigendian = false;
  cloud.is_dense = true;

  sensor_msgs::PointCloud2Modifier modifier(cloud);
  modifier.setPointCloud2FieldsByString(1, "xyz");
  modifier.resize(points.size());

  sensor_msgs::PointCloud2Iterator<float> iter_x(cloud, "x");
  sensor_msgs::PointCloud2Iterator<float> iter_y(cloud, "y");
  sensor_msgs::PointCloud2Iterator<float> iter_z(cloud, "z");

  for (const auto & point : points) {
    *iter_x = static_cast<float>(point.point.x);
    *iter_y = static_cast<float>(point.point.y);
    *iter_z = static_cast<float>(point.point.z);
    ++iter_x;
    ++iter_y;
    ++iter_z;
  }

  return cloud;
}

}  // namespace sensor_components

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<sensor_components::TofObstacleNode>());
  rclcpp::shutdown();
  return 0;
}
