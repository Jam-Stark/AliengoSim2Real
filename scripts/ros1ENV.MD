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





## Aliengo ROS1 部署链路 — 完成

[`ros1/docker/Dockerfile`](ros1/docker/Dockerfile) 已更新，与你之前成功构建的 `scripts/ros1ENV.MD` 方案保持一致，仅追加 LibTorch 下载。

### 最快上手路径（使用已有 noetic-gpu:2026-04 镜像）

**容器内安装 LibTorch（一次性操作）：**

```bash
cd /opt
wget -q "https://download.pytorch.org/libtorch/cu118/libtorch-cxx11-abi-shared-with-deps-2.1.0%2Bcu118.zip" -O libtorch.zip
unzip libtorch.zip && rm libtorch.zip
echo 'export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"' >> /root/.bashrc
echo 'export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"' >> /root/.bashrc
source /root/.bashrc
```

**编译 catkin workspace：**

```bash
source /opt/ros/noetic/setup.bash
mkdir -p /root/catkin_ws/src
ln -s /work/projects/unitree_ros_to_real/unitree_legged_msgs /root/catkin_ws/src/
ln -s /work/projects/unitree_ros_to_real/unitree_legged_real /root/catkin_ws/src/
ln -s /work/projects/unitree_ros_to_real/unitree_legged_sdk  /root/catkin_ws/src/
ln -s /work/projects/AliengoSim2Real/ros1                    /root/catkin_ws/src/aliengo_deploy
cd /root/catkin_ws
catkin_make -DCMAKE_BUILD_TYPE=Release
source devel/setup.bash
```

**运行：**

```bash
roslaunch aliengo_deploy aliengo_deploy.launch policy_path:=/path/to/your/policy/
```

如果编译遇到 `unitree_legged_sdk` 缺失，先在 `unitree_ros_to_real` 目录执行 `git submodule update --init --recursive`。
