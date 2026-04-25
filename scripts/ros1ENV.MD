# Aliengo ROS1 部署环境搭建指南

目标：`Ubuntu 22.04 宿主机 + Docker + GPU 透传 + ROS1 Noetic (Focal) 容器`。
基于 `CUDA 11.8 + Ubuntu 20.04`，与 Noetic 的平台一致。

> **注意**：当前策略推理使用 **LibTorch CPU 版**，因为 ROG 笔记本的 RTX 5070 + CUDA 12.x 驱动
> 与 CUDA 11.8 版 LibTorch 不兼容。50Hz 的 MLP 推理在 CPU 上完全够用。

---

## 1. 宿主机安装 GPU 容器支持

```bash
sudo apt-get update
sudo apt-get install -y ca-certificates curl gnupg

curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey | \
sudo gpg --dearmor -o /usr/share/keyrings/nvidia-container-toolkit-keyring.gpg

curl -s -L https://nvidia.github.io/libnvidia-container/stable/deb/nvidia-container-toolkit.list | \
sed 's#deb https://#deb [signed-by=/usr/share/keyrings/nvidia-container-toolkit-keyring.gpg] https://#g' | \
sudo tee /etc/apt/sources.list.d/nvidia-container-toolkit.list

sudo apt-get update
sudo apt-get install -y nvidia-container-toolkit
sudo nvidia-ctk runtime configure --runtime=docker
sudo systemctl restart docker

docker run --rm --gpus all nvidia/cuda:11.8.0-cudnn8-devel-ubuntu20.04 nvidia-smi
```

## 2. 构建 Noetic GPU 镜像

```bash
mkdir -p ~/docker/noetic-gpu
cd ~/docker/noetic-gpu
```

```dockerfile
FROM nvidia/cuda:11.8.0-cudnn8-devel-ubuntu20.04
SHELL ["/bin/bash", "-lc"]

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y tzdata

SHELL ["/bin/bash", "-lc"]

# 基础工具
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl gnupg2 lsb-release ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# ROS1 源（Noetic -> focal）
RUN curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.asc | apt-key add -
RUN echo "deb http://packages.ros.org/ros/ubuntu focal main" > /etc/apt/sources.list.d/ros1.list

# 其余依赖
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git vim wget unzip \
    python3-pip python3-rosdep python3-vcstool \
    libeigen3-dev libjsoncpp-dev libopencv-dev libudev-dev \
    ros-noetic-ros-base \
    ros-noetic-cv-bridge \
    ros-noetic-image-transport \
    ros-noetic-tf \
    ros-noetic-tf2-ros \
    ros-noetic-rviz \
    && rm -rf /var/lib/apt/lists/*

# catkin_tools 用 pip 更稳
RUN pip3 install --no-cache-dir catkin-tools

RUN rosdep init || true
RUN echo "source /opt/ros/noetic/setup.bash" >> /root/.bashrc

WORKDIR /work
```

```bash
docker build -t noetic-gpu:2026-04 .
```

## 3. 启动 GPU 容器

```bash
docker run -it \
  --name noetic-gpu \
  --hostname noetic-gpu \
  --gpus all \
  --network host \
  --ipc host \
  -v $HOME/Downloads/WorkSpace:/work \
  --device /dev/input:/dev/input \
  noetic-gpu:2026-04
```

> ⚠️ 去掉了 `--rm`，这样容器内安装的 LibTorch 不会丢失。后续用 `docker start -i noetic-gpu` 重新进入。
> 使用 `--network host` 以便容器内直接访问 192.168.123.x 网段。

## 4. 容器内安装 LibTorch (CPU 版)

```bash
cd /opt
wget -q "https://download.pytorch.org/libtorch/cpu/libtorch-cxx11-abi-shared-with-deps-2.1.0%2Bcpu.zip" -O libtorch.zip
unzip -q libtorch.zip && rm libtorch.zip
echo 'export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"' >> /root/.bashrc
echo 'export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"' >> /root/.bashrc
source /root/.bashrc
```

> **为什么用 CPU 版？** RTX 5070 需要 CUDA 12.x，但 Docker 镜像基于 CUDA 11.8。
> CUDA 版 LibTorch 要求 CUDA runtime 版本匹配，CPU 版无此限制。
> 50Hz MLP 推理在 CPU 上延迟 < 1ms，完全满足要求。

## 5. 创建 catkin workspace 并编译

```bash
source /opt/ros/noetic/setup.bash

mkdir -p /root/catkin_ws/src

# 软链接依赖包和部署包
ln -sf /work/projects/unitree_ros_to_real/unitree_legged_msgs /root/catkin_ws/src/
ln -sf /work/projects/AliengoSim2Real/ros1                    /root/catkin_ws/src/aliengo_deploy

cd /root/catkin_ws
export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"
catkin_make -DCMAKE_BUILD_TYPE=Release
source devel/setup.bash
```

> **路径说明**：
> - 宿主机 `~/Downloads/WorkSpace` → 容器内 `/work`
> - 项目在 `/work/projects/AliengoSim2Real/`
> - unitree_legged_msgs 在 `/work/projects/unitree_ros_to_real/unitree_legged_msgs/`
> - `ros1/` 目录通过软链接挂载为 `aliengo_deploy` 包名

## 6. 验证

```bash
rospack find aliengo_deploy
# 应输出: /root/catkin_ws/src/aliengo_deploy

rospack find unitree_legged_msgs
# 应输出: /root/catkin_ws/src/unitree_legged_msgs
```

## 7. ROS 网络配置（容器内）

每次进入容器后需要设置（或写入 `.bashrc`）：

```bash
export ROS_MASTER_URI=http://127.0.0.1:11311
export ROS_HOSTNAME=127.0.0.1
echo "127.0.0.1 $(hostname)" >> /etc/hosts 2>/dev/null
```

> **为什么需要这个？** Docker `--network host` 模式下，hostname 不一定能被 DNS 解析。
> 不设置会导致 ROS node 之间无法通信（一直等待连接）。

## 8. 日常使用

### 重新进入已有容器

```bash
docker start -i noetic-gpu
```

### 在容器内打开第二个终端

```bash
docker exec -it noetic-gpu bash
```

### 编译更新

```bash
cd /root/catkin_ws
source /opt/ros/noetic/setup.bash
export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"
catkin_make -DCMAKE_BUILD_TYPE=Release
source devel/setup.bash
```

---

## 参考来源

- NVIDIA Container Toolkit: https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/
- `nvidia/cuda:11.8.0-cudnn8-devel-ubuntu20.04`: https://hub.docker.com/layers/nvidia/cuda/
- `ros:noetic-ros-base-focal`: https://hub.docker.com/layers/library/ros/noetic-ros-base-focal/
- Noetic EOL: https://www.ros.org/blog/noetic-eol/
- LibTorch CPU: https://pytorch.org/get-started/locally/
