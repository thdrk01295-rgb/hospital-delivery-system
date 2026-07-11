from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    motor_sequences = PathJoinSubstitution([
        FindPackageShare('robot_task'),
        'config',
        'motor_sequences.yaml',
    ])

    return LaunchDescription([
        DeclareLaunchArgument(
            'lock_mock_enabled',
            default_value='false',
            description='Use mock lock feedback instead of motor sequence action goals.',
        ),
        Node(
            package='robot_task',
            executable='task_manager_node',
            name='task_manager_node',
            output='screen',
            parameters=[{
                'lock_mock_enabled': LaunchConfiguration('lock_mock_enabled'),
            }],
        ),
        Node(
            package='robot_task',
            executable='task_motor_sequence_node',
            name='task_motor_sequence_node',
            output='screen',
            parameters=[motor_sequences],
        ),
    ])
