import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    pkg_share = get_package_share_directory('sensor_components')
    default_params = os.path.join(pkg_share, 'config', 'collision_guard_params.yaml')
    params_file = LaunchConfiguration('params_file')

    return LaunchDescription([
        DeclareLaunchArgument(
            'params_file',
            default_value=default_params,
            description='Path to ToF collision guard parameter file',
        ),
        Node(
            package='sensor_components',
            executable='collision_guard_node',
            name='collision_guard_node',
            output='screen',
            parameters=[params_file],
        ),
    ])
