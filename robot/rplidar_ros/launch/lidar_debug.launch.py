#!/usr/bin/env python3

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import Command, LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    description_dir = get_package_share_directory('description')
    lidar_dir = get_package_share_directory('rplidar_ros')

    use_sim_time = LaunchConfiguration('use_sim_time')
    laser_filter_params_file = LaunchConfiguration('laser_filter_params_file')
    laser_filter_input_topic = LaunchConfiguration('laser_filter_input_topic')
    laser_filter_output_topic = LaunchConfiguration('laser_filter_output_topic')

    default_urdf_file = os.path.join(
        description_dir,
        'urdf',
        'robot.urdf.xacro',
    )
    default_laser_filter_params_file = os.path.join(
        lidar_dir,
        'config',
        'front_180_laser_filter.yaml',
    )

    robot_description = ParameterValue(
        Command(['xacro ', default_urdf_file]),
        value_type=str,
    )
    rplidar_parameters = [{
        'channel_type': 'serial',
        'serial_port': LaunchConfiguration('rplidar_serial_port'),
        'serial_baudrate': ParameterValue(LaunchConfiguration('rplidar_serial_baudrate'), value_type=int),
        'frame_id': LaunchConfiguration('rplidar_frame_id'),
        'inverted': ParameterValue(LaunchConfiguration('rplidar_inverted'), value_type=bool),
        'angle_compensate': ParameterValue(LaunchConfiguration('rplidar_angle_compensate'), value_type=bool),
        'scan_mode': LaunchConfiguration('rplidar_scan_mode'),
        'scan_frequency': ParameterValue(LaunchConfiguration('rplidar_scan_frequency'), value_type=float),
    }]

    return LaunchDescription([
        DeclareLaunchArgument(
            'use_sim_time',
            default_value='false',
        ),
        DeclareLaunchArgument(
            'rplidar_serial_port',
            default_value='/dev/ttyUSB0',
        ),
        DeclareLaunchArgument(
            'rplidar_serial_baudrate',
            default_value='460800',
        ),
        DeclareLaunchArgument(
            'rplidar_frame_id',
            default_value='laser_frame',
        ),
        DeclareLaunchArgument(
            'rplidar_inverted',
            default_value='false',
        ),
        DeclareLaunchArgument(
            'rplidar_angle_compensate',
            default_value='true',
        ),
        DeclareLaunchArgument(
            'rplidar_scan_mode',
            default_value='Standard',
        ),
        DeclareLaunchArgument(
            'rplidar_scan_frequency',
            default_value='10.0',
        ),
        DeclareLaunchArgument(
            'laser_filter_params_file',
            default_value=default_laser_filter_params_file,
        ),
        DeclareLaunchArgument(
            'laser_filter_input_topic',
            default_value='/scan_raw',
        ),
        DeclareLaunchArgument(
            'laser_filter_output_topic',
            default_value='/scan',
        ),
        Node(
            package='robot_state_publisher',
            executable='robot_state_publisher',
            name='robot_state_publisher',
            output='screen',
            parameters=[{
                'robot_description': robot_description,
                'use_sim_time': use_sim_time,
            }],
        ),
        Node(
            package='joint_state_publisher',
            executable='joint_state_publisher',
            name='joint_state_publisher',
            output='screen',
            parameters=[{'use_sim_time': use_sim_time}],
        ),
        Node(
            package='rplidar_ros',
            executable='rplidar_node',
            name='rplidar_node',
            output='screen',
            parameters=rplidar_parameters,
            remappings=[
                ('scan', laser_filter_input_topic),
            ],
        ),
        Node(
            package='laser_filters',
            executable='scan_to_scan_filter_chain',
            name='scan_to_scan_filter_chain',
            output='screen',
            parameters=[
                laser_filter_params_file,
                {'use_sim_time': use_sim_time},
            ],
            remappings=[
                ('scan', laser_filter_input_topic),
                ('scan_filtered', laser_filter_output_topic),
            ],
        ),
    ])
