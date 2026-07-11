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

#include <nlohmann/json.hpp>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_srvs/srv/trigger.hpp"
#include "robot_task/action/execute_motor_sequence.hpp"
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
  WAIT_UNLOCK,
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
  std::optional<int> order_top;
  std::optional<int> order_bottom;
};

// ────────────────────────────────────────────────────────────
class TaskManagerNode : public rclcpp::Node
{
public:
  explicit TaskManagerNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());

private:
  using ExecuteMotorSequence = robot_task::action::ExecuteMotorSequence;
  using MotorGoalHandle = rclcpp_action::ClientGoalHandle<ExecuteMotorSequence>;

  enum class PendingMotorPhase
  {
    NONE,
    PREPARE,
    FINALIZE
  };

  // ── Subscribers ─────────────────────────────────────────
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_task_assign_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_task_cancel_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_task_finish_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_lock_command_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_lock_status_feedback_;
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
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_lock_status_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr pub_cmd_vel_;
  rclcpp_action::Client<ExecuteMotorSequence>::SharedPtr motor_sequence_client_;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr motor_sequence_stop_client_;

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
  bool                      lock_mock_enabled_{true};
  std::optional<int>         navigation_task_id_;
  std::string               navigation_phase_;
  std::string               unlock_phase_;
  bool                      navigation_in_progress_{false};
  bool                      waiting_patient_finish_{false};
  bool                      lock_open_{false};
  bool                      pending_finish_after_lock_{false};
  bool                      pending_destination_after_lock_{false};
  bool                      motor_sequence_pending_{false};
  bool                      interaction_ready_{false};
  PendingMotorPhase         pending_motor_phase_{PendingMotorPhase::NONE};
  int64_t                   pending_motor_task_id_{-1};
  uint64_t                  motor_request_generation_{0};
  MotorGoalHandle::SharedPtr active_motor_goal_handle_;
  std::string               pending_lock_command_;

  // ── Callbacks ───────────────────────────────────────────
  void on_task_assign(const std_msgs::msg::String::SharedPtr msg);
  void on_task_cancel(const std_msgs::msg::String::SharedPtr msg);
  void on_task_finish(const std_msgs::msg::String::SharedPtr msg);
  void on_lock_command(const std_msgs::msg::String::SharedPtr msg);
  void on_lock_status_feedback(const std_msgs::msg::String::SharedPtr msg);
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
  bool send_nav_goal(const std::string & code, const std::string & phase);
  void publish_nav_cancel(std::optional<int> task_id);
  void publish_stop_command();
  void start_navigation_timeout();
  void stop_navigation_timeout();
  void publish_task_state();
  void publish_location_code(const std::string & code);
  void publish_task_complete();
  void publish_lock_status(
    const std::string & command,
    const std::string & status,
    const std::optional<std::string> & message = std::nullopt);
  void publish_error(const std::string & message);
  void enter_error(const std::string & message);
  void enter_emergency();
  void handle_lock_opened();
  void handle_lock_locked();
  void close_lock_before_finish();
  void clear_interaction_context();
  void request_motor_sequence(
    const std::string & phase,
    const std::string & lock_command);
  void on_motor_goal_response(
    uint64_t generation,
    int64_t task_id,
    PendingMotorPhase phase,
    const std::string & lock_command,
    const MotorGoalHandle::SharedPtr & goal_handle);
  void on_motor_feedback(
    uint64_t generation,
    int64_t task_id,
    PendingMotorPhase phase,
    const MotorGoalHandle::SharedPtr & goal_handle,
    const std::shared_ptr<const ExecuteMotorSequence::Feedback> feedback);
  void on_motor_result(
    uint64_t generation,
    int64_t task_id,
    PendingMotorPhase phase,
    const std::string & lock_command,
    const MotorGoalHandle::WrappedResult & result);
  void cancel_motor_sequence();
  void request_motor_sequence_stop();
  void clear_motor_sequence_context();
  bool is_current_motor_request(
    uint64_t generation,
    int64_t task_id,
    PendingMotorPhase phase) const;
  bool can_accept_lock_command() const;
  static std::string pending_motor_phase_to_string(PendingMotorPhase phase);
  void move_to_destination_after_origin_finish();
  void finish_task_from_server();
  void clear_task_context();
  void clear_navigation_context();
  void reset_to_idle();
  bool can_accept_unlock_command() const;
  void log_unlock_rejected(
    const std::optional<int> & command_task_id,
    const std::string & reason) const;
  bool is_patient_task() const;
  bool is_battery_low_task() const;
  bool requires_task_finish(const ActiveTask & task) const;
  bool is_supported_task_type(const std::string & task_type) const;
  bool is_originless_task_type(const std::string & task_type) const;
  std::optional<int> parse_order_selection(
    const nlohmann::json & payload,
    const char * field,
    bool & valid,
    std::string & error_message) const;

  static std::string state_to_string(TaskState s);
  std::string state_to_robot_state(TaskState s) const;
};

}  // namespace robot_task

#endif  // ROBOT_TASK__TASK_MANAGER_NODE_HPP_
