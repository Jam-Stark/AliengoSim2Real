FROM ros:humble-ros-base-jammy

SHELL ["/bin/bash", "-lc"]

ENV DEBIAN_FRONTEND=noninteractive
ENV UNITREE_ROOT=/opt/unitree
ENV UNITREE_ROS2_ROOT=/opt/unitree/unitree_ros2
ENV UNITREE_SDK2_ROOT=/opt/unitree/unitree_sdk2
ENV UNITREE_SDK2_PYTHON_ROOT=/opt/unitree/unitree_sdk2_python
ENV RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
ENV Torch_DIR=/opt/libtorch/share/cmake/Torch
ENV CMAKE_PREFIX_PATH=/opt/libtorch:${CMAKE_PREFIX_PATH:-}
ENV LD_LIBRARY_PATH=/opt/libtorch/lib:${LD_LIBRARY_PATH:-}
ENV PATH=/opt/unitree/unitree_sdk2/install/bin:/opt/unitree/unitree_sdk2/build/bin:${PATH}

ARG UNITREE_ROS2_REF=5204e6e
ARG UNITREE_SDK2_REF=63c6f53
ARG UNITREE_SDK2_PYTHON_REF=f7a5526
ARG LIBTORCH_URL="https://download.pytorch.org/libtorch/cpu/libtorch-cxx11-abi-shared-with-deps-2.1.0%2Bcpu.zip"

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash-completion \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    dnsutils \
    ethtool \
    git \
    iproute2 \
    iputils-ping \
    less \
    libasio-dev \
    libjsoncpp-dev \
    libyaml-cpp-dev \
    lsb-release \
    net-tools \
    procps \
    python3-colcon-common-extensions \
    python3-pip \
    python3-rosdep \
    python3-vcstool \
    ros-humble-rmw-cyclonedds-cpp \
    ros-humble-rosidl-generator-dds-idl \
    tcpdump \
    tmux \
    unzip \
    vim \
    wget \
    && rm -rf /var/lib/apt/lists/*

RUN rosdep init 2>/dev/null || true

RUN mkdir -p "${UNITREE_ROOT}" && \
    git clone https://github.com/unitreerobotics/unitree_ros2.git "${UNITREE_ROS2_ROOT}" && \
    git -C "${UNITREE_ROS2_ROOT}" checkout "${UNITREE_ROS2_REF}" && \
    git clone https://github.com/unitreerobotics/unitree_sdk2.git "${UNITREE_SDK2_ROOT}" && \
    git -C "${UNITREE_SDK2_ROOT}" checkout "${UNITREE_SDK2_REF}" && \
    git clone https://github.com/unitreerobotics/unitree_sdk2_python.git "${UNITREE_SDK2_PYTHON_ROOT}" && \
    git -C "${UNITREE_SDK2_PYTHON_ROOT}" checkout "${UNITREE_SDK2_PYTHON_REF}"

RUN cd /opt && \
    wget -q "${LIBTORCH_URL}" -O libtorch.zip && \
    unzip -q libtorch.zip && \
    rm -f libtorch.zip && \
    test -f /opt/libtorch/share/cmake/Torch/TorchConfig.cmake

# Humble uses apt-provided CycloneDDS/RMW by default. Source CycloneDDS builds are
# intentionally not part of the default image because Unitree ROS2 messages build
# cleanly against ros-humble-rmw-cyclonedds-cpp on Jammy.
RUN source /opt/ros/humble/setup.bash && \
    cd "${UNITREE_ROS2_ROOT}/cyclonedds_ws" && \
    colcon build --merge-install \
      --packages-select unitree_go unitree_hg unitree_api \
      --cmake-args -DBUILD_TESTING=OFF

RUN cd "${UNITREE_SDK2_ROOT}" && \
    cmake -S . -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_EXAMPLES=ON \
      -DCMAKE_INSTALL_PREFIX="${UNITREE_SDK2_ROOT}/install" && \
    cmake --build build --parallel "$(nproc)" && \
    cmake --install build && \
    mkdir -p "${UNITREE_SDK2_ROOT}/install/bin" && \
    find "${UNITREE_SDK2_ROOT}/build/bin" -maxdepth 1 -type f -perm -111 \
      -exec cp -a {} "${UNITREE_SDK2_ROOT}/install/bin/" \;

COPY entrypoint.sh /opt/a2/entrypoint.sh
COPY build_a2_workspace.sh /opt/a2/build_a2_workspace.sh
RUN chmod +x /opt/a2/entrypoint.sh /opt/a2/build_a2_workspace.sh && \
    echo 'source /opt/a2/entrypoint.sh >/dev/null 2>&1 || true' >> /root/.bashrc

WORKDIR /work/projects/AliengoSim2Real/ros2
ENTRYPOINT ["/opt/a2/entrypoint.sh"]
CMD ["/bin/bash"]
