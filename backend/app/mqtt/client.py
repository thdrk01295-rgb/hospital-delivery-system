"""
MQTT client setup and background thread.
Connects to Mosquitto, subscribes to robot topics, and routes messages to handlers.
"""
import json
import logging
import threading

import paho.mqtt.client as mqtt

from app.config.settings import settings
from app.constants import mqtt_topics

logger = logging.getLogger(__name__)

_mqtt_client: mqtt.Client | None = None


def get_mqtt_client() -> mqtt.Client:
    if _mqtt_client is None:
        raise RuntimeError("MQTT client has not been initialized")
    return _mqtt_client


def publish(topic: str, payload: dict) -> None:
    """Publish a JSON payload to a topic (fire and forget)."""
    try:
        get_mqtt_client().publish(topic, json.dumps(payload))
    except Exception as e:
        logger.error(f"MQTT publish failed on topic {topic}: {e}")


def _on_connect(client: mqtt.Client, userdata, flags, rc):
    if rc == 0:
        logger.info("MQTT connected")
        client.subscribe(mqtt_topics.ROBOT_STATUS)
        client.subscribe(mqtt_topics.ROBOT_LOCATION)
        client.subscribe(mqtt_topics.ROBOT_BATTERY)
        client.subscribe(mqtt_topics.ROBOT_ERROR)
        client.subscribe(mqtt_topics.ROBOT_TASK_COMPLETE)
    else:
        logger.error(f"MQTT connection failed, rc={rc}")


def _on_message(client: mqtt.Client, userdata, msg: mqtt.MQTTMessage):
    from app.mqtt.handlers import dispatch_message
    try:
        payload = json.loads(msg.payload.decode())
        dispatch_message(msg.topic, payload)
    except Exception as e:
        logger.error(f"MQTT message error on {msg.topic}: {e}")


def start_mqtt() -> None:
    """Initialize and start the MQTT client in a background thread."""
    global _mqtt_client
    client = mqtt.Client(client_id=settings.MQTT_CLIENT_ID)
    client.on_connect = _on_connect
    client.on_message = _on_message
    try:
        client.connect(settings.MQTT_HOST, settings.MQTT_PORT, keepalive=60)
        _mqtt_client = client
        thread = threading.Thread(target=client.loop_forever, daemon=True)
        thread.start()
        logger.info(f"MQTT client started — broker {settings.MQTT_HOST}:{settings.MQTT_PORT}")
    except Exception as e:
        logger.warning(f"MQTT broker unavailable at startup: {e}. Running without MQTT.")
