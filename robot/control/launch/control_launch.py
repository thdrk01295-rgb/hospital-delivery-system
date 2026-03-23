from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([

        # ===== motor params =====
        DeclareLaunchArgument('port', default_value='/dev/ttyACM0'),
        DeclareLaunchArgument('baudrate', default_value='115200'),
        DeclareLaunchArgument('wheel_separation', default_value='0.330'),
        DeclareLaunchArgument('wheel_radius', default_value='0.0625'),
        DeclareLaunchArgument('max_rpm', default_value='60.0'),
        DeclareLaunchArgument('watchdog_timeout', default_value='0.5'),

        Node(
            package='motor_bridge',
            executable='motor_bridge_node',
            name='motor_bridge_node',
            output='screen',
            parameters=[{
                'port': LaunchConfiguration('port'),
                'baudrate': LaunchConfiguration('baudrate'),
                'wheel_separation': LaunchConfiguration('wheel_separation'),
                'wheel_radius': LaunchConfiguration('wheel_radius'),
                'max_rpm': LaunchConfiguration('max_rpm'),
                'watchdog_timeout': LaunchConfiguration('watchdog_timeout'),
            }]
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
    ])