#ifndef ROBOT_TASK__TASK_MOTOR_SEQUENCE_NODE_HPP_
#define ROBOT_TASK__TASK_MOTOR_SEQUENCE_NODE_HPP_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "control/srv/motor_command.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "robot_task/action/execute_motor_sequence.hpp"
#include "std_msgs/msg/bool.hpp"
#include "std_srvs/srv/trigger.hpp"

namespace robot_task
{

enum class MotorPhase
{
  PREPARE,
  FINALIZE
};

struct MotorStep
{
  std::string name;
  std::string command;
  bool is_lift_command;
};

struct SequenceBuildResult
{
  bool success;
  std::string message;
  std::vector<MotorStep> steps;
};

class TaskMotorSequenceNode : public rclcpp::Node
{
public:
  using ExecuteMotorSequence = robot_task::action::ExecuteMotorSequence;
  using GoalHandleExecuteMotorSequence =
    rclcpp_action::ServerGoalHandle<ExecuteMotorSequence>;

  explicit TaskMotorSequenceNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());

#ifdef ROBOT_TASK_ENABLE_SEQUENCE_TESTING
  SequenceBuildResult buildAndValidateSequenceForTest(
    const ExecuteMotorSequence::Goal & goal) const
  {
    return buildAndValidateSequence(goal);
  }
#endif

private:
  rclcpp_action::GoalResponse handleGoal(
    const rclcpp_action::GoalUUID & uuid,
    std::shared_ptr<const ExecuteMotorSequence::Goal> goal);
  rclcpp_action::CancelResponse handleCancel(
    const std::shared_ptr<GoalHandleExecuteMotorSequence> goal_handle);
  void handleAccepted(const std::shared_ptr<GoalHandleExecuteMotorSequence> goal_handle);

  void onMotorReady(const std_msgs::msg::Bool::SharedPtr msg);
  void handleStopMotorSequence(
    const std::shared_ptr<std_srvs::srv::Trigger::Request> request,
    std::shared_ptr<std_srvs::srv::Trigger::Response> response);
  void startGoal(const std::shared_ptr<GoalHandleExecuteMotorSequence> goal_handle);
  void sendNextStep(uint64_t generation);
  void handleMotorResponse(
    uint64_t generation,
    const std::string & step_name,
    rclcpp::Client<control::srv::MotorCommand>::SharedFuture future);
  void requestMotorEmergencyStop(uint64_t generation);
  void finishMotorEmergencyStopRequest(uint64_t generation);

  SequenceBuildResult buildAndValidateSequence(const ExecuteMotorSequence::Goal & goal) const;
  bool validateGoal(
    const ExecuteMotorSequence::Goal & goal,
    MotorPhase & phase,
    std::string & message) const;
  bool buildSequence(
    const ExecuteMotorSequence::Goal & goal,
    MotorPhase phase,
    std::vector<MotorStep> & steps,
    std::string & message) const;
  bool addStep(
    std::vector<MotorStep> & steps,
    const std::string & name,
    const std::string & command_parameter,
    bool is_lift_command,
    std::string & message) const;
  std::string commandParameter(const std::string & name) const;
  bool readyFailureMessageLocked(std::string & message) const;
  bool isOriginlessTaskType(const std::string & task_type) const;
  bool isRouteTaskType(const std::string & task_type) const;
  bool isSupportedTaskType(const std::string & task_type) const;
  void finishGoal(
    uint64_t generation,
    bool success,
    const std::string & message);
  void failGoal(
    const std::shared_ptr<GoalHandleExecuteMotorSequence> & goal_handle,
    const std::string & message);

  rclcpp_action::Server<ExecuteMotorSequence>::SharedPtr action_server_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr motor_ready_sub_;
  rclcpp::Client<control::srv::MotorCommand>::SharedPtr motor_command_client_;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr motor_emergency_stop_client_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr stop_motor_sequence_srv_;

  mutable std::mutex sequence_mutex_;
  bool motor_ready_{false};
  rclcpp::Time last_motor_ready_time_;
  bool sequence_running_{false};
  bool goal_reserved_{false};
  bool stop_requested_{false};
  uint64_t sequence_generation_{0};
  std::shared_ptr<GoalHandleExecuteMotorSequence> active_goal_;
  std::vector<MotorStep> active_steps_;
  std::optional<MotorStep> active_step_;
  std::optional<MotorStep> recent_canceled_step_;
  rclcpp::Time recent_cancel_time_;
  size_t current_step_index_{0};
  double motor_ready_timeout_sec_{2.0};
};

}  // namespace robot_task

#endif  // ROBOT_TASK__TASK_MOTOR_SEQUENCE_NODE_HPP_
