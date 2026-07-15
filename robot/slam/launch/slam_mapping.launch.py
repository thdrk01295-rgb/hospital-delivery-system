from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, EmitEvent, RegisterEventHandler
from launch.conditions import IfCondition
from launch.events import matches_action
from launch.substitutions import Command, LaunchConfiguration
from launch_ros.actions import LifecycleNode, Node
from launch_ros.event_handlers import OnStateTransition
from launch_ros.events.lifecycle import ChangeState
from launch_ros.parameter_descriptions import ParameterValue
from lifecycle_msgs.msg import Transition
import os


def generate_launch_description():
    slam_share = get_package_share_directory('slam')
    control_share = get_package_share_directory('control')
    sensor_share = get_package_share_directory('sensor_components')
    description_share = get_package_share_directory('description')
    lidar_share = get_package_share_directory('rplidar_ros')

    use_sim_time = LaunchConfiguration('use_sim_time')
    use_ekf = LaunchConfiguration('use_ekf')
    slam_params_file = LaunchConfiguration('slam_params_file')
    encoder_params_file = LaunchConfiguration('encoder_params_file')
    imu_params_file = LaunchConfiguration('imu_params_file')
    ekf_params_file = LaunchConfiguration('ekf_params_file')
    laser_filter_params_file = LaunchConfiguration('laser_filter_params_file')
    laser_filter_input_topic = LaunchConfiguration('laser_filter_input_topic')
    laser_filter_output_topic = LaunchConfiguration('laser_filter_output_topic')

    default_slam_params_file = os.path.join(
        slam_share,
        'config',
        'slam_mapper_params.yaml'
    )
    default_encoder_params_file = os.path.join(
        control_share,
        'config',
        'encoder_odom_params.yaml'
    )
    default_imu_params_file = os.path.join(
        sensor_share,
        'config',
        'imu_params.yaml'
    )
    default_ekf_params_file = os.path.join(
        sensor_share,
        'config',
        'ekf.yaml'
    )
    default_urdf_file = os.path.join(
        description_share,
        'urdf',
        'robot.urdf.xacro'
    )
    default_laser_filter_params_file = os.path.join(
        lidar_share,
        'config',
        'use_37to300_laser_filter.yaml'
    )

    robot_description = ParameterValue(
        Command(['xacro ', default_urdf_file]),
        value_type=str
    )
    rplidar_parameters = [{
        'channel_type': 'serial',
        'serial_port': LaunchConfiguration('rplidar_serial_port'),
        'serial_baudrate': ParameterValue(LaunchConfiguration('rplidar_serial_baudrate'), value_type=int),
        'frame_id': LaunchConfiguration('rplidar_frame_id'),
        'inverted': ParameterValue(LaunchConfiguration('rplidar_inverted'), value_type=bool),
        'angle_compensate': ParameterValue(LaunchConfiguration('rplidar_angle_compensate'), value_type=bool),
        'scan_mode': LaunchConfiguration('rplidar_scan_mode'),
        'scan_frequency': ParameterValue(LaunchConfiguration('rplidar_scan_frequency'), value_type=float)
    }]

    slam_toolbox_node = LifecycleNode(
        package='slam_toolbox',
        executable='async_slam_toolbox_node',
        name='slam_toolbox',
        namespace='',
        output='screen',
        parameters=[
            slam_params_file,
            {
                'use_lifecycle_manager': False,
                'use_sim_time': use_sim_time
            }
        ]
    )

    configure_slam_toolbox = EmitEvent(
        event=ChangeState(
            lifecycle_node_matcher=matches_action(slam_toolbox_node),
            transition_id=Transition.TRANSITION_CONFIGURE
        )
    )

    activate_slam_toolbox = RegisterEventHandler(
        OnStateTransition(
            target_lifecycle_node=slam_toolbox_node,
            start_state='configuring',
            goal_state='inactive',
            entities=[
                EmitEvent(event=ChangeState(
                    lifecycle_node_matcher=matches_action(slam_toolbox_node),
                    transition_id=Transition.TRANSITION_ACTIVATE
                ))
            ]
        )
    )

    return LaunchDescription([
        DeclareLaunchArgument(
            'use_sim_time',
            default_value='false'
        ),
        DeclareLaunchArgument(
            'use_ekf',
            default_value='true'
        ),
        DeclareLaunchArgument(
            'slam_params_file',
            default_value=default_slam_params_file
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
            'motor_port',
            default_value='/dev/ttyACM0'
        ),
        DeclareLaunchArgument(
            'motor_baudrate',
            default_value='115200'
        ),
        DeclareLaunchArgument(
            'rplidar_serial_port',
            default_value='/dev/ttyUSB0'
        ),
        DeclareLaunchArgument(
            'rplidar_serial_baudrate',
            default_value='460800'
        ),
        DeclareLaunchArgument(
            'rplidar_frame_id',
            default_value='laser_frame'
        ),
        DeclareLaunchArgument(
            'rplidar_inverted',
            default_value='false'
        ),
        DeclareLaunchArgument(
            'rplidar_angle_compensate',
            default_value='true'
        ),
        DeclareLaunchArgument(
            'rplidar_scan_mode',
            default_value='Standard'
        ),
        DeclareLaunchArgument(
            'rplidar_scan_frequency',
            default_value='10.0'
        ),
        DeclareLaunchArgument(
            'laser_filter_params_file',
            default_value=default_laser_filter_params_file
        ),
        DeclareLaunchArgument(
            'laser_filter_input_topic',
            default_value='/scan_raw'
        ),
        DeclareLaunchArgument(
            'laser_filter_output_topic',
            default_value='/scan'
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
                'wheel_separation': 0.35,
                'wheel_radius': 0.0625,
                'max_motor_rpm': 204.0,
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
            package='rplidar_ros',
            executable='rplidar_node',
            name='rplidar_node',
            output='screen',
            parameters=rplidar_parameters,
            remappings=[
                ('scan', laser_filter_input_topic)
            ]
        ),
        Node(
            package='laser_filters',
            executable='scan_to_scan_filter_chain',
            name='scan_to_scan_filter_chain',
            output='screen',
            parameters=[
                laser_filter_params_file,
                {'use_sim_time': use_sim_time}
            ],
            remappings=[
                ('scan', laser_filter_input_topic),
                ('scan_filtered', laser_filter_output_topic)
            ]
        ),
        slam_toolbox_node,
        configure_slam_toolbox,
        activate_slam_toolbox
    ])
