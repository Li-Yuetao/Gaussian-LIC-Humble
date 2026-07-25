import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration


def generate_launch_description():
    gaussian_share = get_package_share_directory("gaussian_lic")
    cocolic_share = get_package_share_directory("cocolic")

    joint_launch = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(
                gaussian_share, "launch", "cocolic_gaussian.launch.py")),
        launch_arguments={
            "gaussian_config": os.path.join(
                gaussian_share, "config", "simlab.yaml"),
            "cocolic_config": os.path.join(
                cocolic_share, "config", "ct_odometry_simlab.yaml"),
            "bag_path": LaunchConfiguration("bag_path"),
            "bag_start": LaunchConfiguration("bag_start"),
            "bag_durr": LaunchConfiguration("bag_durr"),
            "pause_time": LaunchConfiguration("pause_time"),
            "verbose": LaunchConfiguration("verbose"),
            "rviz": LaunchConfiguration("rviz"),
            "result_path": LaunchConfiguration("result_path"),
        }.items(),
    )

    return LaunchDescription([
        DeclareLaunchArgument(
            "bag_path",
            default_value="",
            description="Absolute path to the custom rosbag2 directory"),
        DeclareLaunchArgument("bag_start", default_value="0.0"),
        DeclareLaunchArgument("bag_durr", default_value="-1.0"),
        DeclareLaunchArgument("pause_time", default_value="-1.0"),
        DeclareLaunchArgument("verbose", default_value="false"),
        DeclareLaunchArgument("rviz", default_value="true"),
        DeclareLaunchArgument(
            "result_path",
            default_value=os.path.join(os.getcwd(), "result_simlab")),
        joint_launch,
    ])
