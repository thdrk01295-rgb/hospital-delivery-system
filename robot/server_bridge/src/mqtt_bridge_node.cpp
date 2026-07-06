#include "server_bridge/mqtt_bridge_node.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

using json = nlohmann::json;

namespace server_bridge
{

namespace topic
{
constexpr char TASK_ASSIGN[]    = "server/task_assign";
constexpr char TASK_CANCEL[]    = "server/task_cancel";
constexpr char TASK_FINISH[]    = "server/task_finish";
constexpr char LOCK_COMMAND[]   = "server/lock_command";
constexpr char EMERGENCY_CALL[] = "server/emergency_call";
constexpr char STATUS[]         = "robot/status";
constexpr char BATTERY[]        = "robot/battery";
constexpr char ERROR[]          = "robot/error";
constexpr char TASK_COMPLETE[]  = "robot/task_complete";
constexpr char LOCK_STATUS[]    = "robot/lock_status";
}  // namespace topic

namespace
{

std::string payloadPreview(const std::string & payload)
{
  constexpr size_t kMaxPreview = 160;
  if (payload.size() <= kMaxPreview) {
    return payload;
  }
  return payload.substr(0, kMaxPreview) + "...";
}

bool parseJsonObject(
  const rclcpp::Logger & logger,
  const std::string & source,
  const std::string & payload,
  json & out)
{
  try {
    out = json::parse(payload);
  } catch (const json::exception & e) {
    RCLCPP_WARN(
      logger,
      "%s payload JSON parse failed: %s payload='%s'",
      source.c_str(), e.what(), payloadPreview(payload).c_str());
    return false;
  }

  if (!out.is_object()) {
    RCLCPP_WARN(
      logger,
      "%s payload must be a JSON object: payload='%s'",
      source.c_str(), payloadPreview(payload).c_str());
    return false;
  }
  return true;
}

bool isInteger(const json & value)
{
  return value.is_number_integer() || value.is_number_unsigned();
}

bool copyIntegerTaskIdIfPresent(
  const rclcpp::Logger & logger,
  const std::string & source,
  const json & src,
  json & dst)
{
  if (!src.contains("task_id") || src["task_id"].is_null()) {
    return true;
  }

  if (!isInteger(src["task_id"])) {
    RCLCPP_ERROR(logger, "%s payload task_id must be an integer", source.c_str());
    return false;
  }

  dst["task_id"] = src["task_id"];
  return true;
}

bool requireIntegerField(
  const rclcpp::Logger & logger,
  const std::string & source,
  const json & payload,
  const char * field)
{
  if (!payload.contains(field) || payload[field].is_null()) {
    RCLCPP_ERROR(logger, "%s payload missing required %s", source.c_str(), field);
    return false;
  }
  if (!isInteger(payload[field])) {
    RCLCPP_ERROR(logger, "%s payload %s must be an integer", source.c_str(), field);
    return false;
  }
  return true;
}

bool requireStringField(
  const rclcpp::Logger & logger,
  const std::string & source,
  const json & payload,
  const char * field)
{
  if (!payload.contains(field) || payload[field].is_null()) {
    RCLCPP_ERROR(logger, "%s payload missing required %s", source.c_str(), field);
    return false;
  }
  if (!payload[field].is_string()) {
    RCLCPP_ERROR(logger, "%s payload %s must be a string", source.c_str(), field);
    return false;
  }
  return true;
}

bool requireOptionalStringField(
  const rclcpp::Logger & logger,
  const std::string & source,
  const json & payload,
  const char * field)
{
  if (!payload.contains(field) || payload[field].is_null()) {
    return true;
  }
  if (!payload[field].is_string()) {
    RCLCPP_ERROR(logger, "%s payload %s must be a string", source.c_str(), field);
    return false;
  }
  return true;
}

bool requireBooleanField(
  const rclcpp::Logger & logger,
  const std::string & source,
  const json & payload,
  const char * field)
{
  if (!payload.contains(field) || payload[field].is_null()) {
    RCLCPP_ERROR(logger, "%s payload missing required %s", source.c_str(), field);
    return false;
  }
  if (!payload[field].is_boolean()) {
    RCLCPP_ERROR(logger, "%s payload %s must be a boolean", source.c_str(), field);
    return false;
  }
  return true;
}

bool requireNullableStringField(
  const rclcpp::Logger & logger,
  const std::string & source,
  const json & payload,
  const char * field)
{
  if (!payload.contains(field)) {
    RCLCPP_ERROR(logger, "%s payload missing required %s", source.c_str(), field);
    return false;
  }
  if (!payload[field].is_null() && !payload[field].is_string()) {
    RCLCPP_ERROR(logger, "%s payload %s must be a string or null", source.c_str(), field);
    return false;
  }
  return true;
}

bool requireNonEmptyStringField(
  const rclcpp::Logger & logger,
  const std::string & source,
  const json & payload,
  const char * field)
{
  if (!requireStringField(logger, source, payload, field)) {
    return false;
  }
  if (payload[field].get<std::string>().empty()) {
    RCLCPP_ERROR(logger, "%s payload %s must not be empty", source.c_str(), field);
    return false;
  }
  return true;
}

bool matchesRobotId(
  const rclcpp::Logger & logger,
  const std::string & topic_name,
  const json & payload,
  const std::string & robot_id)
{
  if (!payload.contains("robot_id") || payload["robot_id"].is_null()) {
    RCLCPP_ERROR(logger, "%s payload missing required robot_id", topic_name.c_str());
    return false;
  }

  if (!payload["robot_id"].is_string()) {
    RCLCPP_ERROR(logger, "%s payload robot_id must be a string", topic_name.c_str());
    return false;
  }

  const auto incoming_robot_id = payload["robot_id"].get<std::string>();
  if (incoming_robot_id != robot_id) {
    RCLCPP_WARN(
      logger,
      "%s payload ignored for robot_id='%s' expected='%s'",
      topic_name.c_str(), incoming_robot_id.c_str(), robot_id.c_str());
    return false;
  }
  return true;
}

bool validateTaskFinishPayload(
  const rclcpp::Logger & logger,
  const std::string & source,
  const json & payload)
{
  if (!requireIntegerField(logger, source, payload, "task_id") ||
    !requireOptionalStringField(logger, source, payload, "source"))
  {
    return false;
  }

  const bool has_v6_field =
    payload.contains("stop_type") ||
    payload.contains("is_final") ||
    payload.contains("next_action");
  if (!has_v6_field) {
    return true;
  }

  return requireStringField(logger, source, payload, "stop_type") &&
    requireBooleanField(logger, source, payload, "is_final") &&
    requireStringField(logger, source, payload, "next_action");
}

}  // namespace

MqttBridgeNode::MqttBridgeNode(const rclcpp::NodeOptions & options)
: Node("mqtt_bridge_node", options)
{
  declareParameters();
  initMqtt();
  initRos();
  RCLCPP_INFO(get_logger(), "mqtt_bridge_node started [%s]", broker_uri_.c_str());
}

MqttBridgeNode::~MqttBridgeNode()
{
  if (mqtt_client_) {mqtt_client_->disconnect();}
}

void MqttBridgeNode::declareParameters()
{
  declare_parameter("robot_id",   std::string("AMR-001"));
  declare_parameter("broker_uri", std::string("tcp://localhost:1883"));
  declare_parameter("qos",        1);

  robot_id_   = get_parameter("robot_id").as_string();
  broker_uri_ = get_parameter("broker_uri").as_string();
  qos_        = get_parameter("qos").as_int();
}

void MqttBridgeNode::initMqtt()
{
  mqtt_client_ = std::make_unique<MqttClient>(
    broker_uri_, "ros2_bridge_" + robot_id_, qos_);

  mqtt_client_->setMessageCallback(
    [this](const std::string & t, const std::string & p) {onMqttMessage(t, p);});

  mqtt_client_->setConnectionCallback(
    [this](bool ok, const std::string & detail) {
      if (ok) {
        RCLCPP_INFO(get_logger(), "MQTT connected; subscriptions refreshed");
      } else {
        RCLCPP_WARN(
          get_logger(),
          "MQTT connection lost: %s; automatic reconnect is enabled",
          detail.empty() ? "unknown" : detail.c_str());
      }
    });

  mqtt_client_->subscribe(topic::TASK_ASSIGN);
  mqtt_client_->subscribe(topic::TASK_CANCEL);
  mqtt_client_->subscribe(topic::TASK_FINISH);
  mqtt_client_->subscribe(topic::LOCK_COMMAND);
  mqtt_client_->subscribe(topic::EMERGENCY_CALL);

  if (!mqtt_client_->connect(10)) {
    RCLCPP_WARN(get_logger(), "MQTT broker unreachable — will retry on reconnect");
  }
}

void MqttBridgeNode::initRos()
{
  auto qos = rclcpp::QoS(10);

  pub_task_assign_    = create_publisher<std_msgs::msg::String>("/server/task_assign",    qos);
  pub_task_cancel_    = create_publisher<std_msgs::msg::String>("/server/task_cancel",    qos);
  pub_task_finish_    = create_publisher<std_msgs::msg::String>("/server/task_finish",    qos);
  pub_emergency_call_ = create_publisher<std_msgs::msg::String>("/server/emergency_call", qos);
  pub_lock_command_   = create_publisher<std_msgs::msg::String>("/server/lock_command",   qos);

  sub_task_state_ = create_subscription<std_msgs::msg::String>(
    "/robot/task_state", qos,
    [this](const std_msgs::msg::String::SharedPtr m) {onTaskState(m);});

  sub_battery_state_ = create_subscription<std_msgs::msg::Float32>(
    "/robot/battery_state", qos,
    [this](const std_msgs::msg::Float32::SharedPtr m) {onBatteryState(m);});

  sub_error_event_ = create_subscription<std_msgs::msg::String>(
    "/robot/error_event", qos,
    [this](const std_msgs::msg::String::SharedPtr m) {onErrorEvent(m);});

  sub_task_complete_ = create_subscription<std_msgs::msg::String>(
    "/robot/task_complete_event", qos,
    [this](const std_msgs::msg::String::SharedPtr m) {onTaskCompleteEvent(m);});

  sub_lock_status_ = create_subscription<std_msgs::msg::String>(
    "/robot/lock_status", qos,
    [this](const std_msgs::msg::String::SharedPtr m) {onLockStatus(m);});
}

void MqttBridgeNode::onMqttMessage(const std::string & t, const std::string & payload)
{
  json parsed;
  if (!parseJsonObject(get_logger(), t, payload, parsed)) {
    return;
  }

  if (!matchesRobotId(get_logger(), t, parsed, robot_id_)) {
    return;
  }

  if (t == topic::TASK_ASSIGN) {
    if (!requireIntegerField(get_logger(), t, parsed, "task_id") ||
      !requireStringField(get_logger(), t, parsed, "task_type") ||
      !requireNullableStringField(get_logger(), t, parsed, "origin") ||
      !requireNonEmptyStringField(get_logger(), t, parsed, "destination") ||
      !requireIntegerField(get_logger(), t, parsed, "priority"))
    {
      return;
    }
  } else if (t == topic::TASK_CANCEL) {
    if (!requireIntegerField(get_logger(), t, parsed, "task_id")) {
      return;
    }
  } else if (t == topic::TASK_FINISH) {
    if (!validateTaskFinishPayload(get_logger(), t, parsed)) {
      return;
    }
  } else if (t == topic::LOCK_COMMAND) {
    if (!requireStringField(get_logger(), t, parsed, "command")) {
      return;
    }
    if (parsed.contains("task_id") && !parsed["task_id"].is_null() &&
      !isInteger(parsed["task_id"]))
    {
      RCLCPP_ERROR(get_logger(), "%s payload task_id must be an integer", t.c_str());
      return;
    }
    const auto command = parsed["command"].get<std::string>();
    if (command != "UNLOCK" && command != "LOCK") {
      RCLCPP_ERROR(get_logger(), "%s payload command must be UNLOCK or LOCK", t.c_str());
      return;
    }
  } else if (t == topic::EMERGENCY_CALL) {
    if (!requireStringField(get_logger(), t, parsed, "command")) {
      return;
    }
    const auto command = parsed["command"].get<std::string>();
    if (command != "STOP" && command != "RELEASE") {
      RCLCPP_ERROR(get_logger(), "%s payload command must be STOP or RELEASE", t.c_str());
      return;
    }
  }

  auto msg = std_msgs::msg::String();
  msg.data = payload;

  if (t == topic::TASK_ASSIGN) {
    pub_task_assign_->publish(msg);
  } else if (t == topic::TASK_CANCEL) {
    pub_task_cancel_->publish(msg);
  } else if (t == topic::TASK_FINISH) {
    pub_task_finish_->publish(msg);
  } else if (t == topic::LOCK_COMMAND) {
    pub_lock_command_->publish(msg);
  } else if (t == topic::EMERGENCY_CALL) {
    pub_emergency_call_->publish(msg);
    RCLCPP_WARN(get_logger(), "Emergency call received");
  }
}

void MqttBridgeNode::onTaskState(const std_msgs::msg::String::SharedPtr msg)
{
  json in;
  if (!parseJsonObject(get_logger(), "/robot/task_state", msg->data, in)) {
    return;
  }
  if (!in.contains("state") || !in["state"].is_string()) {
    RCLCPP_ERROR(get_logger(), "/robot/task_state JSON missing string field 'state'");
    return;
  }
  const auto current_state = in["state"].get<std::string>();

  std::optional<int64_t> current_task_id;
  if (in.contains("task_id") && !in["task_id"].is_null()) {
    if (!isInteger(in["task_id"])) {
      RCLCPP_ERROR(get_logger(), "/robot/task_state payload task_id must be an integer");
      return;
    }
    current_task_id = in["task_id"].get<int64_t>();
  }

  if (has_last_status_ &&
    current_state == last_status_state_ &&
    current_task_id == last_status_task_id_)
  {
    RCLCPP_DEBUG(
      get_logger(),
      "Duplicate robot/status skipped: state=%s task_id=%s",
      current_state.c_str(),
      current_task_id ? std::to_string(*current_task_id).c_str() : "null");
    return;
  }

  json out;
  out["robot_id"] = robot_id_;
  out["state"] = in["state"];
  if (!copyIntegerTaskIdIfPresent(get_logger(), "/robot/task_state", in, out)) {
    return;
  }
  if (in.contains("timestamp") && !in["timestamp"].is_null()) {
    out["timestamp"] = in["timestamp"];
  }
  if (publishMqtt(topic::STATUS, out)) {
    has_last_status_ = true;
    last_status_state_ = current_state;
    last_status_task_id_ = current_task_id;
  }
}

void MqttBridgeNode::onBatteryState(const std_msgs::msg::Float32::SharedPtr msg)
{
  json out;
  out["robot_id"] = robot_id_;
  out["battery_percent"] = msg->data;
  publishMqtt(topic::BATTERY, out);
}

void MqttBridgeNode::onErrorEvent(const std_msgs::msg::String::SharedPtr msg)
{
  json in;
  if (!parseJsonObject(get_logger(), "/robot/error_event", msg->data, in)) {
    return;
  }

  json out;
  out["robot_id"] = robot_id_;
  if (in.contains("error") && in["error"].is_string()) {
    out["error_message"] = in["error"];
  } else if (in.contains("error_message") && in["error_message"].is_string()) {
    out["error_message"] = in["error_message"];
  } else {
    RCLCPP_ERROR(
      get_logger(),
      "/robot/error_event JSON missing string field 'error_message' or 'error'");
    return;
  }
  if (!copyIntegerTaskIdIfPresent(get_logger(), "/robot/error_event", in, out)) {
    return;
  }
  if (in.contains("timestamp") && !in["timestamp"].is_null()) {
    out["timestamp"] = in["timestamp"];
  }

  publishMqtt(topic::ERROR, out);
  RCLCPP_ERROR(
    get_logger(),
    "Error event bridged to MQTT: %s",
    out["error_message"].get<std::string>().c_str());
}

void MqttBridgeNode::onTaskCompleteEvent(const std_msgs::msg::String::SharedPtr msg)
{
  json in;
  if (!parseJsonObject(get_logger(), "/robot/task_complete_event", msg->data, in)) {
    return;
  }
  if (!requireIntegerField(get_logger(), "/robot/task_complete_event", in, "task_id")) {
    return;
  }

  json out;
  out["robot_id"] = robot_id_;
  out["task_id"] = in["task_id"];
  if (in.contains("timestamp") && !in["timestamp"].is_null()) {
    out["timestamp"] = in["timestamp"];
  }
  publishMqtt(topic::TASK_COMPLETE, out);
}

void MqttBridgeNode::onLockStatus(const std_msgs::msg::String::SharedPtr msg)
{
  json in;
  if (!parseJsonObject(get_logger(), "/robot/lock_status", msg->data, in)) {
    return;
  }
  if (!requireStringField(get_logger(), "/robot/lock_status", in, "command") ||
    !requireStringField(get_logger(), "/robot/lock_status", in, "status"))
  {
    return;
  }

  if (in.contains("robot_id") && !in["robot_id"].is_null()) {
    if (!in["robot_id"].is_string()) {
      RCLCPP_ERROR(get_logger(), "/robot/lock_status payload robot_id must be a string");
      return;
    }
    if (in["robot_id"].get<std::string>() != robot_id_) {
      RCLCPP_WARN(
        get_logger(),
        "/robot/lock_status ignored for robot_id='%s' expected='%s'",
        in["robot_id"].get<std::string>().c_str(), robot_id_.c_str());
      return;
    }
  }

  const auto command = in["command"].get<std::string>();
  if (command != "UNLOCK" && command != "LOCK") {
    RCLCPP_ERROR(get_logger(), "/robot/lock_status command must be UNLOCK or LOCK");
    return;
  }

  const auto status = in["status"].get<std::string>();
  if (status != "ACCEPTED" && status != "OPENED" && status != "LOCKED" && status != "FAILED") {
    RCLCPP_ERROR(
      get_logger(),
      "/robot/lock_status status must be ACCEPTED, OPENED, LOCKED, or FAILED");
    return;
  }

  json out;
  out["robot_id"] = robot_id_;
  if (!copyIntegerTaskIdIfPresent(get_logger(), "/robot/lock_status", in, out)) {
    return;
  }
  out["command"] = in["command"];
  out["status"] = in["status"];
  if (in.contains("message") && !in["message"].is_null()) {
    if (!in["message"].is_string()) {
      RCLCPP_ERROR(get_logger(), "/robot/lock_status message must be a string");
      return;
    }
    out["message"] = in["message"];
  }

  publishMqtt(topic::LOCK_STATUS, out);
}

bool MqttBridgeNode::publishMqtt(const std::string & topic_name, const json & payload)
{
  const auto dumped = payload.dump();
  if (mqtt_client_->publish(topic_name, dumped)) {
    return true;
  }

  RCLCPP_ERROR(
    get_logger(),
    "MQTT publish failed topic='%s' payload='%s'",
    topic_name.c_str(), payloadPreview(dumped).c_str());
  return false;
}

}  // namespace server_bridge
