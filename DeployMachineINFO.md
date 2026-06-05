# A2 Deploy Machine Info

## Report Metadata

- timestamp: `2026-06-05 18:02:03 HKT`
- hostname: `lt5.precognition.team`
- user: `baoquanc`
- cwd: `/home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real`
- script path: `/home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real/ros2/A2/scripts/collect_deploy_machine_info.sh`
- unitree root: `/home/baoquanc/Downloads/WorkSpace/projects/third_party/unitree`
- no-sensitive mode: `1`

## OS / Kernel / Arch


### /etc/os-release

```text
$ cat /etc/os-release
PRETTY_NAME="Ubuntu 24.04.3 LTS"
NAME="Ubuntu"
VERSION_ID="24.04"
VERSION="24.04.3 LTS (Noble Numbat)"
VERSION_CODENAME=noble
ID=ubuntu
ID_LIKE=debian
HOME_URL="https://www.ubuntu.com/"
SUPPORT_URL="https://help.ubuntu.com/"
BUG_REPORT_URL="https://bugs.launchpad.net/ubuntu/"
PRIVACY_POLICY_URL="https://www.ubuntu.com/legal/terms-and-policies/privacy-policy"
UBUNTU_CODENAME=noble
LOGO=ubuntu-logo
```

### uname

```text
$ uname -a
Linux lt5.precognition.team 6.17.0-23-generic #23~24.04.1-Ubuntu SMP PREEMPT_DYNAMIC Tue Apr 14 16:11:48 UTC 2 x86_64 x86_64 x86_64 GNU/Linux
```
- CPU arch: `x86_64`
- LONG_BIT: `64`

## Network


### interface list

```text
$ if command -v ip >/dev/null 2>&1; then ip -brief link; elif command -v ifconfig >/dev/null 2>&1; then ifconfig -a; else echo 'MISSING: ip and ifconfig'; fi
lo               UNKNOWN        00:00:00:00:00:00 <LOOPBACK,UP,LOWER_UP> 
enp131s0         DOWN           34:5a:60:88:34:5e <NO-CARRIER,BROADCAST,MULTICAST,UP> 
wlp132s0f0       UP             dc:97:ba:7f:03:af <BROADCAST,MULTICAST,UP,LOWER_UP> 
docker0          DOWN           16:45:80:8c:d3:27 <NO-CARRIER,BROADCAST,MULTICAST,UP> 
```

### IPv4 addresses

```text
$ if command -v ip >/dev/null 2>&1; then ip -4 addr show; elif command -v ifconfig >/dev/null 2>&1; then ifconfig -a | grep -E 'inet '; else echo 'MISSING: ip and ifconfig'; fi
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN group default qlen 1000
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
3: wlp132s0f0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UP group default qlen 1000
    inet 10.13.152.67/22 brd 10.13.155.255 scope global dynamic noprefixroute wlp132s0f0
       valid_lft 7135sec preferred_lft 7135sec
4: docker0: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 qdisc noqueue state DOWN group default 
    inet 172.17.0.1/16 brd 172.17.255.255 scope global docker0
       valid_lft forever preferred_lft forever
```

### default route

```text
$ if command -v ip >/dev/null 2>&1; then ip route show default; elif command -v route >/dev/null 2>&1; then route -n get default; elif command -v netstat >/dev/null 2>&1; then netstat -rn | sed -n '1,20p'; else echo 'MISSING: ip, route, and netstat'; fi
default via 10.13.155.254 dev wlp132s0f0 proto dhcp src 10.13.152.67 metric 600 
```

### Unitree subnet presence

```text
$ if command -v ip >/dev/null 2>&1; then ip -4 addr show; elif command -v ifconfig >/dev/null 2>&1; then ifconfig -a; else echo 'MISSING: ip and ifconfig'; fi | grep -E '192\.168\.(123|124)\.' || echo 'UNAVAILABLE: no 192.168.123/124 address detected'
UNAVAILABLE: no 192.168.123/124 address detected
```
- ping checks: `SKIPPED (pass --ping to run)`

