#include "robot_task/task_motor_sequence_node.hpp"

#include <algorithm>
#include <functional>
#include <utility>

namespace robot_task
{

namespace
{

constexpr const char * kActionName = "/robot_task/execute_motor_sequence";

}  // namespace

TaskMotorSequenceNode::TaskMotorSequenceNode(const rclcpp::NodeOptions & options)
: Node("task_motor_sequence_node", options)
{
  last_motor_ready_time_ = now();
  const auto motor_ready_topic =
    declare_parameter<std::string>("motor_ready_topic", "/control/motor_ready");
  const auto motor_service_name =
    declare_parameter<std::string>("motor_service_name", "/control/motor_command");
  motor_ready_timeout_sec_ = declare_parameter<double>("motor_ready_timeout_sec", 2.0);

  declare_parameter<std::string>("lift_level_1_command", "1l");
  declare_parameter<std::string>("lift_level_2_command", "2l");
  declare_parameter<std::string>("lift_level_3_command", "3l");
  declare_parameter<std::string>("lift_top_command", "tl");
  declare_parameter<std::string>("lift_stop_command", "k");
  declare_parameter<std::string>("clothes_step_forward_command", "w");
  declare_parameter<std::string>("kit_step_forward_command", "r");
  declare_parameter<std::string>("servo_1_release_command", "");
  declare_parameter<std::string>("servo_1_lock_command", "");
  declare_parameter<std::string>("servo_2_release_command", "");
  declare_parameter<std::string>("servo_2_lock_command", "");
  declare_parameter<std::string>("servo_3_release_command", "");
  declare_parameter<std::string>("servo_3_lock_command", "");
  declare_parameter<std::string>("servo_4_release_command", "");
  declare_parameter<std::string>("servo_4_lock_command", "");
  declare_parameter<std::string>("servo_5_release_command", "");
  declare_parameter<std::string>("servo_5_lock_command", "");
  declare_parameter<std::string>("door_open_command", "");
  declare_parameter<std::string>("door_close_command", "");
  declare_parameter<std::string>("lift_home_command", "");
  declare_parameter<std::string>("reset_command", "");

  motor_ready_sub_ = create_subscription<std_msgs::msg::Bool>(
    motor_ready_topic,
    rclcpp::QoS(1).transient_local().reliable(),
    std::bind(&TaskMotorSequenceNode::onMotorReady, this, std::placeholders::_1));

  motor_command_client_ = create_client<control::srv::MotorCommand>(motor_service_name);

  action_server_ = rclcpp_action::create_server<ExecuteMotorSequence>(
    this,
    kActionName,
    std::bind(
      &TaskMotorSequenceNode::handleGoal, this,
      std::placeholders::_1, std::placeholders::_2),
    std::bind(
      &TaskMotorSequenceNode::handleCancel, this,
      std::placeholders::_1),
    std::bind(
      &TaskMotorSequenceNode::handleAccepted, this,
      std::placeholders::_1));

  RCLCPP_INFO(get_logger(), "TaskMotorSequenceNode ready: action=%s", kActionName);
}

rclcpp_action::GoalResponse TaskMotorSequenceNode::handleGoal(
  const rclcpp_action::GoalUUID & uuid,
  std::shared_ptr<const ExecuteMotorSequence::Goal> goal)
{
  (void)uuid;
  RCLCPP_INFO(
    get_logger(),
    "Motor sequence goal received: task_id=%ld type=%s stop=%s phase=%s",
    goal->task_id, goal->task_type.c_str(), goal->stop_type.c_str(), goal->phase.c_str());
  return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
}

rclcpp_action::CancelResponse TaskMotorSequenceNode::handleCancel(
  const std::shared_ptr<GoalHandleExecuteMotorSequence> goal_handle)
{
  bool cancel_active = false;
  {
    std::lock_guard<std::mutex> lock(sequence_mutex_);
    cancel_active = sequence_running_ && active_goal_ == goal_handle;
    if (cancel_active) {
      ++sequence_generation_;
      sequence_running_ = false;
      active_steps_.clear();
      current_step_index_ = 0;
      active_goal_.reset();
    }
  }

  if (cancel_active) {
    auto result = std::make_shared<ExecuteMotorSequence::Result>();
    result->success = false;
    result->message = "motor sequence canceled";
    goal_handle->canceled(result);
    RCLCPP_WARN(get_logger(), "Motor sequence canceled");
  }
  return rclcpp_action::CancelResponse::ACCEPT;
}

void TaskMotorSequenceNode::handleAccepted(
  const std::shared_ptr<GoalHandleExecuteMotorSequence> goal_handle)
{
  startGoal(goal_handle);
}

void TaskMotorSequenceNode::onMotorReady(const std_msgs::msg::Bool::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock(sequence_mutex_);
  motor_ready_ = msg->data;
  last_motor_ready_time_ = now();
}

void TaskMotorSequenceNode::startGoal(
  const std::shared_ptr<GoalHandleExecuteMotorSequence> goal_handle)
{
  const auto goal = goal_handle->get_goal();
  MotorPhase phase;
  std::string message;
  if (!validateGoal(*goal, phase, message)) {
    failGoal(goal_handle, message);
    return;
  }

  std::vector<MotorStep> steps;
  if (!buildSequence(*goal, phase, steps, message)) {
    failGoal(goal_handle, message);
    return;
  }

  if (steps.empty()) {
    auto result = std::make_shared<ExecuteMotorSequence::Result>();
    result->success = true;
    result->message = "no motor sequence required for battery_low";
    goal_handle->succeed(result);
    return;
  }

  uint64_t generation = 0;
  {
    std::lock_guard<std::mutex> lock(sequence_mutex_);
    if (sequence_running_) {
      message = "motor sequence is already running";
    } else if (readyFailureMessageLocked(message)) {
      // message populated by helper
    } else if (!motor_command_client_->service_is_ready()) {
      message = "motor command service is unavailable";
    } else {
      sequence_running_ = true;
      generation = ++sequence_generation_;
      active_goal_ = goal_handle;
      active_steps_ = std::move(steps);
      current_step_index_ = 0;
    }
  }

  if (generation == 0) {
    failGoal(goal_handle, message);
    return;
  }

  sendNextStep(generation);
}

void TaskMotorSequenceNode::sendNextStep(uint64_t generation)
{
  std::shared_ptr<GoalHandleExecuteMotorSequence> goal_handle;
  MotorStep step;
  int32_t current_step = 0;
  int32_t total_steps = 0;
  std::string failure_message;

  {
    std::lock_guard<std::mutex> lock(sequence_mutex_);
    if (!sequence_running_ || generation != sequence_generation_ || !active_goal_) {
      return;
    }

    if (readyFailureMessageLocked(failure_message)) {
      // handled outside the lock
    } else if (current_step_index_ >= active_steps_.size()) {
      goal_handle = active_goal_;
    } else {
      step = active_steps_[current_step_index_];
      current_step = static_cast<int32_t>(current_step_index_ + 1);
      total_steps = static_cast<int32_t>(active_steps_.size());
      goal_handle = active_goal_;
    }
  }

  if (!failure_message.empty()) {
    finishGoal(generation, false, failure_message);
    return;
  }

  if (current_step == 0) {
    finishGoal(generation, true, "motor sequence completed");
    return;
  }

  auto feedback = std::make_shared<ExecuteMotorSequence::Feedback>();
  feedback->current_command = step.name;
  feedback->current_step = current_step;
  feedback->total_steps = total_steps;
  goal_handle->publish_feedback(feedback);

  auto request = std::make_shared<control::srv::MotorCommand::Request>();
  request->command = step.command;

  try {
    motor_command_client_->async_send_request(
      request,
      [this, generation, step_name = step.name](
        rclcpp::Client<control::srv::MotorCommand>::SharedFuture future) {
        handleMotorResponse(generation, step_name, future);
      });
  } catch (const std::exception & e) {
    finishGoal(
      generation,
      false,
      "motor service call failed for step " + step.name + ": " + e.what());
  }
}

void TaskMotorSequenceNode::handleMotorResponse(
  uint64_t generation,
  const std::string & step_name,
  rclcpp::Client<control::srv::MotorCommand>::SharedFuture future)
{
  std::shared_ptr<control::srv::MotorCommand::Response> response;
  try {
    response = future.get();
  } catch (const std::exception & e) {
    finishGoal(
      generation,
      false,
      "motor service response failed for step " + step_name + ": " + e.what());
    return;
  }

  {
    std::lock_guard<std::mutex> lock(sequence_mutex_);
    if (!sequence_running_ || generation != sequence_generation_ || !active_goal_) {
      return;
    }
  }

  if (!response->success) {
    finishGoal(
      generation,
      false,
      "motor step failed: " + step_name + ": " + response->response);
    return;
  }

  {
    std::lock_guard<std::mutex> lock(sequence_mutex_);
    if (!sequence_running_ || generation != sequence_generation_ || !active_goal_) {
      return;
    }
    ++current_step_index_;
  }

  sendNextStep(generation);
}

void TaskMotorSequenceNode::requestLiftStop()
{
  const auto command = commandParameter("lift_stop_command");
  if (command.empty() || !motor_command_client_->service_is_ready()) {
    RCLCPP_WARN(get_logger(), "Lift stop request skipped");
    return;
  }

  auto request = std::make_shared<control::srv::MotorCommand::Request>();
  request->command = command;
  motor_command_client_->async_send_request(request);
}

bool TaskMotorSequenceNode::validateGoal(
  const ExecuteMotorSequence::Goal & goal,
  MotorPhase & phase,
  std::string & message) const
{
  if (goal.phase == "PREPARE") {
    phase = MotorPhase::PREPARE;
  } else if (goal.phase == "FINALIZE") {
    phase = MotorPhase::FINALIZE;
  } else {
    message = "invalid phase: " + goal.phase;
    return false;
  }

  if (goal.stop_type != "origin" && goal.stop_type != "destination") {
    message = "invalid stop_type: " + goal.stop_type;
    return false;
  }

  if (goal.order_top != 0 && goal.order_top != 1) {
    message = "order_top must be 0 or 1";
    return false;
  }
  if (goal.order_bottom != 0 && goal.order_bottom != 1) {
    message = "order_bottom must be 0 or 1";
    return false;
  }

  if (goal.task_type == "emergency_call") {
    message = "emergency_call is not allowed for motor sequence action";
    return false;
  }
  if (!isSupportedTaskType(goal.task_type)) {
    message = "unsupported task_type: " + goal.task_type;
    return false;
  }
  if (isOriginlessTaskType(goal.task_type) && goal.stop_type != "destination") {
    message = "originless task requires stop_type=destination: " + goal.task_type;
    return false;
  }
  if (isRouteTaskType(goal.task_type) &&
    goal.stop_type != "origin" && goal.stop_type != "destination")
  {
    message = "invalid stop_type: " + goal.stop_type;
    return false;
  }
  if (goal.task_type == "patient_clothes_rental" &&
    goal.order_top == 0 && goal.order_bottom == 0)
  {
    message = "patient clothing item selection is missing";
    return false;
  }

  return true;
}

bool TaskMotorSequenceNode::buildSequence(
  const ExecuteMotorSequence::Goal & goal,
  MotorPhase phase,
  std::vector<MotorStep> & steps,
  std::string & message)
{
  if (goal.task_type == "battery_low") {
    return true;
  }

  const bool prepare = phase == MotorPhase::PREPARE;

  if (goal.task_type == "clothes_refill") {
    if (prepare) {
      return addStep(steps, "servo_4_release", "servo_4_release_command", false, message) &&
        addStep(steps, "servo_3_release", "servo_3_release_command", false, message);
    }
    return addStep(steps, "servo_4_lock", "servo_4_lock_command", false, message) &&
      addStep(steps, "servo_3_lock", "servo_3_lock_command", false, message);
  }

  if (goal.task_type == "kit_refill") {
    if (prepare) {
      return addStep(steps, "servo_2_release", "servo_2_release_command", false, message);
    }
    return addStep(steps, "servo_2_lock", "servo_2_lock_command", false, message);
  }

  if (goal.task_type == "kit_delivery") {
    if (prepare) {
      return addStep(steps, "lift_level_3", "lift_level_3_command", true, message) &&
        addStep(steps, "kit_step_forward", "kit_step_forward_command", false, message) &&
        addStep(steps, "lift_top", "lift_top_command", true, message);
    }
    return addStep(steps, "lift_home", "lift_home_command", true, message);
  }

  if (goal.task_type == "specimen_delivery" || goal.task_type == "logistics_delivery") {
    if (prepare) {
      return addStep(steps, "servo_1_release", "servo_1_release_command", false, message);
    }
    return addStep(steps, "servo_1_lock", "servo_1_lock_command", false, message);
  }

  if (goal.task_type == "used_clothes_collection") {
    if (prepare) {
      return addStep(steps, "servo_5_release", "servo_5_release_command", false, message);
    }
    return addStep(steps, "servo_5_lock", "servo_5_lock_command", false, message);
  }

  if (goal.task_type == "patient_clothes_rental") {
    if (prepare) {
      if (!addStep(steps, "door_open", "door_open_command", false, message)) {
        return false;
      }
      if (goal.order_top == 1) {
        if (!addStep(steps, "lift_level_1", "lift_level_1_command", true, message) ||
          !addStep(steps, "clothes_step_forward", "clothes_step_forward_command", false, message) ||
          !addStep(steps, "lift_top", "lift_top_command", true, message))
        {
          return false;
        }
      }
      if (goal.order_bottom == 1) {
        if (!addStep(steps, "lift_level_2", "lift_level_2_command", true, message) ||
          !addStep(steps, "clothes_step_forward", "clothes_step_forward_command", false, message) ||
          !addStep(steps, "lift_top", "lift_top_command", true, message))
        {
          return false;
        }
      }
      return true;
    }
    return addStep(steps, "door_close", "door_close_command", false, message) &&
      addStep(steps, "lift_home", "lift_home_command", true, message);
  }

  if (goal.task_type == "patient_clothes_return") {
    if (prepare) {
      return addStep(steps, "door_open", "door_open_command", false, message);
    }
    return addStep(steps, "door_close", "door_close_command", false, message);
  }

  message = "unsupported task_type: " + goal.task_type;
  return false;
}

bool TaskMotorSequenceNode::addStep(
  std::vector<MotorStep> & steps,
  const std::string & name,
  const std::string & command_parameter,
  bool is_lift_command,
  std::string & message)
{
  const auto command = commandParameter(command_parameter);
  if (command.empty()) {
    message = "motor command parameter is empty: " + command_parameter;
    return false;
  }
  steps.push_back(MotorStep{name, command, is_lift_command});
  return true;
}

std::string TaskMotorSequenceNode::commandParameter(const std::string & name) const
{
  return get_parameter(name).as_string();
}

bool TaskMotorSequenceNode::readyFailureMessageLocked(std::string & message) const
{
  if (!motor_ready_) {
    message = "motor controller is not ready";
    return true;
  }

  const auto elapsed = (now() - last_motor_ready_time_).seconds();
  if (elapsed > motor_ready_timeout_sec_) {
    message = "motor ready state is stale";
    return true;
  }

  return false;
}

bool TaskMotorSequenceNode::isOriginlessTaskType(const std::string & task_type) const
{
  return task_type == "kit_delivery" ||
    task_type == "kit_refill" ||
    task_type == "clothes_refill" ||
    task_type == "patient_clothes_rental" ||
    task_type == "patient_clothes_return" ||
    task_type == "battery_low";
}

bool TaskMotorSequenceNode::isRouteTaskType(const std::string & task_type) const
{
  return task_type == "specimen_delivery" ||
    task_type == "logistics_delivery" ||
    task_type == "used_clothes_collection";
}

bool TaskMotorSequenceNode::isSupportedTaskType(const std::string & task_type) const
{
  return isOriginlessTaskType(task_type) ||
    isRouteTaskType(task_type);
}

void TaskMotorSequenceNode::finishGoal(
  uint64_t generation,
  bool success,
  const std::string & message)
{
  std::shared_ptr<GoalHandleExecuteMotorSequence> goal_handle;
  {
    std::lock_guard<std::mutex> lock(sequence_mutex_);
    if (!sequence_running_ || generation != sequence_generation_ || !active_goal_) {
      return;
    }
    goal_handle = active_goal_;
    ++sequence_generation_;
    sequence_running_ = false;
    active_steps_.clear();
    current_step_index_ = 0;
    active_goal_.reset();
  }

  auto result = std::make_shared<ExecuteMotorSequence::Result>();
  result->success = success;
  result->message = message;
  if (success) {
    goal_handle->succeed(result);
  } else if (goal_handle->is_canceling()) {
    goal_handle->canceled(result);
  } else {
    goal_handle->abort(result);
  }
}

void TaskMotorSequenceNode::failGoal(
  const std::shared_ptr<GoalHandleExecuteMotorSequence> & goal_handle,
  const std::string & message)
{
  auto result = std::make_shared<ExecuteMotorSequence::Result>();
  result->success = false;
  result->message = message;
  if (goal_handle->is_canceling()) {
    goal_handle->canceled(result);
  } else {
    goal_handle->abort(result);
  }
}

}  // namespace robot_task
