用这个版本就行，目标是 `Ubuntu 22.04 宿主机 + Docker + GPU 透传 + ROS1 Noetic(Focal) 容器`。我把它固定成 `CUDA 11.8 + Ubuntu 20.04`，这样和 `Noetic` 的平台更一致。

**1. 宿主机安装 GPU 容器支持**

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

**2. 构建 Noetic GPU 镜像**

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
    build-essential cmake git vim \
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

**3. 启动 GPU 容器**

如果你需要 OpenCV/GUI 显示，宿主机先执行：

```bash
xhost +si:localuser:root
mkdir -p ~/.ros
```

然后启动：

```bash
docker run -it --rm \
  --name noetic-gpu \
  --hostname noetic-gpu \
  --gpus all \
  --network host \
  --ipc host \
  -e DISPLAY=$DISPLAY \
  -e QT_X11_NO_MITSHM=1 \
  -e NVIDIA_DRIVER_CAPABILITIES=all \
  -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
  -v $HOME/.ros:/root/.ros \
  -v $HOME/Downloads/WorkSpace:/work \
  --device /dev/input:/dev/input \
  --device /dev/bus/usb:/dev/bus/usb \
  noetic-gpu:2026-04
```

**4. 容器内验证**

```bash
source /opt/ros/noetic/setup.bash
nvidia-smi
roscore
```

如果以后你的代码已经迁成 ROS1 `catkin` 包，再在容器内这样编：

```bash
source /opt/ros/noetic/setup.bash
mkdir -p /root/catkin_ws/src
ln -s /work/go2w_sim2sim /root/catkin_ws/src/go2w_sim2sim
cd /root/catkin_ws
rosdep update
rosdep install --from-paths src --ignore-src -r -y --rosdistro noetic
catkin_make -DCMAKE_BUILD_TYPE=Release
source devel/setup.bash
```

当前要注意一点：你这个仓库现在还是 ROS2，不是 ROS1，所以这个 GPU 容器先解决的是“Noetic + CUDA 环境可用”，不是“当前代码直接能编”。

来源：

- NVIDIA Container Toolkit: https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/1.18.1/install-guide.html
- `nvidia/cuda:11.8.0-cudnn8-devel-ubuntu20.04`: https://hub.docker.com/layers/nvidia/cuda/11.8.0-cudnn8-devel-ubuntu20.04/images/
- `ros:noetic-ros-base-focal`: https://hub.docker.com/layers/library/ros/noetic-ros-base-focal/images/
- Noetic EOL: https://www.ros.org/blog/noetic-eol/




已创建 [`ros1/scripts/setup_and_build.sh`](ros1/scripts/setup_and_build.sh) 完整搭建指南。以下是你在远程机上要执行的操作，按顺序来：

---

### 步骤 1: 启动你已有的 Docker 容器

在远程机宿主机上运行：

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

> ⚠️ 注意去掉了 `--rm`，这样容器内安装的 LibTorch 不会丢失。后续用 `docker start -i noetic-gpu` 重新进入。

---

### 步骤 2: 容器内安装 LibTorch

进入容器后执行：

```bash
cd /opt
wget -q "https://download.pytorch.org/libtorch/cu118/libtorch-cxx11-abi-shared-with-deps-2.1.0%2Bcu118.zip" -O libtorch.zip
unzip -q libtorch.zip && rm libtorch.zip
echo 'export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"' >> /root/.bashrc
echo 'export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"' >> /root/.bashrc
source /root/.bashrc
```

---

### 步骤 3: 初始化 unitree_legged_sdk

```bash
cd /work/projects/unitree_ros_to_real
git submodule update --init --recursive
# 如果失败则手动 clone:
# rm -rf unitree_legged_sdk
# git clone https://github.com/unitreerobotics/unitree_legged_sdk.git
```

---

### 步骤 4: 创建 catkin workspace 并编译

```bash
source /opt/ros/noetic/setup.bash

mkdir -p /root/catkin_ws/src
ln -sf /work/projects/unitree_ros_to_real/unitree_legged_msgs /root/catkin_ws/src/
ln -sf /work/projects/unitree_ros_to_real/unitree_legged_real /root/catkin_ws/src/
ln -sf /work/projects/unitree_ros_to_real/unitree_legged_sdk  /root/catkin_ws/src/
ln -sf /work/projects/AliengoSim2Real/ros1                    /root/catkin_ws/src/aliengo_deploy

cd /root/catkin_ws
catkin_make -DCMAKE_BUILD_TYPE=Release
source devel/setup.bash
```

---

### 步骤 5: 验证

```bash
rospack find aliengo_deploy
rospack find unitree_legged_real
```

两个命令都应输出包路径。编译产物在 `/root/catkin_ws/devel/lib/` 下。

---

如果编译出错，把错误信息发给我，我来定位修复。