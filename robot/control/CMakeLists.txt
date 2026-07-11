cmake_minimum_required(VERSION 3.8)
project(control)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "srv/MotorCommand.srv"
)

add_executable(motor_bridge_node src/motor_bridge_node.cpp)
target_include_directories(motor_bridge_node PRIVATE include)
ament_target_dependencies(
  motor_bridge_node
  rclcpp
  std_msgs
  geometry_msgs
  sensor_msgs
)

add_executable(control_motor_bridge_node src/control_motor_bridge_node.cpp)
ament_target_dependencies(
  control_motor_bridge_node
  rclcpp
  std_msgs
  std_srvs
)
rosidl_get_typesupport_target(cpp_typesupport_target ${PROJECT_NAME} "rosidl_typesupport_cpp")
target_link_libraries(control_motor_bridge_node "${cpp_typesupport_target}")

add_executable(encoder_odom_node src/encoder_odom_node.cpp)
target_include_directories(encoder_odom_node PRIVATE include)
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
  control_motor_bridge_node
  encoder_odom_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(control_motor_response_matcher_test
    test/control_motor_response_matcher_test.cpp
  )
  if(TARGET control_motor_response_matcher_test)
    ament_target_dependencies(
      control_motor_response_matcher_test
      rclcpp
      std_msgs
      std_srvs
    )
    add_dependencies(control_motor_response_matcher_test ${PROJECT_NAME}__cpp)
    target_link_libraries(control_motor_response_matcher_test "${cpp_typesupport_target}")
  endif()
endif()

ament_export_dependencies(rosidl_default_runtime)
ament_package()
