#include "robot_task/task_manager_node.hpp"

#include <nlohmann/json.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>

#include <chrono>

using json = nlohmann::json;
using std::placeholders::_1;

namespace robot_task
{

// ============================================================
// Constructor
// ============================================================
TaskManagerNode::TaskManagerNode(const rclcpp::NodeOptions & options)
: Node("task_manager_node", options)
{
  robot_id_ = declare_parameter<std::string>("robot_id", "AMR-001");
  navigation_timeout_sec_ = declare_parameter<double>("navigation_timeout_sec", 300.0);
  if (navigation_timeout_sec_ <= 0.0) {
    RCLCPP_WARN(get_logger(),
      "navigation_timeout_sec must be positive; using default 300.0 sec");
    navigation_timeout_sec_ = 300.0;
  }

  // ── Load location map ──────────────────────────────────
  std::string pkg_share =
    ament_index_cpp::get_package_share_directory("robot_task");
  std::string yaml_path = pkg_share + "/config/locations.yaml";

  if (!location_mapper_.load(yaml_path)) {
    RCLCPP_FATAL(get_logger(),
      "Failed to load locations.yaml from: %s", yaml_path.c_str());
    throw std::runtime_error("LocationMapper load failed");
  }
  RCLCPP_INFO(get_logger(),
    "LocationMapper ready — %zu locations loaded", location_mapper_.size());

  // ── Subscribers ────────────────────────────────────────
  sub_task_assign_ = create_subscription<std_msgs::msg::String>(
    "/server/task_assign", 10,
    std::bind(&TaskManagerNode::on_task_assign, this, _1));

  sub_task_cancel_ = create_subscription<std_msgs::msg::String>(
    "/server/task_cancel", 10,
    std::bind(&TaskManagerNode::on_task_cancel, this, _1));

  sub_task_finish_ = create_subscription<std_msgs::msg::String>(
    "/server/task_finish", 10,
    std::bind(&TaskManagerNode::on_task_finish, this, _1));

  sub_emergency_call_ = create_subscription<std_msgs::msg::String>(
    "/server/emergency_call", 10,
    std::bind(&TaskManagerNode::on_emergency_call, this, _1));

  sub_nav_result_ = create_subscription<std_msgs::msg::String>(
    "/robot/navigation_result", 10,
    std::bind(&TaskManagerNode::on_nav_result, this, _1));

#ifdef USE_NFC_TRIGGER
  sub_nfc_trigger_ = create_subscription<std_msgs::msg::String>(
    "/robot/nfc_trigger", 10,
    std::bind(&TaskManagerNode::on_nfc_trigger, this, _1));
  RCLCPP_INFO(get_logger(), "NFC trigger mode: ENABLED (timer disabled)");
#else
  RCLCPP_INFO(get_logger(),
    "Loading/Unloading timer: %.1fs / %.1fs",
    LOADING_TIMER_SEC, UNLOADING_TIMER_SEC);
#endif

  // ── Publishers ─────────────────────────────────────────
  pub_nav_goal_      = create_publisher<std_msgs::msg::String>("/robot/navigation_goal",     10);
  pub_task_state_    = create_publisher<std_msgs::msg::String>("/robot/task_state",          10);
  pub_location_code_ = create_publisher<std_msgs::msg::String>("/robot/location_code",       10);
  pub_task_complete_ = create_publisher<std_msgs::msg::String>("/robot/task_complete_event", 10);
  pub_error_event_   = create_publisher<std_msgs::msg::String>("/robot/error_event",         10);
  pub_cmd_vel_        = create_publisher<geometry_msgs::msg::Twist>("/cmd_vel",              10);

  // Initial state broadcast so mqtt_bridge knows we're IDLE on startup
  publish_task_state();
  RCLCPP_INFO(get_logger(), "TaskManagerNode ready — state: IDLE");
  RCLCPP_INFO(get_logger(),
    "Navigation timeout: %.1fs", navigation_timeout_sec_);
}

// ============================================================
// Subscriber callbacks
// ============================================================

void TaskManagerNode::on_task_assign(const std_msgs::msg::String::SharedPtr msg)
{
  std::lock_guard<std::recursive_mutex> lock(state_mutex_);

  if (state_ != TaskState::IDLE) {
    if (state_ == TaskState::EMERGENCY) {
      RCLCPP_WARN(get_logger(), "Received task_assign while EMERGENCY — ignoring");
      return;
    }
    RCLCPP_WARN(get_logger(),
      "Received task_assign but state is %s — ignoring",
      state_to_string(state_).c_str());
    return;
  }

  // ── Parse JSON ──────────────────────────────────────────
  ActiveTask task;
  try {
    json j = json::parse(msg->data);
    if (j.contains("robot_id") && !j["robot_id"].is_null()) {
      const auto incoming_robot_id = j["robot_id"].get<std::string>();
      if (incoming_robot_id != robot_id_) {
        RCLCPP_WARN(get_logger(),
          "task_assign robot_id mismatch — got %s expected %s; ignored",
          incoming_robot_id.c_str(), robot_id_.c_str());
        return;
      }
    }
    if (!j.contains("task_id") || !j["task_id"].is_number_integer()) {
      RCLCPP_ERROR(get_logger(), "task_assign task_id must be an integer");
      publish_error("task_assign task_id must be an integer");
      return;
    }
    task.task_id    = j.at("task_id").get<int>();
    task.task_type  = j.at("task_type").get<std::string>();
    task.priority   = j.value("priority", 0);

    // origin / destination may be null
    task.origin = (!j.contains("origin") || j["origin"].is_null()) ?
      "" : j["origin"].get<std::string>();
    task.destination = (!j.contains("destination") || j["destination"].is_null()) ?
      "" : j["destination"].get<std::string>();

  } catch (const json::exception & e) {
    RCLCPP_ERROR(get_logger(), "task_assign JSON parse error: %s", e.what());
    publish_error("task_assign parse error: " + std::string(e.what()));
    return;
  }

  if (task.task_id <= 0) {
    RCLCPP_ERROR(get_logger(), "task_assign: invalid task_id=%d", task.task_id);
    publish_error("task_assign: invalid task_id");
    return;
  }
  if (task.task_type.empty()) {
    RCLCPP_ERROR(get_logger(), "task_assign: empty task_type");
    publish_error("task_assign: empty task_type");
    return;
  }
  if (!is_supported_task_type(task.task_type)) {
    RCLCPP_ERROR(get_logger(), "task_assign: unsupported task_type=%s", task.task_type.c_str());
    publish_error("task_assign: unsupported task_type");
    return;
  }

  // ── Validate destination ────────────────────────────────
  if (task.destination.empty()) {
    RCLCPP_ERROR(get_logger(), "task_assign: destination is null — cannot execute");
    publish_error("task_assign: null destination");
    return;
  }
  if (!location_mapper_.resolve(task.destination)) {
    RCLCPP_ERROR(get_logger(),
      "task_assign: unknown destination code '%s'", task.destination.c_str());
    publish_error("unknown destination: " + task.destination);
    return;
  }
  if (!task.origin.empty() && !location_mapper_.resolve(task.origin)) {
    RCLCPP_ERROR(get_logger(),
      "task_assign: unknown origin code '%s'", task.origin.c_str());
    publish_error("unknown origin: " + task.origin);
    return;
  }

  active_task_ = task;
  RCLCPP_INFO(get_logger(),
    "Task accepted — id=%d type=%s origin='%s' dest='%s' priority=%d",
    task.task_id, task.task_type.c_str(),
    task.origin.c_str(), task.destination.c_str(), task.priority);

  transition_to(TaskState::TASK_RECEIVED);
}

// ────────────────────────────────────────────────────────────
void TaskManagerNode::on_task_cancel(const std_msgs::msg::String::SharedPtr msg)
{
  std::lock_guard<std::recursive_mutex> lock(state_mutex_);

  int cancel_task_id;
  try {
    json j = json::parse(msg->data);
    if (j.contains("robot_id") && !j["robot_id"].is_null()) {
      const auto incoming_robot_id = j["robot_id"].get<std::string>();
      if (incoming_robot_id != robot_id_) {
        RCLCPP_WARN(get_logger(),
          "task_cancel robot_id mismatch — got %s expected %s; ignored",
          incoming_robot_id.c_str(), robot_id_.c_str());
        return;
      }
    }
    if (!j.contains("task_id") || j["task_id"].is_null()) {
      RCLCPP_ERROR(get_logger(), "task_cancel JSON missing required task_id");
      return;
    }
    if (!j["task_id"].is_number_integer()) {
      RCLCPP_ERROR(get_logger(), "task_cancel task_id must be an integer");
      return;
    }
    cancel_task_id = j["task_id"].get<int>();
  } catch (const json::exception & e) {
    RCLCPP_ERROR(get_logger(), "task_cancel JSON parse error: %s", e.what());
    return;
  } catch (const std::exception & e) {
    RCLCPP_ERROR(get_logger(), "task_cancel invalid task_id: %s", e.what());
    return;
  }

  if (state_ == TaskState::IDLE) {
    RCLCPP_WARN(get_logger(),
      "task_cancel for task_id=%d received but already IDLE", cancel_task_id);
    return;
  }

  if (!active_task_) {
    RCLCPP_WARN(get_logger(),
      "task_cancel for task_id=%d received without active task — ignored",
      cancel_task_id);
    return;
  }

  if (cancel_task_id != active_task_->task_id) {
    RCLCPP_WARN(get_logger(),
      "task_cancel task_id mismatch — got %d active %d; ignored",
      cancel_task_id, active_task_->task_id);
    return;
  }

  RCLCPP_WARN(get_logger(),
    "Task CANCELLED — task_id=%d resetting to IDLE", active_task_->task_id);

  // Cancel any running timers
  if (loading_timer_)   { loading_timer_->cancel();   loading_timer_.reset(); }
  if (unloading_timer_) { unloading_timer_->cancel(); unloading_timer_.reset(); }
  stop_navigation_timeout();

  publish_nav_cancel(active_task_->task_id);
  publish_stop_command();

  reset_to_idle();
}

// ────────────────────────────────────────────────────────────
void TaskManagerNode::on_task_finish(const std_msgs::msg::String::SharedPtr msg)
{
  std::lock_guard<std::recursive_mutex> lock(state_mutex_);

  int finish_task_id;
  try {
    json j = json::parse(msg->data);
    if (j.contains("robot_id") && !j["robot_id"].is_null()) {
      const auto incoming_robot_id = j["robot_id"].get<std::string>();
      if (incoming_robot_id != robot_id_) {
        RCLCPP_WARN(get_logger(),
          "task_finish robot_id mismatch — got %s expected %s; ignored",
          incoming_robot_id.c_str(), robot_id_.c_str());
        return;
      }
    }
    if (!j.contains("task_id") || !j["task_id"].is_number_integer()) {
      RCLCPP_ERROR(get_logger(), "task_finish task_id must be an integer");
      return;
    }
    finish_task_id = j["task_id"].get<int>();
  } catch (const json::exception & e) {
    RCLCPP_ERROR(get_logger(), "task_finish JSON parse error: %s", e.what());
    return;
  }

  if (!active_task_) {
    RCLCPP_WARN(get_logger(),
      "task_finish for task_id=%d received without active task — ignored",
      finish_task_id);
    return;
  }

  if (finish_task_id != active_task_->task_id) {
    RCLCPP_WARN(get_logger(),
      "task_finish task_id mismatch — got %d active %d; ignored",
      finish_task_id, active_task_->task_id);
    return;
  }

  if (!is_patient_task()) {
    RCLCPP_WARN(get_logger(),
      "task_finish received for non-patient task_type=%s — ignored",
      active_task_->task_type.c_str());
    return;
  }

  RCLCPP_INFO(get_logger(),
    "Patient task_finish accepted — task_id=%d resetting to IDLE",
    active_task_->task_id);
  reset_to_idle();
}

// ────────────────────────────────────────────────────────────
void TaskManagerNode::on_emergency_call(const std_msgs::msg::String::SharedPtr msg)
{
  std::lock_guard<std::recursive_mutex> lock(state_mutex_);

  std::string command;
  try {
    json j = json::parse(msg->data);
    if (j.contains("robot_id") && !j["robot_id"].is_null()) {
      const auto incoming_robot_id = j["robot_id"].get<std::string>();
      if (incoming_robot_id != robot_id_) {
        RCLCPP_WARN(get_logger(),
          "emergency_call robot_id mismatch — got %s expected %s; ignored",
          incoming_robot_id.c_str(), robot_id_.c_str());
        return;
      }
    }
    command = j.at("command").get<std::string>();
  } catch (const json::exception & e) {
    RCLCPP_ERROR(get_logger(), "emergency_call JSON parse error: %s", e.what());
    return;
  }

  if (command == "STOP") {
    enter_emergency();
    return;
  }

  if (command == "RELEASE") {
    if (state_ != TaskState::EMERGENCY) {
      RCLCPP_WARN(get_logger(),
        "Emergency RELEASE received while state is %s — returning to IDLE",
        state_to_string(state_).c_str());
    }
    reset_to_idle();
    return;
  }

  RCLCPP_ERROR(get_logger(), "emergency_call unknown command: %s", command.c_str());
}

// ────────────────────────────────────────────────────────────
void TaskManagerNode::on_nav_result(const std_msgs::msg::String::SharedPtr msg)
{
  std::lock_guard<std::recursive_mutex> lock(state_mutex_);

  if (state_ == TaskState::EMERGENCY) {
    RCLCPP_WARN(get_logger(), "navigation_result received while EMERGENCY — ignored");
    return;
  }

  std::string result;
  std::string phase;
  std::optional<int> result_task_id;
  try {
    json j = json::parse(msg->data);
    result = j.at("result").get<std::string>();
    if (j.contains("phase") && !j["phase"].is_null()) {
      phase = j["phase"].get<std::string>();
    }
    if (j.contains("task_id") && !j["task_id"].is_null()) {
      if (!j["task_id"].is_number_integer()) {
        RCLCPP_WARN(get_logger(), "navigation_result task_id must be an integer — ignored");
        return;
      }
      result_task_id = j["task_id"].get<int>();
    }
  } catch (const json::exception & e) {
    RCLCPP_ERROR(get_logger(), "navigation_result JSON parse error: %s", e.what());
    if (state_ == TaskState::MOVING_TO_ORIGIN ||
      state_ == TaskState::MOVING_TO_DESTINATION)
    {
      enter_error("navigation_result parse error: " + std::string(e.what()));
    }
    return;
  }

  RCLCPP_INFO(get_logger(), "nav_result: result=%s task_id=%s phase=%s (state: %s)",
    result.c_str(),
    result_task_id ? std::to_string(*result_task_id).c_str() : "null",
    phase.empty() ? "null" : phase.c_str(),
    state_to_string(state_).c_str());

  if (!active_task_) {
    RCLCPP_WARN(get_logger(), "navigation_result received without active task — ignored");
    return;
  }

  if (!result_task_id || *result_task_id != active_task_->task_id) {
    RCLCPP_WARN(get_logger(),
      "Stale navigation_result ignored: got task_id=%s active task_id=%d",
      result_task_id ? std::to_string(*result_task_id).c_str() : "null",
      active_task_->task_id);
    return;
  }

  const bool moving_to_origin = state_ == TaskState::MOVING_TO_ORIGIN;
  const bool moving_to_destination = state_ == TaskState::MOVING_TO_DESTINATION;
  const std::string expected_phase = moving_to_origin ? "ORIGIN" :
    (moving_to_destination ? "DESTINATION" : "");

  if (result == "CANCELLED") {
    // Already handled by on_task_cancel; just log
    RCLCPP_INFO(get_logger(), "Navigation was cancelled");
    return;
  }

  if (!moving_to_origin && !moving_to_destination) {
    RCLCPP_WARN(get_logger(),
      "navigation_result received in non-moving state %s — ignored",
      state_to_string(state_).c_str());
    return;
  }

  if (phase != expected_phase) {
    RCLCPP_WARN(get_logger(),
      "Stale navigation_result ignored: phase=%s expected=%s",
      phase.c_str(), expected_phase.c_str());
    return;
  }

  stop_navigation_timeout();

  if (result == "FAILURE") {
    RCLCPP_ERROR(get_logger(), "Navigation FAILED in state: %s",
      state_to_string(state_).c_str());
    enter_error("Navigation failed in state: " + state_to_string(state_));
    return;
  }

  if (result != "SUCCESS") {
    RCLCPP_WARN(get_logger(), "Unknown nav_result value: %s", result.c_str());
    return;
  }

  // ── Handle SUCCESS per current state ────────────────────
  switch (state_) {
    case TaskState::MOVING_TO_ORIGIN:
      transition_to(TaskState::AT_ORIGIN);
      break;

    case TaskState::MOVING_TO_DESTINATION:
      transition_to(TaskState::AT_DESTINATION);
      break;

    default:
      RCLCPP_WARN(get_logger(),
        "nav_result SUCCESS received in unexpected state: %s",
        state_to_string(state_).c_str());
      break;
  }
}

// ── NFC hook (compiled in only when USE_NFC_TRIGGER defined) ─
#ifdef USE_NFC_TRIGGER
void TaskManagerNode::on_nfc_trigger(const std_msgs::msg::String::SharedPtr msg)
{
  std::lock_guard<std::recursive_mutex> lock(state_mutex_);
  (void)msg;
  RCLCPP_INFO(get_logger(), "NFC trigger received in state: %s",
    state_to_string(state_).c_str());

  if (state_ == TaskState::LOADING) {
    transition_to(TaskState::MOVING_TO_DESTINATION);
  } else if (state_ == TaskState::UNLOADING) {
    if (is_patient_task()) {
      RCLCPP_INFO(get_logger(), "Patient task unloading waits for server task_finish");
      return;
    }
    transition_to(TaskState::TASK_COMPLETE);
  } else {
    RCLCPP_WARN(get_logger(), "NFC trigger in unexpected state — ignored");
  }
}
#endif

// ============================================================
// Timer callbacks
// ============================================================

void TaskManagerNode::on_loading_timer()
{
  std::lock_guard<std::recursive_mutex> lock(state_mutex_);

  if (loading_timer_) {
    loading_timer_->cancel();
    loading_timer_.reset();
  }
  if (state_ != TaskState::LOADING || !active_task_) {
    RCLCPP_WARN(get_logger(), "Loading timer fired outside LOADING — ignored");
    return;
  }
  RCLCPP_INFO(get_logger(), "Loading timer expired — moving to destination");
  transition_to(TaskState::MOVING_TO_DESTINATION);
}

void TaskManagerNode::on_unloading_timer()
{
  std::lock_guard<std::recursive_mutex> lock(state_mutex_);

  if (unloading_timer_) {
    unloading_timer_->cancel();
    unloading_timer_.reset();
  }
  if (state_ != TaskState::UNLOADING || !active_task_) {
    RCLCPP_WARN(get_logger(), "Unloading timer fired outside UNLOADING — ignored");
    return;
  }
  if (is_patient_task()) {
    RCLCPP_INFO(get_logger(), "Patient task unloading waits for server task_finish");
    return;
  }
  RCLCPP_INFO(get_logger(), "Unloading timer expired — task complete");
  transition_to(TaskState::TASK_COMPLETE);
}

void TaskManagerNode::on_navigation_timeout()
{
  std::lock_guard<std::recursive_mutex> lock(state_mutex_);

  stop_navigation_timeout();
  if (state_ != TaskState::MOVING_TO_ORIGIN &&
    state_ != TaskState::MOVING_TO_DESTINATION)
  {
    RCLCPP_WARN(get_logger(), "Navigation timeout fired outside moving state — ignored");
    return;
  }

  RCLCPP_ERROR(get_logger(), "Navigation timeout in state: %s",
    state_to_string(state_).c_str());
  enter_error("Navigation timeout in state: " + state_to_string(state_));
}

// ============================================================
// State transition — the heart of the state machine
// ============================================================
void TaskManagerNode::transition_to(TaskState next)
{
  std::lock_guard<std::recursive_mutex> lock(state_mutex_);

  if (next != TaskState::IDLE && next != TaskState::ERROR &&
    next != TaskState::EMERGENCY && !active_task_)
  {
    RCLCPP_ERROR(get_logger(), "Invalid transition to %s without active task",
      state_to_string(next).c_str());
    enter_error("invalid state transition without active task");
    return;
  }

  RCLCPP_INFO(get_logger(), "State: %s → %s",
    state_to_string(state_).c_str(), state_to_string(next).c_str());

  state_ = next;
  publish_task_state();

  switch (state_) {

    // ── Immediately decide: has origin? ──────────────────
    case TaskState::TASK_RECEIVED:
      if (!is_patient_task() && !active_task_->origin.empty()) {
        transition_to(TaskState::MOVING_TO_ORIGIN);
      } else {
        RCLCPP_INFO(get_logger(),
          "Skipping origin — moving directly to destination");
        transition_to(TaskState::MOVING_TO_DESTINATION);
      }
      break;

    case TaskState::MOVING_TO_ORIGIN:
      send_nav_goal(active_task_->origin, "ORIGIN");
      if (state_ == TaskState::MOVING_TO_ORIGIN) {
        start_navigation_timeout();
      }
      break;

    case TaskState::AT_ORIGIN:
      publish_location_code(active_task_->origin);
#ifndef USE_NFC_TRIGGER
      RCLCPP_INFO(get_logger(),
        "AT_ORIGIN — starting loading timer (%.1fs)", LOADING_TIMER_SEC);
      loading_timer_ = create_wall_timer(
        std::chrono::duration<double>(LOADING_TIMER_SEC),
        std::bind(&TaskManagerNode::on_loading_timer, this));
#else
      RCLCPP_INFO(get_logger(), "AT_ORIGIN — waiting for NFC trigger");
#endif
      transition_to(TaskState::LOADING);
      break;

    case TaskState::LOADING:
      // Timer already started in AT_ORIGIN. Nothing more to do here.
      // (or waiting for NFC trigger if USE_NFC_TRIGGER defined)
      break;

    case TaskState::MOVING_TO_DESTINATION:
      send_nav_goal(active_task_->destination, "DESTINATION");
      if (state_ == TaskState::MOVING_TO_DESTINATION) {
        start_navigation_timeout();
      }
      break;

    case TaskState::AT_DESTINATION:
      publish_location_code(active_task_->destination);
#ifndef USE_NFC_TRIGGER
      if (is_patient_task()) {
        RCLCPP_INFO(get_logger(),
          "AT_DESTINATION — patient task waits for server task_finish after unload opens");
      } else {
        RCLCPP_INFO(get_logger(),
          "AT_DESTINATION — starting unloading timer (%.1fs)", UNLOADING_TIMER_SEC);
        unloading_timer_ = create_wall_timer(
          std::chrono::duration<double>(UNLOADING_TIMER_SEC),
          std::bind(&TaskManagerNode::on_unloading_timer, this));
      }
#else
      if (is_patient_task()) {
        RCLCPP_INFO(get_logger(),
          "AT_DESTINATION — patient task waits for server task_finish after unload opens");
      } else {
        RCLCPP_INFO(get_logger(), "AT_DESTINATION — waiting for NFC trigger");
      }
#endif
      transition_to(TaskState::UNLOADING);
      break;

    case TaskState::UNLOADING:
      // Timer already started in AT_DESTINATION. Nothing more to do here.
      break;

    case TaskState::TASK_COMPLETE:
      if (is_patient_task()) {
        RCLCPP_INFO(get_logger(),
          "Patient task %d complete — task_complete_event suppressed",
          active_task_->task_id);
      } else {
        publish_task_complete();
      }
      RCLCPP_INFO(get_logger(), "Task %d complete — resetting to IDLE",
        active_task_->task_id);
      reset_to_idle();
      break;

    case TaskState::ERROR:
      stop_navigation_timeout();
      reset_to_idle();
      break;

    case TaskState::EMERGENCY:
      break;

    case TaskState::IDLE:
      // Nothing extra — publish_task_state() already called above
      break;

    default:
      break;
  }
}

// ============================================================
// Publish helpers
// ============================================================

void TaskManagerNode::send_nav_goal(const std::string & code, const std::string & phase)
{
  auto pose = location_mapper_.resolve(code);
  if (!pose) {
    // Should not happen — validated at task_assign time
    RCLCPP_ERROR(get_logger(), "send_nav_goal: cannot resolve '%s'", code.c_str());
    enter_error("cannot resolve location: " + code);
    return;
  }

  json j;
  j["task_id"]       = active_task_ ? active_task_->task_id : 0;
  j["phase"]         = phase;
  j["location_code"] = code;
  j["x"]             = pose->x;
  j["y"]             = pose->y;
  j["yaw"]           = pose->yaw;

  auto out = std_msgs::msg::String{};
  out.data = j.dump();
  pub_nav_goal_->publish(out);

  RCLCPP_INFO(get_logger(),
    "Nav goal sent: code='%s' x=%.2f y=%.2f yaw=%.2f",
    code.c_str(), pose->x, pose->y, pose->yaw);
}

void TaskManagerNode::publish_nav_cancel(std::optional<int> task_id)
{
  json cancel_json;
  cancel_json["cancel"] = true;
  if (task_id) {
    cancel_json["task_id"] = *task_id;
  } else {
    cancel_json["task_id"] = nullptr;
  }

  auto cancel_msg = std_msgs::msg::String{};
  cancel_msg.data = cancel_json.dump();
  pub_nav_goal_->publish(cancel_msg);
  RCLCPP_WARN(get_logger(), "Navigation cancel published");
}

void TaskManagerNode::publish_stop_command()
{
  auto stop_msg = geometry_msgs::msg::Twist{};
  pub_cmd_vel_->publish(stop_msg);
  RCLCPP_WARN(get_logger(), "Immediate stop command published to /cmd_vel");
}

void TaskManagerNode::start_navigation_timeout()
{
  stop_navigation_timeout();
  navigation_timeout_timer_ = create_wall_timer(
    std::chrono::duration<double>(navigation_timeout_sec_),
    std::bind(&TaskManagerNode::on_navigation_timeout, this));
  RCLCPP_INFO(get_logger(),
    "Navigation timeout timer started (%.1fs)", navigation_timeout_sec_);
}

void TaskManagerNode::stop_navigation_timeout()
{
  if (navigation_timeout_timer_) {
    navigation_timeout_timer_->cancel();
    navigation_timeout_timer_.reset();
  }
}

void TaskManagerNode::publish_task_state()
{
  json j;
  j["state"] = state_to_robot_state(state_);
  j["internal_state"] = state_to_string(state_);
  if (active_task_) {
    j["task_id"] = active_task_->task_id;
  } else {
    j["task_id"] = nullptr;
  }

  auto msg = std_msgs::msg::String{};
  msg.data = j.dump();
  pub_task_state_->publish(msg);
}

void TaskManagerNode::publish_location_code(const std::string & code)
{
  json j;
  j["location_code"] = code;
  if (active_task_) {
    j["task_id"] = active_task_->task_id;
  } else {
    j["task_id"] = nullptr;
  }

  auto msg = std_msgs::msg::String{};
  msg.data = j.dump();
  pub_location_code_->publish(msg);
  RCLCPP_INFO(get_logger(), "Published location_code: %s", code.c_str());
}

void TaskManagerNode::publish_task_complete()
{
  if (!active_task_) return;

  json j;
  j["task_id"] = active_task_->task_id;

  auto msg = std_msgs::msg::String{};
  msg.data = j.dump();
  pub_task_complete_->publish(msg);
  RCLCPP_INFO(get_logger(),
    "Published task_complete_event: task_id=%d", active_task_->task_id);
}

void TaskManagerNode::publish_error(const std::string & message)
{
  json j;
  j["error_message"] = message;
  if (active_task_) {
    j["task_id"] = active_task_->task_id;
  } else {
    j["task_id"] = nullptr;
  }

  auto msg = std_msgs::msg::String{};
  msg.data = j.dump();
  pub_error_event_->publish(msg);
  RCLCPP_ERROR(get_logger(), "Error published: %s", message.c_str());
}

void TaskManagerNode::enter_error(const std::string & message)
{
  publish_error(message);
  transition_to(TaskState::ERROR);
}

void TaskManagerNode::enter_emergency()
{
  std::optional<int> task_id;
  if (active_task_) {
    task_id = active_task_->task_id;
  }

  if (loading_timer_) {
    loading_timer_->cancel();
    loading_timer_.reset();
  }
  if (unloading_timer_) {
    unloading_timer_->cancel();
    unloading_timer_.reset();
  }
  stop_navigation_timeout();
  publish_nav_cancel(task_id);
  publish_stop_command();
  active_task_.reset();
  state_ = TaskState::EMERGENCY;
  publish_task_state();
  RCLCPP_ERROR(get_logger(), "Emergency STOP applied");
}

void TaskManagerNode::reset_to_idle()
{
  if (loading_timer_) {
    loading_timer_->cancel();
    loading_timer_.reset();
  }
  if (unloading_timer_) {
    unloading_timer_->cancel();
    unloading_timer_.reset();
  }
  stop_navigation_timeout();
  active_task_.reset();
  state_ = TaskState::IDLE;
  publish_task_state();
  RCLCPP_INFO(get_logger(), "Reset to IDLE");
}

bool TaskManagerNode::is_patient_task() const
{
  return active_task_ &&
    (active_task_->task_type == "patient_clothes_rental" ||
    active_task_->task_type == "patient_clothes_return");
}

bool TaskManagerNode::is_supported_task_type(const std::string & task_type) const
{
  return task_type == "specimen_delivery" ||
    task_type == "kit_delivery" ||
    task_type == "logistics_delivery" ||
    task_type == "clothes_refill" ||
    task_type == "used_clothes_collection" ||
    task_type == "patient_clothes_rental" ||
    task_type == "patient_clothes_return" ||
    task_type == "battery_low";
}

// ============================================================
// Utility
// ============================================================
std::string TaskManagerNode::state_to_string(TaskState s)
{
  switch (s) {
    case TaskState::IDLE:                   return "IDLE";
    case TaskState::TASK_RECEIVED:          return "TASK_RECEIVED";
    case TaskState::MOVING_TO_ORIGIN:       return "MOVING_TO_ORIGIN";
    case TaskState::AT_ORIGIN:              return "AT_ORIGIN";
    case TaskState::LOADING:                return "LOADING";
    case TaskState::MOVING_TO_DESTINATION:  return "MOVING_TO_DESTINATION";
    case TaskState::AT_DESTINATION:         return "AT_DESTINATION";
    case TaskState::UNLOADING:              return "UNLOADING";
    case TaskState::TASK_COMPLETE:          return "TASK_COMPLETE";
    case TaskState::ERROR:                  return "ERROR";
    case TaskState::EMERGENCY:              return "EMERGENCY";
    default:                                return "UNKNOWN";
  }
}

std::string TaskManagerNode::state_to_robot_state(TaskState s) const
{
  switch (s) {
    case TaskState::IDLE:
      return "IDLE";
    case TaskState::TASK_RECEIVED:
    case TaskState::MOVING_TO_ORIGIN:
    case TaskState::MOVING_TO_DESTINATION:
      return "MOVING";
    case TaskState::AT_ORIGIN:
    case TaskState::AT_DESTINATION:
      return "ARRIVED";
    case TaskState::LOADING:
      return "WAIT_NFC";
    case TaskState::UNLOADING:
      return is_patient_task() ? "DELIVERY_OPEN_PAT" : "DELIVERY_OPEN_NUR";
    case TaskState::TASK_COMPLETE:
      return "COMPLETE";
    case TaskState::ERROR:
      return "ERROR";
    case TaskState::EMERGENCY:
      return "EMERGENCY";
    default:
      return "ERROR";
  }
}

}  // namespace robot_task
