import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def static_tof_transform(name, x, y, z, yaw):
    return Node(
        package='tf2_ros',
        executable='static_transform_publisher',
        name=f'{name}_static_transform',
        arguments=[
            str(x), str(y), str(z),
            str(yaw), '0.0', '0.0',
            'base_link', name,
        ],
        condition=IfCondition(LaunchConfiguration('publish_static_tf')),
    )


def generate_launch_description():
    pkg_share = get_package_share_directory('sensor_components')
    default_params = os.path.join(pkg_share, 'config', 'tof_params.yaml')
    params_file = LaunchConfiguration('params_file')
    front_left_yaw = LaunchConfiguration('front_left_yaw')
    front_right_yaw = LaunchConfiguration('front_right_yaw')
    rear_left_yaw = LaunchConfiguration('rear_left_yaw')
    rear_right_yaw = LaunchConfiguration('rear_right_yaw')
    rear_center_yaw = LaunchConfiguration('rear_center_yaw')

    return LaunchDescription([
        DeclareLaunchArgument(
            'params_file',
            default_value=default_params,
            description='Path to ToF node parameter file',
        ),
        DeclareLaunchArgument(
            'publish_static_tf',
            default_value='false',
            description='Publish temporary static TFs for ToF sensor frames',
        ),
        DeclareLaunchArgument(
            'front_left_yaw',
            default_value='0.209440',
            description='Front-left ToF yaw relative to base_link in radians',
        ),
        DeclareLaunchArgument(
            'front_right_yaw',
            default_value='-0.209440',
            description='Front-right ToF yaw relative to base_link in radians',
        ),
        DeclareLaunchArgument(
            'rear_left_yaw',
            default_value='2.932153',
            description='Rear-left ToF yaw relative to base_link in radians',
        ),
        DeclareLaunchArgument(
            'rear_right_yaw',
            default_value='-2.932153',
            description='Rear-right ToF yaw relative to base_link in radians',
        ),
        DeclareLaunchArgument(
            'rear_center_yaw',
            default_value='3.141593',
            description='Rear-center ToF yaw relative to base_link in radians',
        ),
        Node(
            package='sensor_components',
            executable='tof_node',
            name='tof_node',
            output='screen',
            parameters=[params_file],
        ),
        static_tof_transform('tof_front_left_link', 0.22, 0.37, 0.15, front_left_yaw),
        static_tof_transform('tof_front_right_link', 0.22, -0.23, 0.15, front_right_yaw),
        static_tof_transform('tof_rear_left_link', -0.55, 0.32, 0.15, rear_left_yaw),
        static_tof_transform('tof_rear_right_link', -0.55, -0.23, 0.15, rear_right_yaw),
        static_tof_transform('tof_rear_center_link', -0.55, 0.0, 0.15, rear_center_yaw),
    ])
