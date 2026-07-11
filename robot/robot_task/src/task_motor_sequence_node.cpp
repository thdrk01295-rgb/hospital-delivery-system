#include "robot_task/task_motor_sequence_node.hpp"

#include <algorithm>
#include <functional>
#include <sstream>
#include <utility>

namespace robot_task
{

namespace
{

constexpr const char * kActionName = "/robot_task/execute_motor_sequence";

std::string sequenceKey(
  const std::string & task_type,
  const std::string & stop_type,
  const std::string & phase)
{
  return task_type + "/" + stop_type + "/" + phase;
}

std::string formatMotorSteps(const std::vector<MotorStep> & steps)
{
  std::ostringstream out;
  for (size_t i = 0; i < steps.size(); ++i) {
    if (i > 0) {
      out << ", ";
    }
    out << steps[i].name << "(" << steps[i].command << ")";
  }
  return out.str();
}

}  // namespace

TaskMotorSequenceNode::TaskMotorSequenceNode(const rclcpp::NodeOptions & options)
: Node("task_motor_sequence_node", options)
{
  last_motor_ready_time_ = now();
  recent_cancel_time_ = now();
  const auto motor_ready_topic =
    declare_parameter<std::string>("motor_ready_topic", "/control/motor_ready");
  const auto motor_service_name =
    declare_parameter<std::string>("motor_service_name", "/control/motor_command");
  const auto motor_emergency_stop_service_name =
    declare_parameter<std::string>(
      "motor_emergency_stop_service_name", "/control/motor_emergency_stop");
  const auto stop_motor_sequence_service_name =
    declare_parameter<std::string>(
      "stop_motor_sequence_service_name", "/robot_task/stop_motor_sequence");
  motor_ready_timeout_sec_ = declare_parameter<double>("motor_ready_timeout_sec", 2.0);

  declare_parameter<std::string>("lift_level_1_command", "");
  declare_parameter<std::string>("lift_level_2_command", "");
  declare_parameter<std::string>("lift_level_3_command", "");
  declare_parameter<std::string>("lift_top_command", "LIFT:500");
  declare_parameter<std::string>("lift_stop_command", "k");
  declare_parameter<std::string>("top_clothes_step_forward_command", "w");
  declare_parameter<std::string>("bottom_clothes_step_forward_command", "e");
  declare_parameter<std::string>("kit_step_forward_command", "r");
  declare_parameter<std::string>("servo_1_release_command", "1o");
  declare_parameter<std::string>("servo_1_lock_command", "1c");
  declare_parameter<std::string>("servo_2_release_command", "2o");
  declare_parameter<std::string>("servo_2_lock_command", "2c");
  declare_parameter<std::string>("servo_3_release_command", "3o");
  declare_parameter<std::string>("servo_3_lock_command", "3c");
  declare_parameter<std::string>("servo_4_release_command", "4o");
  declare_parameter<std::string>("servo_4_lock_command", "4c");
  declare_parameter<std::string>("servo_5_release_command", "5o");
  declare_parameter<std::string>("servo_5_lock_command", "5c");
  declare_parameter<std::string>("door_open_command", "o");
  declare_parameter<std::string>("door_close_command", "c");
  declare_parameter<std::string>("lift_home_command", "");
  declare_parameter<std::string>("reset_command", "reset");

  motor_ready_sub_ = create_subscription<std_msgs::msg::Bool>(
    motor_ready_topic,
    rclcpp::QoS(1).transient_local().reliable(),
    std::bind(&TaskMotorSequenceNode::onMotorReady, this, std::placeholders::_1));

  motor_command_client_ = create_client<control::srv::MotorCommand>(motor_service_name);
  motor_emergency_stop_client_ =
    create_client<std_srvs::srv::Trigger>(motor_emergency_stop_service_name);
  stop_motor_sequence_srv_ = create_service<std_srvs::srv::Trigger>(
    stop_motor_sequence_service_name,
    std::bind(
      &TaskMotorSequenceNode::handleStopMotorSequence, this,
      std::placeholders::_1, std::placeholders::_2));

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

  RCLCPP_INFO(
    get_logger(), "TaskMotorSequenceNode ready: action=%s stop_service=%s",
    kActionName, stop_motor_sequence_service_name.c_str());
}

rclcpp_action::GoalResponse TaskMotorSequenceNode::handleGoal(
  const rclcpp_action::GoalUUID & uuid,
  std::shared_ptr<const ExecuteMotorSequence::Goal> goal)
{
  (void)uuid;
  RCLCPP_INFO(
    get_logger(),
    "Motor sequence goal received: task_id=%ld type=%s stop=%s phase=%s sequence_key=%s",
    goal->task_id,
    goal->task_type.c_str(),
    goal->stop_type.c_str(),
    goal->phase.c_str(),
    sequenceKey(goal->task_type, goal->stop_type, goal->phase).c_str());

  auto build_result = buildAndValidateSequence(*goal);
  if (!build_result.success) {
    RCLCPP_WARN(
      get_logger(),
      "Rejected motor sequence goal: task_id=%ld task_type=%s phase=%s sequence_key=%s reason=%s",
      goal->task_id,
      goal->task_type.c_str(),
      goal->phase.c_str(),
      sequenceKey(goal->task_type, goal->stop_type, goal->phase).c_str(),
      build_result.message.c_str());
    return rclcpp_action::GoalResponse::REJECT;
  }

  std::string message;
  {
    std::lock_guard<std::mutex> lock(sequence_mutex_);
    if (sequence_running_ || goal_reserved_) {
      RCLCPP_WARN(get_logger(), "Motor sequence goal rejected: motor sequence is already running");
      return rclcpp_action::GoalResponse::REJECT;
    }
    if (stop_requested_) {
      RCLCPP_WARN(get_logger(), "Motor sequence goal rejected: emergency stop is in progress");
      return rclcpp_action::GoalResponse::REJECT;
    }
    if (readyFailureMessageLocked(message)) {
      RCLCPP_WARN(get_logger(), "Motor sequence goal rejected: %s", message.c_str());
      return rclcpp_action::GoalResponse::REJECT;
    }
    goal_reserved_ = true;
  }

  if (!motor_command_client_->service_is_ready()) {
    RCLCPP_WARN(get_logger(), "Motor sequence goal rejected: motor command service is unavailable");
    std::lock_guard<std::mutex> lock(sequence_mutex_);
    goal_reserved_ = false;
    return rclcpp_action::GoalResponse::REJECT;
  }

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
      recent_canceled_step_ = active_step_;
      recent_cancel_time_ = now();
      active_step_.reset();
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

void TaskMotorSequenceNode::handleStopMotorSequence(
  const std::shared_ptr<std_srvs::srv::Trigger::Request> request,
  std::shared_ptr<std_srvs::srv::Trigger::Response> response)
{
  (void)request;

  std::shared_ptr<GoalHandleExecuteMotorSequence> goal_handle;
  bool had_sequence = false;
  bool should_stop_lift = false;
  uint64_t generation = 0;

  {
    std::lock_guard<std::mutex> lock(sequence_mutex_);
    const bool recent_lift_cancel =
      recent_canceled_step_ &&
      recent_canceled_step_->is_lift_command &&
      (now() - recent_cancel_time_).seconds() <= 2.0;
    if (!sequence_running_ && !goal_reserved_ && !recent_lift_cancel) {
      response->success = true;
      response->message = "no active motor sequence";
      return;
    }

    had_sequence = sequence_running_ || recent_lift_cancel;
    should_stop_lift = (active_step_ && active_step_->is_lift_command) || recent_lift_cancel;
    generation = ++sequence_generation_;
    stop_requested_ = should_stop_lift;
    sequence_running_ = false;
    goal_reserved_ = false;
    active_steps_.clear();
    active_step_.reset();
    recent_canceled_step_.reset();
    current_step_index_ = 0;
    goal_handle = active_goal_;
    active_goal_.reset();
  }

  if (goal_handle) {
    auto result = std::make_shared<ExecuteMotorSequence::Result>();
    result->success = false;
    result->message = "motor sequence stopped";
    if (goal_handle->is_canceling()) {
      goal_handle->canceled(result);
    } else {
      goal_handle->abort(result);
    }
  }

  if (!had_sequence) {
    response->success = true;
    response->message = "motor sequence reservation canceled";
    return;
  }

  if (!should_stop_lift) {
    response->success = true;
    response->message = "motor sequence canceled; active motor does not support immediate stop";
    return;
  }

  if (!motor_emergency_stop_client_->service_is_ready()) {
    finishMotorEmergencyStopRequest(generation);
    response->success = false;
    response->message = "motor sequence canceled but lift emergency stop service is unavailable";
    return;
  }

  requestMotorEmergencyStop(generation);
  response->success = true;
  response->message = "motor sequence canceled and lift emergency stop requested";
}

void TaskMotorSequenceNode::startGoal(
  const std::shared_ptr<GoalHandleExecuteMotorSequence> goal_handle)
{
  const auto goal = goal_handle->get_goal();
  auto build_result = buildAndValidateSequence(*goal);
  if (!build_result.success) {
    std::lock_guard<std::mutex> lock(sequence_mutex_);
    goal_reserved_ = false;
    RCLCPP_WARN(
      get_logger(),
      "Motor sequence lookup failed after accept: task_id=%ld task_type=%s phase=%s "
      "sequence_key=%s reason=%s",
      goal->task_id,
      goal->task_type.c_str(),
      goal->phase.c_str(),
      sequenceKey(goal->task_type, goal->stop_type, goal->phase).c_str(),
      build_result.message.c_str());
    failGoal(goal_handle, build_result.message);
    return;
  }

  if (build_result.steps.empty()) {
    {
      std::lock_guard<std::mutex> lock(sequence_mutex_);
      goal_reserved_ = false;
    }
    RCLCPP_INFO(
      get_logger(),
      "Motor sequence completed: task_id=%ld task_type=%s phase=%s sequence_key=%s",
      goal->task_id,
      goal->task_type.c_str(),
      goal->phase.c_str(),
      sequenceKey(goal->task_type, goal->stop_type, goal->phase).c_str());
    auto result = std::make_shared<ExecuteMotorSequence::Result>();
    result->success = true;
    result->message = "no motor sequence required for battery_low";
    goal_handle->succeed(result);
    return;
  }

  const std::string steps_text = formatMotorSteps(build_result.steps);
  std::string message;
  uint64_t generation = 0;
  {
    std::lock_guard<std::mutex> lock(sequence_mutex_);
    if (!goal_reserved_) {
      message = "motor sequence canceled before start";
    } else if (sequence_running_) {
      message = "motor sequence is already running";
    } else if (stop_requested_) {
      message = "emergency stop is in progress";
    } else if (readyFailureMessageLocked(message)) {
      // message populated by helper
    } else if (!motor_command_client_->service_is_ready()) {
      message = "motor command service is unavailable";
    } else {
      goal_reserved_ = false;
      sequence_running_ = true;
      generation = ++sequence_generation_;
      active_goal_ = goal_handle;
      active_steps_ = std::move(build_result.steps);
      active_step_.reset();
      recent_canceled_step_.reset();
      current_step_index_ = 0;
    }
    if (generation == 0) {
      goal_reserved_ = false;
    }
  }

  if (generation == 0) {
    failGoal(goal_handle, message);
    return;
  }

  RCLCPP_INFO(
    get_logger(),
    "Starting motor sequence: task_id=%ld task_type=%s stop_type=%s phase=%s sequence_key=%s "
    "steps=%s",
    goal->task_id,
    goal->task_type.c_str(),
    goal->stop_type.c_str(),
    goal->phase.c_str(),
    sequenceKey(goal->task_type, goal->stop_type, goal->phase).c_str(),
    steps_text.c_str());

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
      active_step_ = step;
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

  RCLCPP_INFO(
    get_logger(),
    "Motor sequence step %d/%d: name=%s command=%s",
    current_step, total_steps, step.name.c_str(), step.command.c_str());

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
      RCLCPP_INFO(
        get_logger(),
        "Motor sequence stale response ignored: generation=%lu step=%s",
        generation, step_name.c_str());
      return;
    }
  }

