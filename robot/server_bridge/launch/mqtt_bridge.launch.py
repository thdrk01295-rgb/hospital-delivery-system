import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():

    pkg_share = get_package_share_directory('server_bridge')

    # ── Launch arguments ──────────────────────────────────────────────────────
    # config 파일 경로를 런타임에 바꿀 수 있음
    # 예: ros2 launch server_bridge mqtt_bridge.launch.py \
    #         params_file:=/path/to/custom_params.yaml
    params_file_arg = DeclareLaunchArgument(
        name='params_file',
        default_value=os.path.join(pkg_share, 'config', 'mqtt_bridge_params.yaml'),
        description='Path to the mqtt_bridge_node parameter YAML file'
    )

    # ── Node ──────────────────────────────────────────────────────────────────
    mqtt_bridge_node = Node(
        package='server_bridge',
        executable='mqtt_bridge_node',
        name='mqtt_bridge_node',
        output='screen',
        emulate_tty=True,
        parameters=[LaunchConfiguration('params_file')],
    )

    return LaunchDescription([
        params_file_arg,
        mqtt_bridge_node,
    ])