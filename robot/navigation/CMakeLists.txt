cmake_minimum_required(VERSION 3.8)
project(navigation)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav2_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(navigation_goal_bridge
  src/navigation_goal_bridge_node.cpp
)
ament_target_dependencies(navigation_goal_bridge
  rclcpp
  rclcpp_action
  std_msgs
  geometry_msgs
  nav2_msgs
  tf2
  tf2_geometry_msgs
)
target_link_libraries(navigation_goal_bridge
  nlohmann_json::nlohmann_json
)

install(TARGETS
  navigation_goal_bridge
  DESTINATION lib/${PROJECT_NAME}
)

install(
  DIRECTORY launch config rviz
  DESTINATION share/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
