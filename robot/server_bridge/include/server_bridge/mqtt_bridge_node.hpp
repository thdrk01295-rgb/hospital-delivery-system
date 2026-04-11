#ifndef SERVER_BRIDGE__MQTT_BRIDGE_NODE_HPP_
#define SERVER_BRIDGE__MQTT_BRIDGE_NODE_HPP_

#include <memory>
#include <string>
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_msgs/msg/float32.hpp"
#include "std_msgs/msg/int32.hpp"
#include "server_bridge/mqtt_client.hpp"

namespace server_bridge
{

class MqttBridgeNode : public rclcpp::Node
{
public:
  explicit MqttBridgeNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());
  ~MqttBridgeNode() override;

private:
  void declareParameters();
  void initMqtt();
  void initRos();

  void onMqttMessage(const std::string & topic, const std::string & payload);

  void onTaskState(const std_msgs::msg::String::SharedPtr msg);
  void onLocationCode(const std_msgs::msg::String::SharedPtr msg);
  void onBatteryState(const std_msgs::msg::Float32::SharedPtr msg);
  void onErrorEvent(const std_msgs::msg::String::SharedPtr msg);
  void onTaskCompleteEvent(const std_msgs::msg::Int32::SharedPtr msg);

  std::string robot_id_;
  std::string broker_uri_;
  int qos_;

  std::unique_ptr<MqttClient> mqtt_client_;

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_task_assign_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_task_cancel_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_emergency_call_;

  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr  sub_task_state_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr  sub_location_code_;
  rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr sub_battery_state_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr  sub_error_event_;
  rclcpp::Subscription<std_msgs::msg::Int32>::SharedPtr   sub_task_complete_;
};

}  // namespace server_bridge

#endif  // SERVER_BRIDGE__MQTT_BRIDGE_NODE_HPP_