  if (!response->success) {
    if (response->response.find("timeout waiting") != std::string::npos) {
      RCLCPP_WARN(
        get_logger(),
        "Motor sequence timeout: step=%s response=%s",
        step_name.c_str(), response->response.c_str());
    } else {
      RCLCPP_WARN(
        get_logger(),
        "Motor sequence step failure: step=%s response=%s",
        step_name.c_str(), response->response.c_str());
    }
    finishGoal(
      generation,
      false,
      "motor step failed: " + step_name + ": " + response->response);
    return;
  }

  {
    std::lock_guard<std::mutex> lock(sequence_mutex_);
    if (!sequence_running_ || generation != sequence_generation_ || !active_goal_) {
      RCLCPP_INFO(
        get_logger(),
        "Motor sequence stale response ignored: generation=%lu step=%s",
        generation, step_name.c_str());
      return;
    }
    ++current_step_index_;
  }

  RCLCPP_INFO(
    get_logger(),
    "Motor sequence step success: step=%s response=%s",
    step_name.c_str(), response->response.c_str());
  sendNextStep(generation);
}

void TaskMotorSequenceNode::requestMotorEmergencyStop(uint64_t generation)
{
  auto request = std::make_shared<std_srvs::srv::Trigger::Request>();
  try {
    motor_emergency_stop_client_->async_send_request(
      request,
      [this, generation](rclcpp::Client<std_srvs::srv::Trigger>::SharedFuture future) {
        try {
          const auto result = future.get();
          if (result->success) {
            RCLCPP_WARN(get_logger(), "Motor emergency stop result: %s", result->message.c_str());
          } else {
            RCLCPP_ERROR(get_logger(), "Motor emergency stop failed: %s", result->message.c_str());
          }
        } catch (const std::exception & e) {
          RCLCPP_ERROR(get_logger(), "Motor emergency stop response failed: %s", e.what());
        }
        finishMotorEmergencyStopRequest(generation);
      });
  } catch (const std::exception & e) {
    RCLCPP_ERROR(get_logger(), "Motor emergency stop request failed: %s", e.what());
    finishMotorEmergencyStopRequest(generation);
  }
}

