from launch import LaunchDescription
from launch_ros.actions import Node
from launch.substitutions import Command
from launch.substitutions import PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    pkg = FindPackageShare("description")

    robot_description = Command([
        "xacro ",
        PathJoinSubstitution([pkg, "urdf", "robot.urdf.xacro"])
    ])

    return LaunchDescription([
        Node(
            package="robot_state_publisher",
            executable="robot_state_publisher",
            output="screen",
            parameters=[{"robot_description": robot_description}]
        ),
        Node(
            package="joint_state_publisher",
            executable="joint_state_publisher",
            output="screen"
        )
    ])