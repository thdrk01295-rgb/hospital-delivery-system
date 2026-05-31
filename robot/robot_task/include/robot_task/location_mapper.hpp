#ifndef ROBOT_TASK__LOCATION_MAPPER_HPP_
#define ROBOT_TASK__LOCATION_MAPPER_HPP_

#include <string>
#include <unordered_map>
#include <optional>

namespace robot_task
{

struct Pose2D
{
  double x;
  double y;
  double yaw;  // radians
};

/**
 * @brief Loads locations.yaml and resolves location_code → Pose2D.
 *
 * No ROS2 dependencies — can be unit-tested standalone.
 * load() must be called before resolve().
 */
class LocationMapper
{
public:
  LocationMapper() = default;

  /**
   * @brief Load location map from a YAML file path.
   * @return true on success, false if file not found or malformed.
   */
  bool load(const std::string & yaml_path);

  /**
   * @brief Resolve a location_code to a Pose2D.
   * @return Pose2D if found, std::nullopt if unknown code.
   */
  std::optional<Pose2D> resolve(const std::string & code) const;

  /** @brief Return number of loaded locations (useful for logging). */
  std::size_t size() const { return map_.size(); }

private:
  std::unordered_map<std::string, Pose2D> map_;
};

}  // namespace robot_task

#endif  // ROBOT_TASK__LOCATION_MAPPER_HPP_