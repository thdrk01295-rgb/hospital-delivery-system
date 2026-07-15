#!/usr/bin/env python3

import math
import threading
import time
from dataclasses import dataclass
from typing import Any, Dict, Optional

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Range


SENSORS_CONFIG = [
    {
        "name": "front_right",
        "xshut_pin": "D17",
        "address": 0x30,
        "type": "vl53l0x",
        "topic": "/tof/front_right",
        "frame_id": "tof_front_right_link",
    },
    {
        "name": "front_left",
        "xshut_pin": "D27",
        "address": 0x31,
        "type": "vl53l0x",
        "topic": "/tof/front_left",
        "frame_id": "tof_front_left_link",
    },
    {
        "name": "rear_right",
        "xshut_pin": "D22",
        "address": 0x32,
        "type": "vl53l0x",
        "topic": "/tof/rear_right",
        "frame_id": "tof_rear_right_link",
    },
    {
        "name": "rear_left",
        "xshut_pin": "D23",
        "address": 0x33,
        "type": "vl53l0x",
        "topic": "/tof/rear_left",
        "frame_id": "tof_rear_left_link",
    },
    {
        "name": "rear_center",
        "xshut_pin": "D25",
        "address": 0x34,
        "type": "vl53l1x",
        "topic": "/tof/rear_center",
        "frame_id": "tof_rear_center_link",
    },
]


@dataclass
class SensorState:
    sensor: Optional[Any] = None
    xshut: Optional[Any] = None
    latest_range: float = math.nan
    latest_timestamp: float = 0.0
    last_error: str = ""
    initialized: bool = False
    consecutive_errors: int = 0
    last_log_time: float = 0.0


