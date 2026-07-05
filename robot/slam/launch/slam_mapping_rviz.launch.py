from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
import os


def generate_launch_description():
    pkg_share = get_package_share_directory('slam')

    use_sim_time = LaunchConfiguration('use_sim_time')
    params_file = LaunchConfiguration('params_file')
    rviz_config = LaunchConfiguration('rviz_config')

    default_params_file = os.path.join(
        pkg_share,
        'config',
        'slam_mapper_params.yaml'
    )

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
            'params_file',
            default_value=default_params_file
        ),
        DeclareLaunchArgument(
            'rviz_config',
            default_value=default_rviz_config
        ),
        # Node(
        #     package='slam_toolbox',
        #     executable='async_slam_toolbox_node',
        #     name='slam_toolbox',
        #     output='screen',
        #     parameters=[
        #         params_file,
        #         {'use_sim_time': use_sim_time}
        #     ]
        # ),
        Node(
            package='rviz2',
            executable='rviz2',
            name='rviz2_mapping',
            output='screen',
            arguments=['-d', rviz_config],
            parameters=[{'use_sim_time': use_sim_time}]
        )
    ])
