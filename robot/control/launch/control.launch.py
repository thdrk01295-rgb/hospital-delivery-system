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
        DeclareLaunchArgument('max_wheel_speed', default_value='1.0'),
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
                'max_wheel_speed': LaunchConfiguration('max_wheel_speed'),
                'watchdog_timeout': LaunchConfiguration('watchdog_timeout'),
            }]
        ),

        Node(
            package='teleop_twist_keyboard',
            executable='teleop_twist_keyboard',
            name='teleop_keyboard',
            prefix='xterm -e',   
            output='screen',
            remappings=[
                ('/cmd_vel', '/cmd_vel')
            ]
        )

    ])