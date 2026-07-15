cmake_minimum_required(VERSION 3.8)
project(sensor_components)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(tf2_ros REQUIRED)
pkg_check_modules(GPIOD REQUIRED IMPORTED_TARGET libgpiod)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(collision_guard_policy src/collision_guard_policy.cpp)
target_include_directories(collision_guard_policy PUBLIC
  include
)

add_executable(imu_node src/imu_node.cpp)
target_include_directories(imu_node PRIVATE
  include
)
ament_target_dependencies(imu_node
  rclcpp
  sensor_msgs
)

add_executable(tof_node src/tof_node.cpp)
ament_target_dependencies(tof_node
  rclcpp
  sensor_msgs
)
target_link_libraries(tof_node
  PkgConfig::GPIOD
)

add_executable(tof_obstacle_node src/tof_obstacle_node.cpp)
target_include_directories(tof_obstacle_node PRIVATE
  include
)
ament_target_dependencies(tof_obstacle_node
  geometry_msgs
  rclcpp
  sensor_msgs
  tf2
  tf2_geometry_msgs
  tf2_ros
)

add_executable(collision_guard_node src/collision_guard_node.cpp)
target_include_directories(collision_guard_node PRIVATE
  include
)
ament_target_dependencies(collision_guard_node
  diagnostic_msgs
  geometry_msgs
  rclcpp
  sensor_msgs
)
target_link_libraries(collision_guard_node
  collision_guard_policy
)

add_executable(battery_node src/battery_node.cpp)
ament_target_dependencies(battery_node
  rclcpp
  std_msgs
)

install(TARGETS imu_node tof_node tof_obstacle_node collision_guard_node battery_node
  DESTINATION lib/${PROJECT_NAME}
)

install(PROGRAMS
  scripts/tof_node.py
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(collision_guard_policy_test
    test/collision_guard_policy_test.cpp
  )
  target_include_directories(collision_guard_policy_test PRIVATE
    include
  )
  target_link_libraries(collision_guard_policy_test
    collision_guard_policy
  )
endif()

ament_package()