## ROS2

- /opt/ros distros: `UNAVAILABLE`
- ROS_DISTRO: `UNSET`
- RMW_IMPLEMENTATION: `UNSET`
- CYCLONEDDS_URI: `UNSET`

### ros2

```text
path: MISSING
MISSING
```

### colcon

```text
path: /usr/bin/colcon
usage: colcon [-h] [--log-base LOG_BASE] [--log-level LOG_LEVEL]
              {build,extension-points,extensions,graph,info,list,metadata,mixin,test,test-result,version-check}
              ...
colcon: error: argument verb_name: invalid choice: '--version' (choose from 'build', 'extension-points', 'extensions', 'graph', 'info', 'list', 'metadata', 'mixin', 'test', 'test-result', 'version-check')
FAILED: exit 2
```

## Unitree Repositories


### unitree_ros2

- path: `/home/baoquanc/Downloads/WorkSpace/projects/third_party/unitree/unitree_ros2`
- exists: `FOUND`
- branch: `master`
- commit: `5204e6e`
- dirty status: `clean`
- remotes:
```text
origin	https://github.com/unitreerobotics/unitree_ros2 (fetch)
origin	https://github.com/unitreerobotics/unitree_ros2 (push)
```

### unitree_sdk2

- path: `/home/baoquanc/Downloads/WorkSpace/projects/third_party/unitree/unitree_sdk2`
- exists: `FOUND`
- branch: `main`
- commit: `63c6f53`
- dirty status: `clean`
- remotes:
```text
origin	https://github.com/unitreerobotics/unitree_sdk2 (fetch)
origin	https://github.com/unitreerobotics/unitree_sdk2 (push)
```

### unitree_sdk2_python

- path: `/home/baoquanc/Downloads/WorkSpace/projects/third_party/unitree/unitree_sdk2_python`
- exists: `FOUND`
- branch: `master`
- commit: `f7a5526`
- dirty status: `clean`
- remotes:
```text
origin	https://github.com/unitreerobotics/unitree_sdk2_python (fetch)
origin	https://github.com/unitreerobotics/unitree_sdk2_python (push)
```

### Unitree key files

- unitree_ros2/setup.sh: `FOUND` `/home/baoquanc/Downloads/WorkSpace/projects/third_party/unitree/unitree_ros2/setup.sh`
- unitree_ros2/setup_local.sh: `FOUND` `/home/baoquanc/Downloads/WorkSpace/projects/third_party/unitree/unitree_ros2/setup_local.sh`
- unitree_ros2/cyclonedds_ws/install/setup.bash: `MISSING` `/home/baoquanc/Downloads/WorkSpace/projects/third_party/unitree/unitree_ros2/cyclonedds_ws/install/setup.bash`
- unitree_sdk2/include/unitree: `FOUND` `/home/baoquanc/Downloads/WorkSpace/projects/third_party/unitree/unitree_sdk2/include/unitree`

## ROS2 Packages / Interfaces


### ros2 pkg prefix unitree_hg

```text
MISSING: ros2
```

### ros2 pkg prefix unitree_go

```text
MISSING: ros2
```

### ros2 pkg prefix unitree_api

```text
MISSING: ros2
```

### ros2 interface show unitree_hg/msg/LowCmd

```text
MISSING: ros2
```

### ros2 interface show unitree_hg/msg/LowState

```text
MISSING: ros2
```

### ros2 interface show unitree_hg/msg/MotorCmd

```text
MISSING: ros2
```

### ros2 interface show unitree_hg/msg/LowCmd_

```text
MISSING: ros2
```

### ros2 interface show unitree_hg/msg/LowState_

```text
MISSING: ros2
```

### ros2 interface show unitree_hg/msg/MotorCmd_

```text
MISSING: ros2
```

## Build Tools


### cmake

```text
path: /usr/bin/cmake
cmake version 3.28.3

CMake suite maintained and supported by Kitware (kitware.com/cmake).
```

### gcc

