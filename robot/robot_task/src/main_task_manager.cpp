#include "rclcpp/rclcpp.hpp"
#include "robot_task/task_manager_node.hpp"

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  auto node = std::make_shared<robot_task::TaskManagerNode>();
  rclcpp::spin(node);

  rclcpp::shutdown();
  return 0;
}