#include <cmath>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "std_msgs/msg/string.hpp"
#include "tf2/LinearMath/Quaternion.h"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"

using json = nlohmann::json;

namespace navigation
{
namespace
{

struct GoalRequest
{
  int task_id;
  std::string phase;
  std::string location_code;
  double x;
  double y;
  double yaw;
};

bool is_valid_phase(const std::string & phase)
{
  return phase == "ORIGIN" || phase == "DESTINATION";
}

}  // namespace

class NavigationGoalBridge : public rclcpp::Node
{
public:
  using NavigateToPose = nav2_msgs::action::NavigateToPose;
  using GoalHandle = rclcpp_action::ClientGoalHandle<NavigateToPose>;

  NavigationGoalBridge()
  : Node("navigation_goal_bridge")
  {
    goal_sub_ = create_subscription<std_msgs::msg::String>(
      "/robot/navigation_goal",
      10,
      std::bind(&NavigationGoalBridge::on_goal_message, this, std::placeholders::_1));
    result_pub_ = create_publisher<std_msgs::msg::String>("/robot/navigation_result", 10);
    nav_client_ = rclcpp_action::create_client<NavigateToPose>(this, "/navigate_to_pose");

    RCLCPP_INFO(
      get_logger(),
      "navigation_goal_bridge ready: /robot/navigation_goal -> /navigate_to_pose");
  }

private:
  void on_goal_message(const std_msgs::msg::String::SharedPtr msg)
  {
    try {
      const json payload = json::parse(msg->data);
      if (payload.value("cancel", false)) {
        handle_cancel(payload);
        return;
      }

      auto request = parse_goal(payload);
      if (!request) {
        return;
      }

      std::lock_guard<std::mutex> lock(mutex_);
      if (active_context_) {
        pending_goal_ = *request;
        RCLCPP_WARN(
          get_logger(),
          "New navigation goal received while task_id=%d phase=%s is active; canceling current goal first",
          active_context_->task_id,
          active_context_->phase.c_str());
        cancel_active_locked("replaced by a new navigation goal");
        return;
      }

      send_goal_locked(*request);
    } catch (const json::exception & e) {
      RCLCPP_ERROR(get_logger(), "navigation_goal JSON parse error: %s", e.what());
    } catch (const std::exception & e) {
      RCLCPP_ERROR(get_logger(), "navigation_goal handling error: %s", e.what());
    }
  }

  std::optional<GoalRequest> parse_goal(const json & payload)
  {
    const char * required_fields[] = {"task_id", "phase", "x", "y", "yaw"};
    for (const char * field : required_fields) {
      if (!payload.contains(field) || payload[field].is_null()) {
        RCLCPP_ERROR(get_logger(), "navigation_goal missing required field: %s", field);
        return std::nullopt;
      }
    }

    if (!payload["task_id"].is_number_integer()) {
      RCLCPP_ERROR(get_logger(), "navigation_goal task_id must be an integer");
      return std::nullopt;
    }
    if (!payload["phase"].is_string()) {
      RCLCPP_ERROR(get_logger(), "navigation_goal phase must be a string");
      return std::nullopt;
    }
    if (!payload["x"].is_number() || !payload["y"].is_number() || !payload["yaw"].is_number()) {
      RCLCPP_ERROR(get_logger(), "navigation_goal x, y, and yaw must be numeric");
      return std::nullopt;
    }

    GoalRequest request;
    request.task_id = payload["task_id"].get<int>();
    request.phase = payload["phase"].get<std::string>();
    request.location_code = payload.value("location_code", std::string{});
    request.x = payload["x"].get<double>();
    request.y = payload["y"].get<double>();
    request.yaw = payload["yaw"].get<double>();

    if (!is_valid_phase(request.phase)) {
      RCLCPP_ERROR(get_logger(), "navigation_goal invalid phase: %s", request.phase.c_str());
      return std::nullopt;
    }
    if (!std::isfinite(request.x) || !std::isfinite(request.y) || !std::isfinite(request.yaw)) {
      RCLCPP_ERROR(get_logger(), "navigation_goal x, y, and yaw must be finite");
      return std::nullopt;
    }

    return request;
  }