void TaskMotorSequenceNode::finishMotorEmergencyStopRequest(uint64_t generation)
{
  std::lock_guard<std::mutex> lock(sequence_mutex_);
  if (generation == sequence_generation_) {
    stop_requested_ = false;
  }
}

SequenceBuildResult TaskMotorSequenceNode::buildAndValidateSequence(
  const ExecuteMotorSequence::Goal & goal) const
{
  MotorPhase phase;
  std::string message;
  if (!validateGoal(goal, phase, message)) {
    return SequenceBuildResult{false, message, {}};
  }

  std::vector<MotorStep> steps;
  if (!buildSequence(goal, phase, steps, message)) {
    return SequenceBuildResult{false, message, {}};
  }

  return SequenceBuildResult{true, "", std::move(steps)};
}

bool TaskMotorSequenceNode::validateGoal(
  const ExecuteMotorSequence::Goal & goal,
  MotorPhase & phase,
  std::string & message) const
{
  if (goal.task_id <= 0) {
    message = "invalid task_id";
    return false;
  }

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
  std::string & message) const
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
      const bool needs_top = goal.order_top == 1;
      const bool needs_bottom = goal.order_bottom == 1;
      if (needs_top) {
        if (!addStep(steps, "lift_level_1", "lift_level_1_command", true, message) ||
          !addStep(
            steps,
            "top_clothes_step_forward",
            "top_clothes_step_forward_command",
            false,
            message))
        {
          return false;
        }
      }
      if (needs_bottom) {
        if (!needs_top &&
          !addStep(steps, "lift_level_2", "lift_level_2_command", true, message))
        {
          return false;
        }
        if (!addStep(
            steps,
            "bottom_clothes_step_forward",
            "bottom_clothes_step_forward_command",
            false,
            message))
        {
          return false;
        }
      }
      if (needs_top || needs_bottom) {
        return addStep(steps, "lift_top", "lift_top_command", true, message);
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
  std::string & message) const
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
  std::optional<MotorStep> step;
  {
    std::lock_guard<std::mutex> lock(sequence_mutex_);
    if (!sequence_running_ || generation != sequence_generation_ || !active_goal_) {
      return;
    }
    goal_handle = active_goal_;
    step = active_step_;
    ++sequence_generation_;
    sequence_running_ = false;
    active_steps_.clear();
    active_step_.reset();
    recent_canceled_step_.reset();
    current_step_index_ = 0;
    active_goal_.reset();
  }

  auto result = std::make_shared<ExecuteMotorSequence::Result>();
  result->success = success;
  result->message = message;
  const auto goal = goal_handle->get_goal();
  if (success) {
    RCLCPP_INFO(
      get_logger(),
      "Motor sequence completed: task_id=%ld phase=%s",
      goal->task_id, goal->phase.c_str());
  } else {
    RCLCPP_WARN(
      get_logger(),
      "Motor sequence failed: task_id=%ld phase=%s step=%s response=%s",
      goal->task_id,
      goal->phase.c_str(),
      step ? step->name.c_str() : "none",
      message.c_str());
  }
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
