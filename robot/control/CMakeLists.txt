cmake_minimum_required(VERSION 3.8)
project(control)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)

add_executable(motor_bridge_node src/motor_bridge_node.cpp)
ament_target_dependencies(
  motor_bridge_node
  rclcpp
  std_msgs
  geometry_msgs
  nav_msgs
)

add_executable(encoder_odom_node src/encoder_odom_node.cpp)
ament_target_dependencies(
  encoder_odom_node
  rclcpp
  std_msgs
  nav_msgs
  geometry_msgs
  tf2
  tf2_ros
)

install(TARGETS
  motor_bridge_node
  encoder_odom_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)

ament_package()