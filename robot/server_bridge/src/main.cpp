#include "rclcpp/rclcpp.hpp"
#include "server_bridge/mqtt_bridge_node.hpp"

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  auto node = std::make_shared<server_bridge::MqttBridgeNode>();

  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}