class TofNode(Node):
    def __init__(self) -> None:
        super().__init__("tof_node")

        self.publish_rate_hz = self.declare_parameter("publish_rate_hz", 10.0).value
        self.read_rate_hz = self.declare_parameter("read_rate_hz", 10.0).value
        self.min_range = self.declare_parameter("min_range", 0.03).value
        self.max_range = self.declare_parameter("max_range", 1.2).value
        self.field_of_view = self.declare_parameter("field_of_view", 0.436).value
        self.init_all_low_delay_sec = self.declare_parameter(
            "init_all_low_delay_sec", 0.1).value
        self.init_each_sensor_delay_sec = self.declare_parameter(
            "init_each_sensor_delay_sec", 0.05).value
        self.vl53l1x_ready_retries = self.declare_parameter(
            "vl53l1x_ready_retries", 20).value
        self.vl53l1x_ready_wait_sec = self.declare_parameter(
            "vl53l1x_ready_wait_sec", 0.01).value
        self.error_log_throttle_sec = self.declare_parameter(
            "error_log_throttle_sec", 2.0).value

        self._lock = threading.Lock()
        self._stop_event = threading.Event()
        self._i2c = None
        self._states: Dict[str, SensorState] = {
            config["name"]: SensorState() for config in SENSORS_CONFIG
        }
        self._publishers = {
            config["name"]: self.create_publisher(Range, config["topic"], 10)
            for config in SENSORS_CONFIG
        }

        self._initialize_hardware()

        self._worker = threading.Thread(target=self._read_loop, daemon=True)
        self._worker.start()

        publish_period = 1.0 / max(float(self.publish_rate_hz), 0.1)
        self._timer = self.create_timer(publish_period, self._publish_latest)

    def _initialize_hardware(self) -> None:
        try:
            import adafruit_vl53l0x
            import adafruit_vl53l1x
            import board
            import busio
            import digitalio
        except Exception as exc:
            self.get_logger().error(f"Failed to import Adafruit ToF libraries: {exc}")
            return

        try:
            self._i2c = busio.I2C(board.SCL, board.SDA)
        except Exception as exc:
            self.get_logger().error(f"Failed to create I2C bus: {exc}")
            return

        for config in SENSORS_CONFIG:
            state = self._states[config["name"]]
            try:
                pin = getattr(board, config["xshut_pin"])
                xshut = digitalio.DigitalInOut(pin)
                xshut.direction = digitalio.Direction.OUTPUT
                xshut.value = False
                state.xshut = xshut
            except Exception as exc:
                state.last_error = str(exc)
                self.get_logger().error(
                    f"Failed to configure XSHUT for {config['name']}: {exc}")

        time.sleep(float(self.init_all_low_delay_sec))

        for config in SENSORS_CONFIG:
            state = self._states[config["name"]]
            if state.xshut is None:
                continue
            try:
                state.xshut.value = True
                time.sleep(float(self.init_each_sensor_delay_sec))

                if config["type"] == "vl53l1x":
                    sensor = adafruit_vl53l1x.VL53L1X(self._i2c)
                    sensor.set_address(config["address"])
                    sensor.start_ranging()
                else:
                    sensor = adafruit_vl53l0x.VL53L0X(self._i2c)
                    sensor.set_address(config["address"])

                state.sensor = sensor
                state.initialized = True
                state.last_error = ""
                self.get_logger().info(
                    f"Initialized {config['name']} "
                    f"({config['type']}, address=0x{config['address']:02x})")
            except Exception as exc:
                state.sensor = None
                state.initialized = False
                state.latest_range = math.nan
                state.latest_timestamp = time.monotonic()
                state.last_error = str(exc)
                try:
                    state.xshut.value = False
                except Exception:
                    pass
                self.get_logger().error(f"Failed to initialize {config['name']}: {exc}")

    def _read_loop(self) -> None:
        read_period = 1.0 / max(float(self.read_rate_hz), 0.1)
        while not self._stop_event.is_set():
            loop_started = time.monotonic()
            for config in SENSORS_CONFIG:
                if self._stop_event.is_set():
                    break
                self._read_sensor(config)

            elapsed = time.monotonic() - loop_started
            sleep_sec = max(0.0, read_period - elapsed)
            self._stop_event.wait(sleep_sec)

    def _read_sensor(self, config: Dict[str, Any]) -> None:
        name = config["name"]
        with self._lock:
            state = self._states[name]
            sensor = state.sensor
            initialized = state.initialized

        if not initialized or sensor is None:
            self._set_sensor_error(name, "not initialized")
            return

        try:
            if config["type"] == "vl53l1x":
                distance_m = self._read_vl53l1x_m(sensor, name)
            else:
                distance_m = self._read_vl53l0x_m(sensor, name)

            if distance_m is None or math.isnan(distance_m):
                raise RuntimeError("distance is None or NaN")

            if distance_m > float(self.max_range):
                published_range = math.inf
            elif distance_m < float(self.min_range):
                published_range = math.nan
            else:
                published_range = distance_m

            with self._lock:
                state = self._states[name]
                state.latest_range = published_range
                state.latest_timestamp = time.monotonic()
                state.last_error = ""
                state.consecutive_errors = 0
        except Exception as exc:
            self._set_sensor_error(name, str(exc))

    def _read_vl53l0x_m(self, sensor: Any, name: str) -> float:
        distance_mm = sensor.range
        if distance_mm is None:
            raise RuntimeError(f"{name} returned None")
        return float(distance_mm) / 1000.0

    def _read_vl53l1x_m(self, sensor: Any, name: str) -> float:
        ready = False
        for _ in range(int(self.vl53l1x_ready_retries)):
            if self._stop_event.is_set():
                raise RuntimeError("shutdown requested")
            if sensor.data_ready:
                ready = True
                break
            time.sleep(float(self.vl53l1x_ready_wait_sec))

        if not ready:
            raise TimeoutError(
                f"{name} data_ready timeout after "
                f"{int(self.vl53l1x_ready_retries)} retries")

        distance_cm = sensor.distance
        sensor.clear_interrupt()
        if distance_cm is None:
            raise RuntimeError(f"{name} returned None")
        return float(distance_cm) / 100.0

    def _set_sensor_error(self, name: str, error: str) -> None:
        should_log = False
        consecutive_errors = 0
        with self._lock:
            state = self._states[name]
            now = time.monotonic()
            state.latest_range = math.nan
            state.latest_timestamp = now
            state.last_error = error
            state.consecutive_errors += 1
            consecutive_errors = state.consecutive_errors
            if now - state.last_log_time >= float(self.error_log_throttle_sec):
                state.last_log_time = now
                should_log = True

        if should_log:
            self.get_logger().warn(
                f"{name} read error ({consecutive_errors} consecutive): {error}")

    def _publish_latest(self) -> None:
        now_msg = self.get_clock().now().to_msg()
        with self._lock:
            ranges = {
                name: state.latest_range for name, state in self._states.items()
            }

        for config in SENSORS_CONFIG:
            msg = Range()
            msg.header.stamp = now_msg
            msg.header.frame_id = config["frame_id"]
            msg.radiation_type = Range.INFRARED
            msg.field_of_view = float(self.field_of_view)
            msg.min_range = float(self.min_range)
            msg.max_range = float(self.max_range)
            msg.range = float(ranges[config["name"]])
            self._publishers[config["name"]].publish(msg)

    def destroy_node(self) -> bool:
        self._stop_event.set()
        worker = getattr(self, "_worker", None)
        if worker is not None and worker.is_alive():
            worker.join(timeout=2.0)

        for config in SENSORS_CONFIG:
            state = self._states.get(config["name"])
            if state is None:
                continue
            if config["type"] == "vl53l1x" and state.sensor is not None:
                try:
                    state.sensor.stop_ranging()
                except Exception as exc:
                    self.get_logger().warn(
                        f"Failed to stop ranging for {config['name']}: {exc}")
            if state.xshut is not None:
                try:
                    state.xshut.value = False
                except Exception:
                    pass
                try:
                    state.xshut.deinit()
                except Exception as exc:
                    self.get_logger().warn(
                        f"Failed to deinit XSHUT for {config['name']}: {exc}")

        if self._i2c is not None:
            try:
                self._i2c.deinit()
            except Exception as exc:
                self.get_logger().warn(f"Failed to deinit I2C bus: {exc}")

        return super().destroy_node()


def main(args=None) -> None:
    rclpy.init(args=args)
    node = TofNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
