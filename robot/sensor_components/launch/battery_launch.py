import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    pkg_share = get_package_share_directory('sensor_components')
    default_params = os.path.join(pkg_share, 'config', 'battery_params.yaml')
    params_file = LaunchConfiguration('params_file')

    return LaunchDescription([
        DeclareLaunchArgument(
            'params_file',
            default_value=default_params,
            description='Path to battery node parameter file',
        ),
        Node(
            package='sensor_components',
            executable='battery_node',
            name='battery_node',
            output='screen',
            parameters=[params_file],
        ),
    ])
