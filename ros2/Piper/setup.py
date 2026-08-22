from glob import glob
from setuptools import find_packages, setup

package_name = "piper_bridge"

setup(
    name=package_name,
    version="0.1.0",
    packages=find_packages(exclude=("test",)),
    data_files=[
        ("share/ament_index/resource_index/packages", ["resource/" + package_name]),
        ("share/" + package_name, ["package.xml"]),
        ("share/" + package_name + "/config", glob("config/*.yaml")),
        ("share/" + package_name + "/launch", glob("launch/*.launch.py")),
    ],
    install_requires=["setuptools"],
    zip_safe=True,
    maintainer="DoorDog",
    maintainer_email="maintainer@doordog.local",
    description="A2 PC2 ROS 2 bridge for PiPER USB-CAN control.",
    license="BSD-3-Clause",
    entry_points={
        "console_scripts": [
            "piper_bridge = piper_bridge.bridge_node:main",
            "piper_smoke_test = piper_bridge.smoke_test:main",
            "piper_krushell_manipulation = piper_bridge.run_krushell_manipulation:main",
        ],
    },
)
