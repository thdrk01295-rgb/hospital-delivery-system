from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([

        # ===== motor params =====
        DeclareLaunchArgument('port', default_value='/dev/ttyACM0'),
        DeclareLaunchArgument('baudrate', default_value='115200'),
        DeclareLaunchArgument('wheel_separation', default_value='0.35'),
        DeclareLaunchArgument('wheel_radius', default_value='0.0625'),
        DeclareLaunchArgument('max_motor_rpm', default_value='204.0'),
        DeclareLaunchArgument('watchdog_timeout', default_value='0.5'),
        DeclareLaunchArgument('enable_control_motor_bridge', default_value='false'),

        Node(
            package='control',
            executable='motor_bridge_node',
            name='motor_bridge_node',
            output='screen',
            parameters=[{
                'port': LaunchConfiguration('port'),
                'baudrate': LaunchConfiguration('baudrate'),
                'wheel_separation': LaunchConfiguration('wheel_separation'),
                'wheel_radius': LaunchConfiguration('wheel_radius'),
                'max_motor_rpm': LaunchConfiguration('max_motor_rpm'),
                'watchdog_timeout': LaunchConfiguration('watchdog_timeout'),
            }]
        ),
        Node(
            package='control',
            executable='encoder_odom_node',
            name='encoder_odom_node',
            output='screen',
            parameters=[{
                'encoder_ticks_topic': '/encoder_ticks',
                'odom_topic': '/odom_raw',
                'odom_frame': 'odom',
                'base_frame': 'base_footprint',
                'wheel_radius': 0.0625,
                'wheel_separation': 0.35,
                'ticks_per_revolution': 75.0,
                'publish_tf': False,
                'left_tick_sign': 1.0,
                'right_tick_sign': 1.0
            }]
        ),
        Node(
            package='control',
            executable='control_motor_bridge_node',
            name='control_motor_bridge_node',
            output='screen',
            condition=IfCondition(LaunchConfiguration('enable_control_motor_bridge')),
        ),
    ])
