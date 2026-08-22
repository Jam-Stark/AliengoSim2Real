from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare
from launch.substitutions import PathJoinSubstitution


def generate_launch_description() -> LaunchDescription:
    namespace = LaunchConfiguration("namespace")
    config_file = LaunchConfiguration("config_file")
    default_config = PathJoinSubstitution(
        [FindPackageShare("piper_bridge"), "config", "piper_bridge.yaml"]
    )
    return LaunchDescription(
        [
            DeclareLaunchArgument("namespace", default_value="piper"),
            DeclareLaunchArgument("config_file", default_value=default_config),
            Node(
                package="piper_bridge",
                executable="piper_bridge",
                name="piper_bridge",
                namespace=namespace,
                output="screen",
                parameters=[config_file],
            ),
        ]
    )
