import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    share = get_package_share_directory("gaussian_lic")
    return LaunchDescription([
        DeclareLaunchArgument("config_path", default_value=os.path.join(share, "config", "m2dgr.yaml")),
        DeclareLaunchArgument("result_path", default_value=os.path.join(os.getcwd(), "result")),
        Node(package="gaussian_lic", executable="gs_mapping", name="gaussian_lic",
             output="screen", parameters=[{
                 "config_path": LaunchConfiguration("config_path"),
                 "result_path": LaunchConfiguration("result_path"),
                 "lpips_path": os.path.join(share, "lpips"),
             }]),
    ])
