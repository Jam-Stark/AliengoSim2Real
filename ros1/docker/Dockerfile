# ============================================================
# Aliengo ROS1 RL Policy Deployment Docker Image
#
# 基于 scripts/ros1ENV.MD 中已验证通过的 Noetic GPU 镜像方案，
# 追加 LibTorch 推理依赖。
#
# 方案 A（推荐）: 直接在已有 noetic-gpu:2026-04 容器内安装 LibTorch
#   docker exec -it noetic-gpu bash
#   cd /opt && wget -q "https://download.pytorch.org/libtorch/cu118/libtorch-cxx11-abi-shared-with-deps-2.1.0%2Bcu118.zip" -O libtorch.zip && unzip libtorch.zip && rm libtorch.zip
#   echo 'export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"' >> /root/.bashrc
#   echo 'export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"' >> /root/.bashrc
#
# 方案 B: 从头构建完整镜像（使用此 Dockerfile）
#   cd ros1/docker && docker build -t aliengo-deploy:latest .
# ============================================================

FROM nvidia/cuda:11.8.0-cudnn8-devel-ubuntu20.04
SHELL ["/bin/bash", "-lc"]

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y tzdata

SHELL ["/bin/bash", "-lc"]

# ---- 基础工具 ----
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl gnupg2 lsb-release ca-certificates wget unzip \
    && rm -rf /var/lib/apt/lists/*

# ---- ROS1 源 (Noetic -> focal) ----
RUN curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.asc | apt-key add -
RUN echo "deb http://packages.ros.org/ros/ubuntu focal main" > /etc/apt/sources.list.d/ros1.list

# ---- 其余依赖（包含 ROS 包，必须在 ROS 源添加之后） ----
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git vim \
    python3-pip python3-rosdep python3-vcstool \
    libeigen3-dev libjsoncpp-dev libopencv-dev libudev-dev \
    ros-noetic-ros-base \
    ros-noetic-cv-bridge \
    ros-noetic-image-transport \
    ros-noetic-tf \
    ros-noetic-tf2-ros \
    && rm -rf /var/lib/apt/lists/*

# catkin_tools 用 pip 安装更稳定
RUN pip3 install --no-cache-dir catkin-tools

RUN rosdep init || true
RUN echo "source /opt/ros/noetic/setup.bash" >> /root/.bashrc

# ---- LibTorch (CUDA 11.8, cxx11 ABI) ----
RUN cd /opt && \
    wget -q "https://download.pytorch.org/libtorch/cu118/libtorch-cxx11-abi-shared-with-deps-2.1.0%2Bcu118.zip" -O libtorch.zip && \
    unzip libtorch.zip && \
    rm -f libtorch.zip

ENV CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"
ENV LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"

WORKDIR /work
CMD ["/bin/bash"]
