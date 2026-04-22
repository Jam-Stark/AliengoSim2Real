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
# ~/docker/noetic-gpu/Dockerfile
FROM nvidia/cuda:11.8.0-cudnn8-devel-ubuntu20.04
SHELL ["/bin/bash", "-lc"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    curl gnupg2 lsb-release ca-certificates \
    build-essential cmake git vim \
    python3-pip python3-rosdep python3-catkin-tools python3-vcstool \
    libeigen3-dev libjsoncpp-dev libopencv-dev libudev-dev \
    && rm -rf /var/lib/apt/lists/*

RUN curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.asc | apt-key add -
RUN echo "deb http://packages.ros.org/ros/ubuntu focal main" > /etc/apt/sources.list.d/ros1.list

RUN apt-get update && apt-get install -y --no-install-recommends \
    ros-noetic-ros-base \
    ros-noetic-cv-bridge \
    ros-noetic-image-transport \
    ros-noetic-tf \
    ros-noetic-tf2-ros \
    ros-noetic-rviz \
    && rm -rf /var/lib/apt/lists/*

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

如果你要，我下一条直接给你 `docker-compose.yml` 版本。