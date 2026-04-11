cmake_minimum_required(VERSION 3.8)
project(server_bridge)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_library(PAHO_MQTT_C_LIB paho-mqtt3as REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(mqtt_bridge_node
  src/main.cpp
  src/mqtt_client.cpp
  src/mqtt_bridge_node.cpp
)

target_include_directories(mqtt_bridge_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

ament_target_dependencies(mqtt_bridge_node rclcpp std_msgs)
target_link_libraries(mqtt_bridge_node ${PAHO_MQTT_C_LIB} nlohmann_json::nlohmann_json)

install(TARGETS mqtt_bridge_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include/)
install(DIRECTORY config/ DESTINATION share/${PROJECT_NAME}/config)
install(DIRECTORY launch/ DESTINATION share/${PROJECT_NAME}/launch)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()