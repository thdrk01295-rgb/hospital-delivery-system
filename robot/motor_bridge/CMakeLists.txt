cmake_minimum_required(VERSION 3.8)
project(motor_bridge)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(std_msgs REQUIRED)

add_executable(motor_bridge_node src/motor_bridge_node.cpp)

ament_target_dependencies(motor_bridge_node
  rclcpp
  geometry_msgs
  std_msgs
)

install(TARGETS
  motor_bridge_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY
  launch
  DESTINATION share/${PROJECT_NAME}
)

ament_package()