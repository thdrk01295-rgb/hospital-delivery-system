from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([

        # ===== motor params =====
        DeclareLaunchArgument('port', default_value='/dev/ttyACM0'),
        DeclareLaunchArgument('baudrate', default_value='115200'),
        DeclareLaunchArgument('wheel_separation', default_value='0.38'),
        DeclareLaunchArgument('max_linear_vel', default_value='1.0'),
        DeclareLaunchArgument('watchdog_timeout', default_value='0.5'),

        Node(
            package='control',
            executable='motor_bridge_node',
            name='motor_bridge_node',
            output='screen',
            parameters=[{
                'port': LaunchConfiguration('port'),
                'baudrate': LaunchConfiguration('baudrate'),
                'wheel_separation': LaunchConfiguration('wheel_separation'),
                'max_linear_vel': LaunchConfiguration('max_linear_vel'),
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
                'base_frame': 'base_link',
                'wheel_radius': 0.0625,
                'wheel_separation': 0.38,
                'ticks_per_revolution': 75.0,
                'publish_tf': False,
                'left_tick_sign': 1.0,
                'right_tick_sign': 1.0
            }]
        ),
    ])
