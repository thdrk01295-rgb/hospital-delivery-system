cmake_minimum_required(VERSION 3.8)
project(sensor_components)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -O2)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)

add_executable(tof_node src/tof_node.cpp)
ament_target_dependencies(tof_node rclcpp sensor_msgs)
target_link_libraries(tof_node gpiod i2c)

install(TARGETS
  tof_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME}
)

ament_package()