  void send_goal_locked(const GoalRequest & request)
  {
    if (!nav_client_->wait_for_action_server(std::chrono::seconds(1))) {
      RCLCPP_ERROR(
        get_logger(),
        "Nav2 action server /navigate_to_pose is unavailable for task_id=%d phase=%s",
        request.task_id,
        request.phase.c_str());
      publish_result(request, "FAILURE");
      return;
    }

    tf2::Quaternion q;
    q.setRPY(0.0, 0.0, request.yaw);
    const double length = q.length();
    if (!std::isfinite(length) || length <= 0.0) {
      RCLCPP_ERROR(
        get_logger(),
        "Cannot normalize goal quaternion for task_id=%d phase=%s yaw=%.17g",
        request.task_id,
        request.phase.c_str(),
        request.yaw);
      publish_result(request, "FAILURE");
      return;
    }
    q.normalize();
    if (!std::isfinite(q.x()) || !std::isfinite(q.y()) ||
      !std::isfinite(q.z()) || !std::isfinite(q.w()))
    {
      RCLCPP_ERROR(
        get_logger(),
        "Goal quaternion is not finite for task_id=%d phase=%s yaw=%.17g",
        request.task_id,
        request.phase.c_str(),
        request.yaw);
      publish_result(request, "FAILURE");
      return;
    }

    NavigateToPose::Goal goal;
    goal.pose.header.frame_id = "map";
    goal.pose.header.stamp = now();
    goal.pose.pose.position.x = request.x;
    goal.pose.pose.position.y = request.y;
    goal.pose.pose.position.z = 0.0;
    goal.pose.pose.orientation = tf2::toMsg(q);

    active_context_ = request;
    current_goal_handle_.reset();
    const auto generation = ++generation_;

    rclcpp_action::Client<NavigateToPose>::SendGoalOptions options;
    options.goal_response_callback =
      [this, generation](const GoalHandle::SharedPtr & goal_handle) {
        on_goal_response(generation, goal_handle);
      };
    options.result_callback =
      [this, generation](const GoalHandle::WrappedResult & result) {
        on_nav_result(generation, result);
      };

    nav_client_->async_send_goal(goal, options);
    RCLCPP_INFO(
      get_logger(),
      "Nav2 goal requested: task_id=%d phase=%s code=%s x=%.3f y=%.3f yaw=%.6f qz=%.9f qw=%.9f",
      request.task_id,
      request.phase.c_str(),
      request.location_code.empty() ? "<empty>" : request.location_code.c_str(),
      request.x,
      request.y,
      request.yaw,
      q.z(),
      q.w());
  }

  void on_goal_response(
    uint64_t generation,
    const GoalHandle::SharedPtr & goal_handle)
  {
    std::optional<GoalRequest> failed_context;
    std::optional<GoalRequest> pending;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (generation != generation_ || !active_context_) {
        RCLCPP_DEBUG(get_logger(), "Ignoring stale Nav2 goal response");
        return;
      }

      if (!goal_handle) {
        failed_context = active_context_;
        RCLCPP_ERROR(
          get_logger(),
          "Nav2 goal rejected: task_id=%d phase=%s code=%s",
          active_context_->task_id,
          active_context_->phase.c_str(),
          active_context_->location_code.c_str());
        clear_active_locked();
        pending = take_pending_locked();
      } else {
        current_goal_handle_ = goal_handle;
        RCLCPP_INFO(
          get_logger(),
          "Nav2 goal accepted: task_id=%d phase=%s code=%s",
          active_context_->task_id,
          active_context_->phase.c_str(),
          active_context_->location_code.c_str());
        if (pending_goal_) {
          cancel_active_locked("pending replacement goal is waiting");
        }
      }
    }

    if (failed_context) {
      publish_result(*failed_context, "FAILURE");
    }
    send_pending_if_any(pending);
  }

