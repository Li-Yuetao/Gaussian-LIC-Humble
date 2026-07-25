import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, TimerAction
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    gs_share = get_package_share_directory("gaussian_lic")
    coco_share = get_package_share_directory("cocolic")
    bag = (
        "/media/lyt/F15E9476E914632B/dataset/SLAM/"
        "FAST-LIVO2-Dataset/Retail_Street"
    )

    gaussian = Node(
        package="gaussian_lic",
        executable="gs_mapping",
        name="gaussian_lic",
        output="screen",
        parameters=[{
            "config_path": LaunchConfiguration("gaussian_config"),
            "result_path": LaunchConfiguration("result_path"),
            "lpips_path": os.path.join(gs_share, "lpips"),
        }],
    )
    cocolic = Node(
        package="cocolic",
        executable="odometry_node",
        name="cocolic",
        output="screen",
        parameters=[{
            "project_path": coco_share,
            "config_path": LaunchConfiguration("cocolic_config"),
            "bag_path": LaunchConfiguration("bag_path"),
            "bag_start": LaunchConfiguration("bag_start"),
            "bag_durr": LaunchConfiguration("bag_durr"),
            "pause_time": LaunchConfiguration("pause_time"),
            "verbose": LaunchConfiguration("verbose"),
        }],
    )
    rviz = Node(
        package="rviz2",
        executable="rviz2",
        name="rviz_cocolic_gaussian",
        arguments=[
            "-d", os.path.join(coco_share, "config", "coco.rviz")
        ],
        condition=IfCondition(LaunchConfiguration("rviz")),
        output="log",
    )

    return LaunchDescription([
        DeclareLaunchArgument(
            "gaussian_config",
            default_value=os.path.join(gs_share, "config", "fastlivo2.yaml")),
        DeclareLaunchArgument(
            "cocolic_config",
            default_value=os.path.join(
                coco_share, "config", "ct_odometry_fastlivo2.yaml")),
        DeclareLaunchArgument("bag_path", default_value=bag),
        DeclareLaunchArgument("bag_start", default_value="0.0"),
        DeclareLaunchArgument("bag_durr", default_value="-1.0"),
        DeclareLaunchArgument("pause_time", default_value="-1.0"),
        DeclareLaunchArgument("verbose", default_value="false"),
        DeclareLaunchArgument("rviz", default_value="true"),
        DeclareLaunchArgument(
            "result_path", default_value=os.path.join(os.getcwd(), "result")),
        gaussian,
        rviz,
        TimerAction(period=2.0, actions=[cocolic]),
    ])
