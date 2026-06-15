import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, TimerAction
from launch.conditions import IfCondition, UnlessCondition
from launch.substitutions import Command, LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    navigation_dir = get_package_share_directory('navigation')
    slam_dir = get_package_share_directory('slam')
    control_dir = get_package_share_directory('control')
    sensor_dir = get_package_share_directory('sensor_components')
    description_dir = get_package_share_directory('description')

    params_file = LaunchConfiguration('params_file')
    map_yaml = LaunchConfiguration('map')
    use_sim_time = LaunchConfiguration('use_sim_time')
    use_ekf = LaunchConfiguration('use_ekf')
    encoder_params_file = LaunchConfiguration('encoder_params_file')
    imu_params_file = LaunchConfiguration('imu_params_file')
    ekf_params_file = LaunchConfiguration('ekf_params_file')
    tof_params_file = LaunchConfiguration('tof_params_file')
    collision_guard_params_file = LaunchConfiguration('collision_guard_params_file')
    use_scan_filter = LaunchConfiguration('use_scan_filter')
    scan_filter_input_topic = LaunchConfiguration('scan_filter_input_topic')
    scan_filter_output_topic = LaunchConfiguration('scan_filter_output_topic')

    default_params_file = os.path.join(
        navigation_dir,
        'config',
        'nav2_params.yaml'
    )
    default_map_yaml = os.path.join(
        slam_dir,
        'maps',
        '0521.yaml'
    )
    default_encoder_params_file = os.path.join(
        control_dir,
        'config',
        'encoder_odom_params.yaml'
    )
    default_imu_params_file = os.path.join(
        sensor_dir,
        'config',
        'imu_params.yaml'
    )
    default_ekf_params_file = os.path.join(
        sensor_dir,
        'config',
        'ekf.yaml'
    )
    default_tof_params_file = os.path.join(
        sensor_dir,
        'config',
        'tof_params.yaml'
    )
    default_collision_guard_params_file = os.path.join(
        sensor_dir,
        'config',
        'collision_guard_params.yaml'
    )
    default_urdf_file = os.path.join(
        description_dir,
        'urdf',
        'robot.urdf.xacro'
    )

    robot_description = ParameterValue(
        Command(['xacro ', default_urdf_file]),
        value_type=str
    )
    common_nav2_parameters = [
        params_file,
        {'use_sim_time': use_sim_time}
    ]
    tf_remappings = [
        ('/tf', 'tf'),
        ('/tf_static', 'tf_static')
    ]
    velocity_remappings = [
        ('cmd_vel', '/cmd_vel_nav'),
        ('cmd_vel_smoothed', '/cmd_vel_smoothed')
    ]
    localization_lifecycle_nodes = [
        'map_server',
        'amcl'
    ]
    navigation_lifecycle_nodes = [
        'controller_server',
        'smoother_server',
        'planner_server',
        'behavior_server',
        'bt_navigator',
        'waypoint_follower',
        'velocity_smoother'
    ]
    cyglidar_parameters = [{
        'port_number': LaunchConfiguration('cyglidar_port'),
        'baud_rate': ParameterValue(LaunchConfiguration('cyglidar_baud_rate'), value_type=int),
        'frame_id': 'laser_frame',
        'run_mode': ParameterValue(LaunchConfiguration('cyglidar_run_mode'), value_type=int),
        'frequency_channel': 0,
        'duration_mode': 0,
        'duration_value': 10000,
        'color_mode': 0,
        'data_type_3d': 0,
        'filter_mode': 0,
        'edge_filter_value': 0,
        'enable_kalmanfilter': False,
        'enable_clahe': False,
        'clahe_cliplimit': 40,
        'clahe_tiles_grid_size': 8
    }]

    return LaunchDescription([
        DeclareLaunchArgument(
            'map',
            default_value=default_map_yaml
        ),
        DeclareLaunchArgument(
            'params_file',
            default_value=default_params_file
        ),
        DeclareLaunchArgument(
            'use_sim_time',
            default_value='false'
        ),
        DeclareLaunchArgument(
            'use_ekf',
            default_value='true'
        ),
        DeclareLaunchArgument(
            'encoder_params_file',
            default_value=default_encoder_params_file
        ),
        DeclareLaunchArgument(
            'imu_params_file',
            default_value=default_imu_params_file
        ),
        DeclareLaunchArgument(
            'ekf_params_file',
            default_value=default_ekf_params_file
        ),
        DeclareLaunchArgument(
            'tof_params_file',
            default_value=default_tof_params_file
        ),
        DeclareLaunchArgument(
            'collision_guard_params_file',
            default_value=default_collision_guard_params_file
        ),
        DeclareLaunchArgument(
            'motor_port',
            default_value='/dev/ttyACM0'
        ),
        DeclareLaunchArgument(
            'motor_baudrate',
            default_value='115200'
        ),
        DeclareLaunchArgument(
            'cyglidar_port',
            default_value='/dev/ttyUSB0'
        ),
        DeclareLaunchArgument(
            'cyglidar_baud_rate',
            default_value='0'
        ),
        DeclareLaunchArgument(
            'cyglidar_run_mode',
            default_value='0'
        ),
        DeclareLaunchArgument(
            'use_scan_filter',
            default_value='true'
        ),
        DeclareLaunchArgument(
            'scan_filter_input_topic',
            default_value='/scan_raw'
        ),
        DeclareLaunchArgument(
            'scan_filter_output_topic',
            default_value='/scan'
        ),
        DeclareLaunchArgument(
            'scan_filter_center_angle_rad',
            default_value='0.0'
        ),
        DeclareLaunchArgument(
            'scan_filter_remove_angle_width_rad',
            default_value='0.06'
        ),
        DeclareLaunchArgument(
            'scan_filter_enabled',
            default_value='true'
        ),
        DeclareLaunchArgument(
            'scan_filter_use_nan',
            default_value='true'
        ),
        DeclareLaunchArgument(
            'scan_filter_replacement_range',
            default_value='0.0'
        ),
        DeclareLaunchArgument(
            'scan_filter_debug_log',
            default_value='false'
        ),
        Node(
            package='robot_state_publisher',
            executable='robot_state_publisher',
            name='robot_state_publisher',
            output='screen',
            parameters=[{
                'robot_description': robot_description,
                'use_sim_time': use_sim_time
            }]
        ),
        Node(
            package='control',
            executable='motor_bridge_node',
            name='motor_bridge_node',
            output='screen',
            parameters=[{
                'port': LaunchConfiguration('motor_port'),
                'baudrate': ParameterValue(LaunchConfiguration('motor_baudrate'), value_type=int),
                'wheel_separation': 0.38,
                'max_linear_vel': 0.25,
                'watchdog_timeout': 0.5
            }]
        ),
        Node(
            package='control',
            executable='encoder_odom_node',
            name='encoder_odom_node',
            output='screen',
            parameters=[
                encoder_params_file,
                {'use_sim_time': use_sim_time}
            ]
        ),
        Node(
            package='sensor_components',
            executable='imu_node',
            name='imu_node',
            output='screen',
            parameters=[
                imu_params_file,
                {'use_sim_time': use_sim_time}
            ]
        ),
        Node(
            package='robot_localization',
            executable='ekf_node',
            name='ekf_filter_node',
            condition=IfCondition(use_ekf),
            output='screen',
            parameters=[
                ekf_params_file,
                {'use_sim_time': use_sim_time}
            ]
        ),
        Node(
            package='cyglidar_d2_ros2',
            executable='cyglidar_d2_publisher',
            name='cyglidar_d2_publisher',
            condition=UnlessCondition(use_scan_filter),
            output='screen',
            parameters=cyglidar_parameters
        ),
        Node(
            package='cyglidar_d2_ros2',
            executable='cyglidar_d2_publisher',
            name='cyglidar_d2_publisher',
            condition=IfCondition(use_scan_filter),
            output='screen',
            parameters=cyglidar_parameters,
            remappings=[
                ('scan', scan_filter_input_topic)
            ]
        ),
        Node(
            package='scan_filter',
            executable='scan_filter_node',
            name='scan_filter_node',
            condition=IfCondition(use_scan_filter),
            output='screen',
            parameters=[{
                'input_scan_topic': scan_filter_input_topic,
                'output_scan_topic': scan_filter_output_topic,
                'enabled': ParameterValue(
                    LaunchConfiguration('scan_filter_enabled'), value_type=bool),
                'center_angle_rad': ParameterValue(
                    LaunchConfiguration('scan_filter_center_angle_rad'), value_type=float),
                'remove_angle_width_rad': ParameterValue(
                    LaunchConfiguration('scan_filter_remove_angle_width_rad'), value_type=float),
                'use_nan': ParameterValue(
                    LaunchConfiguration('scan_filter_use_nan'), value_type=bool),
                'replacement_range': ParameterValue(
                    LaunchConfiguration('scan_filter_replacement_range'), value_type=float),
                'debug_log': ParameterValue(
                    LaunchConfiguration('scan_filter_debug_log'), value_type=bool)
            }]
        ),
        Node(
            package='sensor_components',
            executable='tof_node',
            name='tof_node',
            output='screen',
            parameters=[
                tof_params_file,
                {'use_sim_time': use_sim_time}
            ]
        ),
        Node(
            package='nav2_map_server',
            executable='map_server',
            name='map_server',
            output='screen',
            parameters=[
                params_file,
                {
                    'yaml_filename': map_yaml,
                    'use_sim_time': use_sim_time
                }
            ],
            remappings=tf_remappings
        ),
        Node(
            package='nav2_amcl',
            executable='amcl',
            name='amcl',
            output='screen',
            parameters=common_nav2_parameters,
            remappings=tf_remappings
        ),
        Node(
            package='nav2_lifecycle_manager',
            executable='lifecycle_manager',
            name='lifecycle_manager_localization',
            output='screen',
            parameters=[
                params_file,
                {
                    'use_sim_time': use_sim_time,
                    'autostart': True,
                    'node_names': localization_lifecycle_nodes
                }
            ]
        ),
        Node(
            package='nav2_controller',
            executable='controller_server',
            name='controller_server',
            output='screen',
            parameters=common_nav2_parameters,
            remappings=tf_remappings + [('cmd_vel', '/cmd_vel_nav')]
        ),
        Node(
            package='nav2_smoother',
            executable='smoother_server',
            name='smoother_server',
            output='screen',
            parameters=common_nav2_parameters,
            remappings=tf_remappings
        ),
        Node(
            package='nav2_planner',
            executable='planner_server',
            name='planner_server',
            output='screen',
            parameters=common_nav2_parameters,
            remappings=tf_remappings
        ),
        Node(
            package='nav2_behaviors',
            executable='behavior_server',
            name='behavior_server',
            output='screen',
            parameters=common_nav2_parameters,
            remappings=tf_remappings + [('cmd_vel', '/cmd_vel_nav')]
        ),
        Node(
            package='nav2_bt_navigator',
            executable='bt_navigator',
            name='bt_navigator',
            output='screen',
            parameters=common_nav2_parameters,
            remappings=tf_remappings
        ),
        Node(
            package='nav2_waypoint_follower',
            executable='waypoint_follower',
            name='waypoint_follower',
            output='screen',
            parameters=common_nav2_parameters,
            remappings=tf_remappings
        ),
        Node(
            package='nav2_velocity_smoother',
            executable='velocity_smoother',
            name='velocity_smoother',
            output='screen',
            parameters=common_nav2_parameters,
            remappings=tf_remappings + velocity_remappings
        ),
        Node(
            package='sensor_components',
            executable='collision_guard_node',
            name='collision_guard_node',
            output='screen',
            parameters=[
                collision_guard_params_file,
                {'use_sim_time': use_sim_time}
            ]
        ),
        TimerAction(
            period=2.0,
            actions=[
                Node(
                    package='nav2_lifecycle_manager',
                    executable='lifecycle_manager',
                    name='lifecycle_manager_navigation',
                    output='screen',
                    parameters=[
                        params_file,
                        {
                            'use_sim_time': use_sim_time,
                            'autostart': True,
                            'node_names': navigation_lifecycle_nodes
                        }
                    ]
                )
            ]
        ),
    ])
