import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    pkg_share = get_package_share_directory('sensor_components')
    imu_config = os.path.join(pkg_share, 'config', 'imu_params.yaml')

    return LaunchDescription([
        Node(
            package='sensor_components',
            executable='imu_node',
            name='imu_node',
            output='screen',
            parameters=[imu_config],
        ),
        Node(
            package='tf2_ros',
            executable='static_transform_publisher',
            name='imu_link_static_transform',
            arguments=[
                '0.30', '0.00', '0.00',
                '0.0', '0.0', '0.0',
                'base_link', 'imu_link',
            ],
        ),
    ])
