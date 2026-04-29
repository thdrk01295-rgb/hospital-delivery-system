import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    pkg_share = get_package_share_directory('sensor_components')

    default_imu_params = os.path.join(pkg_share, 'config', 'imu_params.yaml')
    default_ekf_params = os.path.join(pkg_share, 'config', 'ekf.yaml')

    imu_params_file = LaunchConfiguration('imu_params_file')
    ekf_params_file = LaunchConfiguration('ekf_params_file')

    return LaunchDescription([
        DeclareLaunchArgument(
            'imu_params_file',
            default_value=default_imu_params,
            description='Path to the IMU node parameters file.',
        ),
        DeclareLaunchArgument(
            'ekf_params_file',
            default_value=default_ekf_params,
            description='Path to the robot_localization EKF parameters file.',
        ),

        Node(
            package='sensor_components',
            executable='imu_node',
            name='imu_node',
            output='screen',
            parameters=[imu_params_file],
        ),
        Node(
            package='control',
            executable='encoder_odom_node',
            name='encoder_odom_node',
            output='screen',
            parameters=[{
                'wheel_radius': 0.0625,
                'wheel_separation': 0.38,
                'ticks_per_revolution': 75.0,
                'odom_topic': '/odom_raw',
                'odom_frame': 'odom',
                'base_frame': 'base_link',
                'publish_tf': False,
                'left_tick_sign': 1.0,
                'right_tick_sign': 1.0,
            }],
        ),
        Node(
            package='robot_localization',
            executable='ekf_node',
            name='ekf_filter_node',
            output='screen',
            parameters=[ekf_params_file],
        ),
        Node(
            package='tf2_ros',
            executable='static_transform_publisher',
            name='base_link_to_imu_link_tf',
            output='screen',
            arguments=[
                '--x', '0.30',
                '--y', '0.00',
                '--z', '0.00',
                '--roll', '0.0',
                '--pitch', '0.0',
                '--yaw', '0.0',
                '--frame-id', 'base_link',
                '--child-frame-id', 'imu_link',
            ],
        ),
    ])
