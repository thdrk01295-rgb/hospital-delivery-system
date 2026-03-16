from launch import LaunchDescription
from launch.actions import ExecuteProcess, DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    port_arg = DeclareLaunchArgument(
        'port',
        default_value='/dev/ttyACM0'
    )

    baudrate_arg = DeclareLaunchArgument(
        'baudrate',
        default_value='115200'
    )

    wheel_separation_arg = DeclareLaunchArgument(
        'wheel_separation',
        default_value='0.30'
    )

    wheel_radius_arg = DeclareLaunchArgument(
        'wheel_radius',
        default_value='0.05'
    )

    max_wheel_speed_arg = DeclareLaunchArgument(
        'max_wheel_speed',
        default_value='1.0'
    )

    watchdog_timeout_arg = DeclareLaunchArgument(
        'watchdog_timeout',
        default_value='0.5'
    )

    motor_bridge_node = Node(
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
    )

    teleop_keyboard = ExecuteProcess(
        cmd=[
            'xterm', '-e',
            'ros2', 'run', 'teleop_twist_keyboard', 'teleop_twist_keyboard'
        ],
        output='screen'
    )

    return LaunchDescription([
        port_arg,
        baudrate_arg,
        wheel_separation_arg,
        wheel_radius_arg,
        max_wheel_speed_arg,
        watchdog_timeout_arg,
        motor_bridge_node,
        teleop_keyboard
    ])