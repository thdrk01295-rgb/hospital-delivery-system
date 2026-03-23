import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    pkg_share = get_package_share_directory('med_bot_slam')

    use_sim_time = LaunchConfiguration('use_sim_time')
    rviz_config = LaunchConfiguration('rviz_config')

    default_rviz_config = os.path.join(
        pkg_share,
        'rviz',
        'mapping_config.rviz'
    )

    return LaunchDescription([
        DeclareLaunchArgument(
            'use_sim_time',
            default_value='false'
        ),
        DeclareLaunchArgument(
            'rviz_config',
            default_value=default_rviz_config
        ),

        Node(
            package='rviz2',
            executable='rviz2',
            name='rviz2_mapping',
            output='screen',
            arguments=['-d', rviz_config],
            parameters=[{'use_sim_time': use_sim_time}]
        )
    ])