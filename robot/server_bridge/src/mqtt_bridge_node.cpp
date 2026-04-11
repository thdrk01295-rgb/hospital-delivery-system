#include "server_bridge/mqtt_bridge_node.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace server_bridge
{

namespace topic
{
constexpr char TASK_ASSIGN[]    = "server/task_assign";
constexpr char TASK_CANCEL[]    = "server/task_cancel";
constexpr char EMERGENCY_CALL[] = "server/emergency_call";
constexpr char STATUS[]         = "robot/status";
constexpr char LOCATION[]       = "robot/location";
constexpr char BATTERY[]        = "robot/battery";
constexpr char ERROR[]          = "robot/error";
constexpr char TASK_COMPLETE[]  = "robot/task_complete";
}  // namespace topic

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
    [this](bool ok) {
      if (ok) {
        RCLCPP_INFO(get_logger(), "MQTT connected");
      } else {
        RCLCPP_WARN(get_logger(), "MQTT connection lost");
      }
    });

  mqtt_client_->subscribe(topic::TASK_ASSIGN);
  mqtt_client_->subscribe(topic::TASK_CANCEL);
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
  pub_emergency_call_ = create_publisher<std_msgs::msg::String>("/server/emergency_call", qos);

  sub_task_state_ = create_subscription<std_msgs::msg::String>(
    "/robot/task_state", qos,
    [this](const std_msgs::msg::String::SharedPtr m) {onTaskState(m);});

  sub_location_code_ = create_subscription<std_msgs::msg::String>(
    "/robot/location_code", qos,
    [this](const std_msgs::msg::String::SharedPtr m) {onLocationCode(m);});

  sub_battery_state_ = create_subscription<std_msgs::msg::Float32>(
    "/robot/battery_state", qos,
    [this](const std_msgs::msg::Float32::SharedPtr m) {onBatteryState(m);});

  sub_error_event_ = create_subscription<std_msgs::msg::String>(
    "/robot/error_event", qos,
    [this](const std_msgs::msg::String::SharedPtr m) {onErrorEvent(m);});

  sub_task_complete_ = create_subscription<std_msgs::msg::Int32>(
    "/robot/task_complete_event", qos,
    [this](const std_msgs::msg::Int32::SharedPtr m) {onTaskCompleteEvent(m);});
}

void MqttBridgeNode::onMqttMessage(const std::string & t, const std::string & payload)
{
  auto msg = std_msgs::msg::String();
  msg.data = payload;

  if (t == topic::TASK_ASSIGN) {
    pub_task_assign_->publish(msg);
  } else if (t == topic::TASK_CANCEL) {
    pub_task_cancel_->publish(msg);
  } else if (t == topic::EMERGENCY_CALL) {
    pub_emergency_call_->publish(msg);
    RCLCPP_WARN(get_logger(), "Emergency call received");
  }
}

// timestamp는 ROS time(seconds)을 string으로 사용 — ISO8601 변환은 추후 개선 가능
void MqttBridgeNode::onTaskState(const std_msgs::msg::String::SharedPtr msg)
{
  json j;
  j["robot_id"]  = robot_id_;
  j["state"]     = msg->data;
  j["timestamp"] = std::to_string(now().seconds());
  mqtt_client_->publish(topic::STATUS, j.dump());
}

void MqttBridgeNode::onLocationCode(const std_msgs::msg::String::SharedPtr msg)
{
  json j;
  j["robot_id"]      = robot_id_;
  j["location_code"] = msg->data;
  j["timestamp"]     = std::to_string(now().seconds());
  mqtt_client_->publish(topic::LOCATION, j.dump());
}

void MqttBridgeNode::onBatteryState(const std_msgs::msg::Float32::SharedPtr msg)
{
  json j;
  j["robot_id"]        = robot_id_;
  j["battery_percent"] = msg->data;
  j["timestamp"]       = std::to_string(now().seconds());
  mqtt_client_->publish(topic::BATTERY, j.dump());
}

void MqttBridgeNode::onErrorEvent(const std_msgs::msg::String::SharedPtr msg)
{
  json j;
  j["robot_id"]      = robot_id_;
  j["error_message"] = msg->data;
  j["timestamp"]     = std::to_string(now().seconds());
  mqtt_client_->publish(topic::ERROR, j.dump());
  RCLCPP_ERROR(get_logger(), "Error published: %s", msg->data.c_str());
}

void MqttBridgeNode::onTaskCompleteEvent(const std_msgs::msg::Int32::SharedPtr msg)
{
  json j;
  j["robot_id"]  = robot_id_;
  j["task_id"]   = msg->data;
  j["timestamp"] = std::to_string(now().seconds());
  mqtt_client_->publish(topic::TASK_COMPLETE, j.dump());
}

}  // namespace server_bridge