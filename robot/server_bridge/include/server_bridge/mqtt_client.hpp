#ifndef SERVER_BRIDGE__MQTT_CLIENT_HPP_
#define SERVER_BRIDGE__MQTT_CLIENT_HPP_

#include <functional>
#include <string>
#include <vector>
#include "MQTTAsync.h"

namespace server_bridge
{

class MqttClient
{
public:
  using MessageCallback    = std::function<void(const std::string &, const std::string &)>;
  using ConnectionCallback = std::function<void(bool)>;

  MqttClient(const std::string & broker_uri, const std::string & client_id, int qos = 1);
  ~MqttClient();

  MqttClient(const MqttClient &) = delete;
  MqttClient & operator=(const MqttClient &) = delete;

  void setMessageCallback(MessageCallback cb);
  void setConnectionCallback(ConnectionCallback cb);

  bool connect(int timeout_sec = 10);
  void disconnect();
  bool subscribe(const std::string & topic);
  bool publish(const std::string & topic, const std::string & payload);

  bool isConnected() const { return connected_; }

private:
  static void onConnected(void * ctx, char * cause);
  static void onConnectionLost(void * ctx, char * cause);
  static int  onMessageArrived(void * ctx, char * topic, int topic_len, MQTTAsync_message * msg);
  static void onConnectSuccess(void * ctx, MQTTAsync_successData * resp);
  static void onConnectFailure(void * ctx, MQTTAsync_failureData * resp);

  void handleConnected();
  void handleConnectionLost(const std::string & cause);
  void handleMessage(const std::string & topic, const std::string & payload);

  std::string broker_uri_;
  std::string client_id_;
  int qos_;

  MQTTAsync client_{nullptr};
  bool connected_{false};

  MessageCallback    message_cb_;
  ConnectionCallback connection_cb_;
  std::vector<std::string> pending_subs_;
};

}  // namespace server_bridge

#endif  // SERVER_BRIDGE__MQTT_CLIENT_HPP_