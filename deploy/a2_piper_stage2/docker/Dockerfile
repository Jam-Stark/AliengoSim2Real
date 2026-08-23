ARG A2_BASE_IMAGE=a2-humble-deploy:2026-06-05
FROM ${A2_BASE_IMAGE}

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

ARG TORCH_VERSION=2.7.0
ARG TORCH_INDEX_URL=https://download.pytorch.org/whl/cpu
ARG LIBTORCH_URL=https://download.pytorch.org/libtorch/cpu/libtorch-cxx11-abi-shared-with-deps-2.7.0%2Bcpu.zip
ARG NUMPY_VERSION=1.26.0
ARG PYYAML_VERSION=6.0.2

ENV DEBIAN_FRONTEND=noninteractive \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    RMW_IMPLEMENTATION=rmw_cyclonedds_cpp \
    STAGE2_ROOT=/opt/stage2 \
    STAGE2_WS=/opt/stage2_ws \
    Torch_DIR=/opt/libtorch-stage2/share/cmake/Torch \
    CMAKE_PREFIX_PATH=/opt/libtorch-stage2 \
    LD_LIBRARY_PATH=/opt/libtorch-stage2/lib

RUN apt-get update && apt-get install -y --no-install-recommends \
        libyaml-cpp-dev \
        python3-pip \
        ros-humble-diagnostic-msgs \
        ros-humble-sensor-msgs \
        ros-humble-std-msgs \
        ros-humble-std-srvs \
        ros-humble-trajectory-msgs \
    && rm -rf /var/lib/apt/lists/*

# Python Torch is used only by the offline parity/benchmark CLI.
RUN python3 -m pip install --no-cache-dir \
        --index-url "${TORCH_INDEX_URL}" \
        "torch==${TORCH_VERSION}" \
    && python3 -m pip install --no-cache-dir \
        "numpy==${NUMPY_VERSION}" \
        "PyYAML==${PYYAML_VERSION}" \
    && python3 -c 'import torch; assert torch.__version__.split("+")[0] == "2.7.0", torch.__version__'

# ROS2 C++ links the official cxx11-ABI LibTorch distribution. Do not point
# CMake at the Python wheel: Linux PyTorch wheels use a different C++ ABI.
RUN mkdir -p /tmp/libtorch-stage2-extract \
    && wget -q "${LIBTORCH_URL}" -O /tmp/libtorch-stage2.zip \
    && unzip -q /tmp/libtorch-stage2.zip -d /tmp/libtorch-stage2-extract \
    && test -f /tmp/libtorch-stage2-extract/libtorch/share/cmake/Torch/TorchConfig.cmake \
    && mv /tmp/libtorch-stage2-extract/libtorch /opt/libtorch-stage2 \
    && rm -f /tmp/libtorch-stage2.zip \
    && rmdir /tmp/libtorch-stage2-extract \
    && test -f /opt/libtorch-stage2/share/cmake/Torch/TorchConfig.cmake

# Install the offline policy contract/parity CLI without embedding the policy
# bundle itself. The real bundle is mounted read-only at /policy_bundle.
WORKDIR /opt/stage2/python
COPY deploy/a2_piper_stage2/pyproject.toml ./
COPY deploy/a2_piper_stage2/src ./src
RUN python3 -m pip install --no-cache-dir --no-deps .

# Build the already-deployed A2 LowCmd adapter and the direct Stage2 package in
# one immutable workspace. Tests stay disabled for this deployment image.
WORKDIR /opt/stage2_ws
COPY ros2/A2/CMakeLists.txt ros2/A2/package.xml ./src/a2_lowlevel/
COPY ros2/A2/include ./src/a2_lowlevel/include
COPY ros2/A2/src ./src/a2_lowlevel/src
COPY policy/A2_policy ./policy/A2_policy
COPY utils/cpp_manager_env ./utils/cpp_manager_env
COPY deploy/a2_piper_stage2/ros2/a2_piper_stage2_direct ./src/a2_piper_stage2_direct
COPY ros2/Piper/package.xml ros2/Piper/setup.py ros2/Piper/setup.cfg ./src/piper_bridge/
COPY ros2/Piper/resource ./src/piper_bridge/resource
COPY ros2/Piper/piper_bridge ./src/piper_bridge/piper_bridge
RUN source /opt/ros/humble/setup.bash \
    && source /opt/unitree/unitree_ros2/cyclonedds_ws/install/setup.bash \
    && colcon build --merge-install \
         --packages-select a2_lowlevel a2_piper_stage2_direct piper_bridge \
         --cmake-args \
           -DBUILD_TESTING=OFF \
           -DBUILD_A2_POLICY_DEPLOY=ON \
           -DCMAKE_BUILD_TYPE=Release \
           -DTorch_DIR=/opt/libtorch-stage2/share/cmake/Torch \
           -DA2_PIPER_STAGE2_DEFAULT_BUNDLE_PATH=/policy_bundle \
    && test -x /opt/stage2_ws/install/lib/a2_lowlevel/a2_policy_deploy \
    && test -x /opt/stage2_ws/install/lib/a2_piper_stage2_direct/a2_piper_stage2_direct \
    && test -x /opt/stage2_ws/install/lib/piper_bridge/piper_smoke_test

# Keep the proven A2 operator scripts intact and in their expected repository
# topology so the Stage2 gate runner can call them instead of duplicating A2
# LowState/LowCmd probing logic.
COPY ros2/A2/scripts /opt/stage2/ros2/A2/scripts
COPY ros2/A2/config /opt/stage2/ros2/A2/config
COPY deploy/a2_piper_stage2/scripts /opt/stage2/deploy/a2_piper_stage2/scripts
COPY deploy/a2_piper_stage2/docker/entrypoint.sh /opt/stage2/entrypoint.sh
RUN ln -s /opt/stage2_ws/install /opt/stage2/ros2/install \
    && ln -s /opt/stage2/ros2/A2/scripts /opt/stage2/a2_scripts \
    && ln -s /opt/stage2/ros2 /ros2 \
    && chmod +x /opt/stage2/entrypoint.sh \
       /opt/stage2/ros2/A2/scripts/*.sh \
       /opt/stage2/deploy/a2_piper_stage2/scripts/*.sh \
    && mkdir -p /evidence

WORKDIR /opt/stage2
ENTRYPOINT ["/opt/stage2/entrypoint.sh"]
CMD ["/bin/bash"]
