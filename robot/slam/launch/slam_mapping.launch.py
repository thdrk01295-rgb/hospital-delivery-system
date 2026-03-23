import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    pkg_share = get_package_share_directory('slam')

    use_sim_time = LaunchConfiguration('use_sim_time')
    params_file = LaunchConfiguration('params_file')

    default_params_file = os.path.join(
        pkg_share,
        'config',
        'mapper_params.yaml'
    )

    return LaunchDescription([
        DeclareLaunchArgument(
            'use_sim_time',
            default_value='false'
        ),
        DeclareLaunchArgument(
            'params_file',
            default_value=default_params_file
        ),

        Node(
            package='control',
            executable='motor_bridge_node',
            name='motor_bridge_node',
            output='screen'
        ),

        Node(
            package='control',
            executable='encoder_odom_node',
            name='encoder_odom_node',
            output='screen',
            parameters=[{
                'wheel_radius': 0.0625,
                'wheel_separation': 0.330,
                'ticks_per_revolution': 2048,
                'feedback_topic': '/motor_feedback_raw',
                'odom_topic': '/odom',
                'odom_frame': 'odom',
                'base_frame': 'base_link',
                'publish_tf': True,
                'use_total_counts': True
            }]
        ),

        Node(
            package='slam_toolbox',
            executable='async_slam_toolbox_node',
            name='slam_toolbox',
            output='screen',
            parameters=[
                params_file,
                {'use_sim_time': use_sim_time}
            ]
        )
    ])