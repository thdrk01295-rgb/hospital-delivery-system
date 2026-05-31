cmake_minimum_required(VERSION 3.8)
project(robot_task)

# Default to C++17
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ── Find dependencies ─────────────────────────────────────────
find_package(ament_cmake       REQUIRED)
find_package(rclcpp            REQUIRED)
find_package(std_msgs          REQUIRED)
find_package(geometry_msgs     REQUIRED)
find_package(ament_index_cpp   REQUIRED)
find_package(yaml_cpp_vendor   REQUIRED)
find_package(yaml-cpp          REQUIRED)

# nlohmann/json — header-only, installed via apt (nlohmann-json3-dev)
find_package(nlohmann_json REQUIRED)

# ── Include dirs ──────────────────────────────────────────────
include_directories(
  include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ── location_mapper (shared utility, no ROS deps) ─────────────
add_library(location_mapper STATIC
  src/location_mapper.cpp
)
target_include_directories(location_mapper PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(location_mapper yaml-cpp::yaml-cpp)

# ── task_manager_node executable ─────────────────────────────
add_executable(task_manager_node
  src/task_manager_node.cpp
  src/main_task_manager.cpp
)
ament_target_dependencies(task_manager_node
  rclcpp
  std_msgs
  geometry_msgs
  ament_index_cpp
)
target_link_libraries(task_manager_node
  location_mapper
  nlohmann_json::nlohmann_json
  yaml-cpp::yaml-cpp
)

# ── Install ───────────────────────────────────────────────────
install(TARGETS
  task_manager_node
  location_mapper
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/
  DESTINATION include
)

install(DIRECTORY config/
  DESTINATION share/${PROJECT_NAME}/config
)

# ── Linting (optional) ────────────────────────────────────────
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
