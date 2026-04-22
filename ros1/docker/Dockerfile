# ============================================================
# Docker image for Aliengo ROS1 RL Policy Deployment
#
# Base: CUDA 11.8 + Ubuntu 20.04
# ROS:  Noetic
# ML:   LibTorch 2.1.0+cu118
#
# Build:
#   cd ros1/docker
#   docker build -t aliengo-noetic-gpu:latest .
#
# Run:
#   docker run -it --rm \
#     --name aliengo-deploy \
#     --gpus all \
#     --network host \
#     --ipc host \
#     -v $HOME/Downloads/WorkSpace:/work \
#     --device /dev/input:/dev/input \
#     aliengo-noetic-gpu:latest
# ============================================================

FROM nvidia/cuda:11.8.0-cudnn8-devel-ubuntu20.04
SHELL ["/bin/bash", "-lc"]

# Avoid interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# ---- System packages ----
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl gnupg2 lsb-release ca-certificates wget unzip \
    build-essential cmake git vim \
    python3-pip python3-rosdep python3-catkin-tools python3-vcstool \
    libeigen3-dev libjsoncpp-dev libopencv-dev libudev-dev \
    && rm -rf /var/lib/apt/lists/*

# ---- ROS Noetic ----
RUN curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.asc | apt-key add -
RUN echo "deb http://packages.ros.org/ros/ubuntu focal main" > /etc/apt/sources.list.d/ros1.list

RUN apt-get update && apt-get install -y --no-install-recommends \
    ros-noetic-ros-base \
    ros-noetic-cv-bridge \
    ros-noetic-image-transport \
    ros-noetic-tf \
    ros-noetic-tf2-ros \
    && rm -rf /var/lib/apt/lists/*

RUN rosdep init || true
RUN echo "source /opt/ros/noetic/setup.bash" >> /root/.bashrc

# ---- LibTorch (CUDA 11.8) ----
RUN cd /opt && \
    wget -q "https://download.pytorch.org/libtorch/cu118/libtorch-cxx11-abi-shared-with-deps-2.1.0%2Bcu118.zip" -O libtorch.zip && \
    unzip libtorch.zip && \
    rm -f libtorch.zip

ENV CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"
ENV LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"

# ---- Workspace ----
WORKDIR /work

# ---- Default entrypoint ----
CMD ["/bin/bash"]
