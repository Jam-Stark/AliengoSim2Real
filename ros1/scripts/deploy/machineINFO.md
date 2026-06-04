# 机器信息记录

## 旧机器（ROG 笔记本 — 迁移来源）

> 信息采集时间：2026-04-22

```
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
cmake version 3.22.1
Python 3.13.9
ROS2: humble
UNITREE_ROS2_SETUP: unset
unitree_setup: missing
OpenCV: 4.5.4
libudev: /lib/x86_64-linux-gnu/libudev.so.1
libjsoncpp: /lib/x86_64-linux-gnu/libjsoncpp.so.25
```

| 项目 | 值 |
|------|-----|
| 型号 | ROG Strix G16 G614FR |
| OS | Ubuntu 22.04.5 LTS (Jammy) |
| 内核 | x86_64 |
| GPU | NVIDIA GeForce RTX 5070 (12 GB) |
| 驱动 | 590.48.01 / CUDA 13.1 |
| g++ | 11.4.0 |
| cmake | 3.22.1 |
| Python | 3.13.9 |
| ROS2 | humble |
| OpenCV | 4.5.4 |

---

## 新机器（lt5 工作站 — 迁移目标）

> 迁移完成时间：2026-05-14 20:12 HKT

```
PRETTY_NAME="Ubuntu 24.04.3 LTS"
NAME="Ubuntu"
VERSION_ID="24.04"
VERSION="24.04.3 LTS (Noble Numbat)"
VERSION_CODENAME=noble
x86_64
Linux lt5.precognition.team 6.17.0-23-generic #23~24.04.1-Ubuntu SMP PREEMPT_DYNAMIC x86_64 GNU/Linux
Wed May 13 15:07:41 2026
+-----------------------------------------------------------------------------------------+
| NVIDIA-SMI 580.142                Driver Version: 580.142        CUDA Version: 13.0     |
+-----------------------------------------+------------------------+----------------------+
| GPU  Name                 Persistence-M | Bus-Id          Disp.A | Volatile Uncorr. ECC |
| Fan  Temp   Perf          Pwr:Usage/Cap |           Memory-Usage | GPU-Util  Compute M. |
|                                         |                        |               MIG M. |
|=========================================+========================+======================|
|   0  NVIDIA GeForce RTX 5090 ...    Off |   00000000:02:00.0 Off |                  N/A |
| N/A   54C    P4             21W /   95W |      15MiB /  24463MiB |      7%      Default |
|                                         |                        |                  N/A |
+-----------------------------------------------------------------------------------------+
Docker version 28.5.1, build e180ab8
nvidia-container-toolkit 1.17.8-1
cmake version 3.28.3
git version 2.43.0
```

| 项目 | 值 |
|------|-----|
| 主机名 | lt5.precognition.team |
| OS | Ubuntu 24.04.3 LTS (Noble) |
| 内核 | 6.17.0-23-generic, x86_64 |
| GPU | NVIDIA GeForce RTX 5090 (24 GB) |
| 驱动 | 580.142 / CUDA 13.0 |
| Docker | 28.5.1 |
| NVIDIA Container Toolkit | 1.17.8-1 |
| cmake | 3.28.3 |
| git | 2.43.0 |
| 用户组 | baoquanc (sudo, users, docker) |
| 工作目录 | ~/Downloads/WorkSpace/projects/ |

### 已部署的 Docker 容器

| 容器名 | 镜像 | 用途 |
|--------|------|------|
| `noetic-gpu` | `noetic-cpu:aliengo-deploy` (13.6 GB) | Aliengo ROS1 部署环境 |

