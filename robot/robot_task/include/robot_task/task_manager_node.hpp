#ifndef ROBOT_TASK__TASK_MANAGER_NODE_HPP_
#define ROBOT_TASK__TASK_MANAGER_NODE_HPP_

// ============================================================
//  Timer durations — change here, recompile
// ============================================================
#define LOADING_TIMER_SEC    3.0
#define UNLOADING_TIMER_SEC  3.0

// Uncomment when NFC hardware is ready.
// When defined, LOADING / UNLOADING states wait for an external
// trigger on /robot/nfc_trigger instead of firing the timer.
// #define USE_NFC_TRIGGER
// ============================================================

#include <string>
#include <optional>
#include <memory>
#include <mutex>

#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "std_msgs/msg/string.hpp"
#include "robot_task/location_mapper.hpp"

namespace robot_task
{

// ── Internal state machine states ───────────────────────────
enum class TaskState
{
  IDLE,
  TASK_RECEIVED,
  MOVING_TO_ORIGIN,
  AT_ORIGIN,
  LOADING,
  MOVING_TO_DESTINATION,
  AT_DESTINATION,
  UNLOADING,
  TASK_COMPLETE,
  ERROR,
  EMERGENCY,
};

// ── Active task data ─────────────────────────────────────────
struct ActiveTask
{
  int         task_id;
  std::string task_type;
  std::string origin;       // may be empty if null
  std::string destination;
  int         priority;
};

// ────────────────────────────────────────────────────────────
class TaskManagerNode : public rclcpp::Node
{
public:
  explicit TaskManagerNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());

private:
  // ── Subscribers ─────────────────────────────────────────
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_task_assign_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_task_cancel_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_task_finish_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_emergency_call_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_nav_result_;

#ifdef USE_NFC_TRIGGER
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_nfc_trigger_;
#endif

  // ── Publishers ──────────────────────────────────────────
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_nav_goal_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_task_state_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_location_code_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_task_complete_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_error_event_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr pub_cmd_vel_;

  // ── Timers ──────────────────────────────────────────────
  rclcpp::TimerBase::SharedPtr loading_timer_;
  rclcpp::TimerBase::SharedPtr unloading_timer_;
  rclcpp::TimerBase::SharedPtr navigation_timeout_timer_;

  // ── State ───────────────────────────────────────────────
  TaskState                state_{TaskState::IDLE};
  std::optional<ActiveTask> active_task_;
  LocationMapper            location_mapper_;
  std::recursive_mutex       state_mutex_;
  double                    navigation_timeout_sec_{300.0};
  std::string               robot_id_{"AMR-001"};

  // ── Callbacks ───────────────────────────────────────────
  void on_task_assign(const std_msgs::msg::String::SharedPtr msg);
  void on_task_cancel(const std_msgs::msg::String::SharedPtr msg);
  void on_task_finish(const std_msgs::msg::String::SharedPtr msg);
  void on_emergency_call(const std_msgs::msg::String::SharedPtr msg);
  void on_nav_result (const std_msgs::msg::String::SharedPtr msg);

#ifdef USE_NFC_TRIGGER
  void on_nfc_trigger(const std_msgs::msg::String::SharedPtr msg);
#endif

  void on_loading_timer();
  void on_unloading_timer();
  void on_navigation_timeout();

  // ── Helpers ─────────────────────────────────────────────
  void transition_to(TaskState next);
  void send_nav_goal(const std::string & code, const std::string & phase);
  void publish_nav_cancel(std::optional<int> task_id);
  void publish_stop_command();
  void start_navigation_timeout();
  void stop_navigation_timeout();
  void publish_task_state();
  void publish_location_code(const std::string & code);
  void publish_task_complete();
  void publish_error(const std::string & message);
  void enter_error(const std::string & message);
  void enter_emergency();
  void reset_to_idle();
  bool is_patient_task() const;
  bool is_supported_task_type(const std::string & task_type) const;

  static std::string state_to_string(TaskState s);
  std::string state_to_robot_state(TaskState s) const;
};

}  // namespace robot_task

#endif  // ROBOT_TASK__TASK_MANAGER_NODE_HPP_
