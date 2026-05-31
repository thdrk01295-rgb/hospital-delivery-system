#include "robot_task/location_mapper.hpp"

#include <fstream>
#include <iostream>

// yaml-cpp (available in ROS2 Jazzy via ament)
#include "yaml-cpp/yaml.h"

namespace robot_task
{

bool LocationMapper::load(const std::string & yaml_path)
{
  try {
    YAML::Node root = YAML::LoadFile(yaml_path);

    YAML::Node locations = root["locations"];
    if (!locations || !locations.IsMap()) {
      std::cerr << "[LocationMapper] 'locations' key not found or not a map in: "
                << yaml_path << "\n";
      return false;
    }

    for (auto it = locations.begin(); it != locations.end(); ++it) {
      std::string code = it->first.as<std::string>();
      Pose2D pose;
      pose.x   = it->second["x"].as<double>();
      pose.y   = it->second["y"].as<double>();
      pose.yaw = it->second["yaw"].as<double>();
      map_[code] = pose;
    }

    std::cout << "[LocationMapper] Loaded " << map_.size()
              << " locations from " << yaml_path << "\n";
    return true;

  } catch (const YAML::Exception & e) {
    std::cerr << "[LocationMapper] YAML parse error: " << e.what() << "\n";
    return false;
  } catch (const std::exception & e) {
    std::cerr << "[LocationMapper] Error loading " << yaml_path
              << ": " << e.what() << "\n";
    return false;
  }
}

std::optional<Pose2D> LocationMapper::resolve(const std::string & code) const
{
  auto it = map_.find(code);
  if (it == map_.end()) {
    return std::nullopt;
  }
  return it->second;
}

}  // namespace robot_task