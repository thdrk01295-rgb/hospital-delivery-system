#ifndef SENSOR_COMPONENTS_TOF_OBSTACLE_NODE_HPP_
#define SENSOR_COMPONENTS_TOF_OBSTACLE_NODE_HPP_

#include <memory>
#include <string>
#include <vector>

#include "geometry_msgs/msg/point_stamped.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"
#include "sensor_msgs/msg/range.hpp"
#include "tf2_ros/buffer.h"
#include "tf2_ros/transform_listener.h"

namespace sensor_components
{

class TofObstacleNode : public rclcpp::Node
{
public:
  TofObstacleNode();

private:
  struct SensorState
  {
    std::string name;
    std::string topic;
    std::string frame_id;
    double max_valid_range;
    rclcpp::Subscription<sensor_msgs::msg::Range>::SharedPtr subscription;
    sensor_msgs::msg::Range::SharedPtr last_msg;
    rclcpp::Time last_received;
  };

  void rangeCallback(const sensor_msgs::msg::Range::SharedPtr msg, std::size_t sensor_index);
  void timerCallback();
  bool rangeIsValid(const SensorState & sensor, const sensor_msgs::msg::Range & msg) const;
  bool sensorIsFresh(const SensorState & sensor, const rclcpp::Time & now) const;
  bool transformObstaclePoint(
    const SensorState & sensor,
    const sensor_msgs::msg::Range & msg,
    geometry_msgs::msg::PointStamped & output_point);
  sensor_msgs::msg::PointCloud2 makePointCloud(
    const std::vector<geometry_msgs::msg::PointStamped> & points,
    const rclcpp::Time & stamp) const;
  void loadSensors();

  std::string output_topic_;
  std::string output_frame_;
  double publish_rate_;
  double timeout_sec_;
  double min_valid_range_;
  double max_valid_range_;

  std::vector<SensorState> sensors_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr cloud_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::shared_ptr<tf2_ros::Buffer> tf_buffer_;
  std::shared_ptr<tf2_ros::TransformListener> tf_listener_;
};

}  // namespace sensor_components

#endif  // SENSOR_COMPONENTS_TOF_OBSTACLE_NODE_HPP_
