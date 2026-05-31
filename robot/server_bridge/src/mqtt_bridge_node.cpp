#include "server_bridge/mqtt_bridge_node.hpp"
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

namespace server_bridge
{

namespace topic
{
constexpr char TASK_ASSIGN[]    = "server/task_assign";
constexpr char TASK_CANCEL[]    = "server/task_cancel";
constexpr char TASK_FINISH[]    = "server/task_finish";
constexpr char EMERGENCY_CALL[] = "server/emergency_call";
constexpr char STATUS[]         = "robot/status";
constexpr char BATTERY[]        = "robot/battery";
constexpr char ERROR[]          = "robot/error";
constexpr char TASK_COMPLETE[]  = "robot/task_complete";
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
      !requireStringField(get_logger(), t, parsed, "origin") ||
      !requireNonEmptyStringField(get_logger(), t, parsed, "destination") ||
      !requireIntegerField(get_logger(), t, parsed, "priority"))
    {
      return;
    }
  } else if (t == topic::TASK_CANCEL || t == topic::TASK_FINISH) {
    if (!requireIntegerField(get_logger(), t, parsed, "task_id")) {
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

  json out;
  out["robot_id"] = robot_id_;
  out["state"] = in["state"];
  if (!copyIntegerTaskIdIfPresent(get_logger(), "/robot/task_state", in, out)) {
    return;
  }
  if (in.contains("timestamp") && !in["timestamp"].is_null()) {
    out["timestamp"] = in["timestamp"];
  }
  publishMqtt(topic::STATUS, out);
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
