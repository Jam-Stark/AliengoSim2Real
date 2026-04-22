(base) dreams@dreams-ROG-Strix-G16-G614FR-G614FR:~/Downloads/WorkSpace$ cat /etc/os-release
uname -m
nvidia-smi
g++ --version
cmake --version
python3 --version
ls /opt/ros
echo "${UNITREE_ROS2_SETUP:-unset}"
test -f ~/unitree_ros2/setup_local.sh && echo unitree_setup_ok || echo unitree_setup_missing
pkg-config --modversion opencv4
ldconfig -p | egrep 'onnxruntime|jsoncpp|udev'
PRETTY_NAME="Ubuntu 22.04.5 LTS"
NAME="Ubuntu"
VERSION_ID="22.04"
VERSION="22.04.5 LTS (Jammy Jellyfish)"
VERSION_CODENAME=jammy
ID=ubuntu
ID_LIKE=debian
HOME_URL="https://www.ubuntu.com/"
SUPPORT_URL="https://help.ubuntu.com/"
BUG_REPORT_URL="https://bugs.launchpad.net/ubuntu/"
PRIVACY_POLICY_URL="https://www.ubuntu.com/legal/terms-and-policies/privacy-policy"
UBUNTU_CODENAME=jammy
x86_64
Wed Apr 22 16:51:58 2026       
+-----------------------------------------------------------------------------------------+
| NVIDIA-SMI 590.48.01              Driver Version: 590.48.01      CUDA Version: 13.1     |
+-----------------------------------------+------------------------+----------------------+
| GPU  Name                 Persistence-M | Bus-Id          Disp.A | Volatile Uncorr. ECC |
| Fan  Temp   Perf          Pwr:Usage/Cap |           Memory-Usage | GPU-Util  Compute M. |
|                                         |                        |               MIG M. |
|=========================================+========================+======================|
|   0  NVIDIA GeForce RTX 5070 ...    Off |   00000000:01:00.0  On |                  N/A |
| N/A   48C    P8              8W /   65W |     597MiB /  12227MiB |     28%      Default |
|                                         |                        |                  N/A |
+-----------------------------------------+------------------------+----------------------+

+-----------------------------------------------------------------------------------------+
| Processes:                                                                              |
|  GPU   GI   CI              PID   Type   Process name                        GPU Memory |
|        ID   ID                                                               Usage      |
|=========================================================================================|
|    0   N/A  N/A            3113      G   /usr/bin/gnome-shell                    184MiB |
|    0   N/A  N/A           11827      G   ...rack-uuid=3190708988185955192        138MiB |
|    0   N/A  N/A           15057      G   gnome-control-center                      2MiB |
|    0   N/A  N/A           16402      G   /usr/share/code/code                    131MiB |
+-----------------------------------------------------------------------------------------+
g++ (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0
Copyright (C) 2021 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

cmake version 3.22.1

CMake suite maintained and supported by Kitware (kitware.com/cmake).
Python 3.13.9
humble
unset
unitree_setup_missing
4.5.4
        libudev.so.1 (libc6,x86-64) => /lib/x86_64-linux-gnu/libudev.so.1
        libudev.so (libc6,x86-64) => /lib/x86_64-linux-gnu/libudev.so
        libjsoncpp.so.25 (libc6,x86-64) => /lib/x86_64-linux-gnu/libjsoncpp.so.25
        libjsoncpp.so (libc6,x86-64) => /lib/x86_64-linux-gnu/libjsoncpp.so
        libgudev-1.0.so.0 (libc6,x86-64) => /lib/x86_64-linux-gnu/libgudev-1.0.so.0