from launch import LaunchDescription
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare
from launch.substitutions import PathJoinSubstitution


def generate_launch_description():
    motor_sequences = PathJoinSubstitution([
        FindPackageShare('robot_task'),
        'config',
        'motor_sequences.yaml',
    ])

    return LaunchDescription([
        Node(
            package='robot_task',
            executable='task_manager_node',
            name='task_manager_node',
            output='screen',
        ),
        Node(
            package='robot_task',
            executable='task_motor_sequence_node',
            name='task_motor_sequence_node',
            output='screen',
            parameters=[motor_sequences],
        ),
    ])
