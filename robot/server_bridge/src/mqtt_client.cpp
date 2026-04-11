#include "server_bridge/mqtt_client.hpp"
#include <chrono>
#include <stdexcept>
#include <thread>

namespace server_bridge
{

MqttClient::MqttClient(const std::string & broker_uri, const std::string & client_id, int qos)
: broker_uri_(broker_uri), client_id_(client_id), qos_(qos)
{
  int rc = MQTTAsync_create(&client_, broker_uri_.c_str(), client_id_.c_str(),
                             MQTTCLIENT_PERSISTENCE_NONE, nullptr);
  if (rc != MQTTASYNC_SUCCESS) {
    throw std::runtime_error("MQTTAsync_create failed: " + std::to_string(rc));
  }
  MQTTAsync_setCallbacks(client_, this, onConnectionLost, onMessageArrived, nullptr);
}

MqttClient::~MqttClient()
{
  disconnect();
  MQTTAsync_destroy(&client_);
}

void MqttClient::setMessageCallback(MessageCallback cb)    { message_cb_    = std::move(cb); }
void MqttClient::setConnectionCallback(ConnectionCallback cb) { connection_cb_ = std::move(cb); }

bool MqttClient::connect(int timeout_sec)
{
  MQTTAsync_connectOptions opts = MQTTAsync_connectOptions_initializer;
  opts.keepAliveInterval = 20;
  opts.cleansession      = 1;
  opts.onSuccess         = onConnectSuccess;
  opts.onFailure         = onConnectFailure;
  opts.context           = this;

  if (MQTTAsync_connect(client_, &opts) != MQTTASYNC_SUCCESS) {return false;}

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
  while (!connected_ && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return connected_;
}

void MqttClient::disconnect()
{
  if (!connected_) {return;}
  MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
  opts.timeout = 2000;
  MQTTAsync_disconnect(client_, &opts);
  connected_ = false;
}

bool MqttClient::subscribe(const std::string & topic)
{
  if (!connected_) {
    pending_subs_.push_back(topic);
    return false;
  }
  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
  return MQTTAsync_subscribe(client_, topic.c_str(), qos_, &opts) == MQTTASYNC_SUCCESS;
}

bool MqttClient::publish(const std::string & topic, const std::string & payload)
{
  if (!connected_) {return false;}
  MQTTAsync_message msg  = MQTTAsync_message_initializer;
  msg.payload    = const_cast<char *>(payload.c_str());
  msg.payloadlen = static_cast<int>(payload.size());
  msg.qos        = qos_;
  msg.retained   = 0;
  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
  return MQTTAsync_sendMessage(client_, topic.c_str(), &msg, &opts) == MQTTASYNC_SUCCESS;
}

void MqttClient::handleConnected()
{
  connected_ = true;
  for (const auto & t : pending_subs_) {subscribe(t);}
  pending_subs_.clear();
  if (connection_cb_) {connection_cb_(true);}
}

void MqttClient::handleConnectionLost(const std::string &)
{
  connected_ = false;
  if (connection_cb_) {connection_cb_(false);}
}

void MqttClient::handleMessage(const std::string & topic, const std::string & payload)
{
  if (message_cb_) {message_cb_(topic, payload);}
}

void MqttClient::onConnectSuccess(void * ctx, MQTTAsync_successData *)
{
  static_cast<MqttClient *>(ctx)->handleConnected();
}

void MqttClient::onConnectFailure(void * ctx, MQTTAsync_failureData *)
{
  auto * self = static_cast<MqttClient *>(ctx);
  self->connected_ = false;
  if (self->connection_cb_) {self->connection_cb_(false);}
}

void MqttClient::onConnected(void * ctx, char *)
{
  static_cast<MqttClient *>(ctx)->handleConnected();
}

void MqttClient::onConnectionLost(void * ctx, char * cause)
{
  static_cast<MqttClient *>(ctx)->handleConnectionLost(cause ? cause : "");
}

int MqttClient::onMessageArrived(void * ctx, char * topic, int, MQTTAsync_message * msg)
{
  auto * self = static_cast<MqttClient *>(ctx);
  self->handleMessage(
    std::string(topic),
    std::string(static_cast<char *>(msg->payload), static_cast<size_t>(msg->payloadlen)));
  MQTTAsync_freeMessage(&msg);
  MQTTAsync_free(topic);
  return 1;
}

}  // namespace server_bridge