```text
path: /usr/bin/gcc
gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
Copyright (C) 2023 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

### g++

```text
path: /usr/bin/g++
g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
Copyright (C) 2023 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

### make

```text
path: /usr/bin/make
GNU Make 4.3
为 x86_64-pc-linux-gnu 编译
Copyright (C) 1988-2020 Free Software Foundation, Inc.
许可证：GPLv3+：GNU 通用公共许可证第 3 版或更新版本<http://gnu.org/licenses/gpl.html>。
本软件是自由软件：您可以自由修改和重新发布它。
在法律允许的范围内没有其他保证。
```

### python3

```text
path: /usr/bin/python3
Python 3.12.3
```

### pip3

```text
path: /usr/bin/pip3
pip 24.0 from /usr/lib/python3/dist-packages/pip (python 3.12)
```

### git

```text
path: /usr/bin/git
git version 2.43.0
```

## Runtime Libraries / Hardware


### ldconfig Unitree-related libs

```text
$ if command -v ldconfig >/dev/null 2>&1; then ldconfig -p 2>/dev/null | grep -E 'onnxruntime|cyclonedds|unitree' || echo 'UNAVAILABLE: no matching libs in ldconfig cache'; else echo 'MISSING: ldconfig'; fi
	libcycloneddsidl.so.0 (libc6,x86-64) => /usr/local/lib/libcycloneddsidl.so.0
	libcycloneddsidl.so (libc6,x86-64) => /usr/local/lib/libcycloneddsidl.so
```

### NVIDIA GPU

```text
$ if command -v nvidia-smi >/dev/null 2>&1; then nvidia-smi; else echo 'MISSING: nvidia-smi'; fi
Fri Jun  5 18:02:06 2026       
+-----------------------------------------------------------------------------------------+
| NVIDIA-SMI 580.142                Driver Version: 580.142        CUDA Version: 13.0     |
+-----------------------------------------+------------------------+----------------------+
| GPU  Name                 Persistence-M | Bus-Id          Disp.A | Volatile Uncorr. ECC |
| Fan  Temp   Perf          Pwr:Usage/Cap |           Memory-Usage | GPU-Util  Compute M. |
|                                         |                        |               MIG M. |
|=========================================+========================+======================|
|   0  NVIDIA GeForce RTX 5090 ...    Off |   00000000:02:00.0 Off |                  N/A |
| N/A   50C    P4             15W /   95W |      15MiB /  24463MiB |      0%      Default |
|                                         |                        |                  N/A |
+-----------------------------------------+------------------------+----------------------+

+-----------------------------------------------------------------------------------------+
| Processes:                                                                              |
|  GPU   GI   CI              PID   Type   Process name                        GPU Memory |
|        ID   ID                                                               Usage      |
|=========================================================================================|
|    0   N/A  N/A            3170      G   /usr/lib/xorg/Xorg                        4MiB |
+-----------------------------------------------------------------------------------------+
```

### USB / network command availability

```text
lsusb      /usr/bin/lsusb
lspci      /usr/bin/lspci
ip         /usr/sbin/ip
ifconfig   /usr/sbin/ifconfig
route      /usr/sbin/route
netstat    /usr/bin/netstat
nmcli      /usr/bin/nmcli
ethtool    /usr/sbin/ethtool
tcpdump    /usr/bin/tcpdump
ping       /usr/bin/ping
```

## A2 Package Readiness

- repo root candidate: `/home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real`
- A2 package dir: `/home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real/ros2/A2`
- ros2/A2/package.xml: `FOUND` `/home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real/ros2/A2/package.xml`
- ros2/A2/CMakeLists.txt: `FOUND` `/home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real/ros2/A2/CMakeLists.txt`
- recommended build command: `cd /home/baoquanc/Downloads/WorkSpace/projects/AliengoSim2Real/ros2 && colcon build --packages-select a2_lowlevel`

## Notes

- This report intentionally avoids dumping the full environment.
- Re-run with `--ping` only when the deploy machine is connected to the robot/network.
- Paste this Markdown report back to Codex when adjusting the A2 deployment chain.