  void on_nav_result(
    uint64_t generation,
    const GoalHandle::WrappedResult & result)
  {
    std::optional<GoalRequest> context;
    std::optional<GoalRequest> pending;
    std::string task_result;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (generation != generation_ || !active_context_) {
        RCLCPP_DEBUG(get_logger(), "Ignoring stale Nav2 result");
        return;
      }

      context = active_context_;
      switch (result.code) {
        case rclcpp_action::ResultCode::SUCCEEDED:
          task_result = "SUCCESS";
          RCLCPP_INFO(
            get_logger(),
            "Nav2 goal succeeded: task_id=%d phase=%s code=%s",
            context->task_id,
            context->phase.c_str(),
            context->location_code.c_str());
          break;
        case rclcpp_action::ResultCode::ABORTED:
          task_result = "FAILURE";
          RCLCPP_ERROR(
            get_logger(),
            "Nav2 goal aborted: task_id=%d phase=%s code=%s",
            context->task_id,
            context->phase.c_str(),
            context->location_code.c_str());
          break;
        case rclcpp_action::ResultCode::CANCELED:
          task_result = "CANCELLED";
          RCLCPP_WARN(
            get_logger(),
            "Nav2 goal canceled: task_id=%d phase=%s code=%s",
            context->task_id,
            context->phase.c_str(),
            context->location_code.c_str());
          break;
        default:
          task_result = "FAILURE";
          RCLCPP_ERROR(
            get_logger(),
            "Nav2 goal returned unknown result code: task_id=%d phase=%s code=%s",
            context->task_id,
            context->phase.c_str(),
            context->location_code.c_str());
          break;
      }

      clear_active_locked();
      pending = take_pending_locked();
    }

    publish_result(*context, task_result);
    send_pending_if_any(pending);
  }

  void handle_cancel(const json & payload)
  {
    std::optional<int> task_id;
    if (payload.contains("task_id") && !payload["task_id"].is_null()) {
      if (!payload["task_id"].is_number_integer()) {
        RCLCPP_WARN(get_logger(), "navigation cancel task_id must be an integer or null");
        return;
      }
      task_id = payload["task_id"].get<int>();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    pending_goal_.reset();
    if (!active_context_) {
      RCLCPP_WARN(get_logger(), "Navigation cancel received without an active goal");
      return;
    }
    if (task_id && *task_id != active_context_->task_id) {
      RCLCPP_WARN(
        get_logger(),
        "Ignoring navigation cancel for task_id=%d while active task_id=%d",
        *task_id,
        active_context_->task_id);
      return;
    }

    cancel_active_locked("cancel message received");
  }

  void cancel_active_locked(const std::string & reason)
  {
    if (!active_context_) {
      return;
    }

    if (!current_goal_handle_) {
      RCLCPP_WARN(
        get_logger(),
        "Navigation cancel requested before Nav2 accepted the goal: task_id=%d phase=%s reason=%s",
        active_context_->task_id,
        active_context_->phase.c_str(),
        reason.c_str());
      return;
    }

    RCLCPP_WARN(
      get_logger(),
      "Canceling Nav2 goal: task_id=%d phase=%s code=%s reason=%s",
      active_context_->task_id,
      active_context_->phase.c_str(),
      active_context_->location_code.c_str(),
      reason.c_str());
    nav_client_->async_cancel_goal(current_goal_handle_);
  }

  void publish_result(const GoalRequest & context, const std::string & result)
  {
    json payload;
    payload["task_id"] = context.task_id;
    payload["phase"] = context.phase;
    payload["location_code"] = context.location_code;
    payload["result"] = result;

    auto msg = std_msgs::msg::String{};
    msg.data = payload.dump();
    result_pub_->publish(msg);
  }

  void clear_active_locked()
  {
    active_context_.reset();
    current_goal_handle_.reset();
  }

  std::optional<GoalRequest> take_pending_locked()
  {
    auto pending = pending_goal_;
    pending_goal_.reset();
    return pending;
  }

  void send_pending_if_any(const std::optional<GoalRequest> & pending)
  {
    if (!pending) {
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (active_context_) {
      pending_goal_ = pending;
      return;
    }
    send_goal_locked(*pending);
  }

  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr goal_sub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr result_pub_;
  rclcpp_action::Client<NavigateToPose>::SharedPtr nav_client_;

  std::mutex mutex_;
  std::optional<GoalRequest> active_context_;
  std::optional<GoalRequest> pending_goal_;
  GoalHandle::SharedPtr current_goal_handle_;
  uint64_t generation_{0};
};

}  // namespace navigation

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<navigation::NavigationGoalBridge>());
  rclcpp::shutdown();
  return 0;
}
