import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    default_params_file = os.path.join(
        get_package_share_directory('scan_filter'),
        'config',
        'scan_filter_params.yaml'
    )

    return LaunchDescription([
        DeclareLaunchArgument(
            'params_file',
            default_value=default_params_file
        ),
        Node(
            package='scan_filter',
            executable='scan_filter_node',
            name='scan_filter_node',
            output='screen',
            parameters=[LaunchConfiguration('params_file')]
        ),
    ])
