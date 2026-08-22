FROM ros:humble-ros-base-jammy

SHELL ["/bin/bash", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    can-utils \
    ethtool \
    git \
    iproute2 \
    iputils-ping \
    python3-colcon-common-extensions \
    python3-pip \
    python3-typing-extensions \
    ros-humble-diagnostic-msgs \
    ros-humble-launch-ros \
    ros-humble-rmw-cyclonedds-cpp \
    ros-humble-sensor-msgs \
    ros-humble-std-srvs \
    ros-humble-trajectory-msgs \
    && rm -rf /var/lib/apt/lists/*

ARG PIPER_SDK_REPO=https://github.com/krushell/piper_sdk.git
ARG PIPER_SDK_REF=master
RUN git clone --depth 1 --branch "${PIPER_SDK_REF}" "${PIPER_SDK_REPO}" /opt/piper_sdk \
    && python3 -m pip install --no-cache-dir /opt/piper_sdk

COPY . /opt/piper_ws/src/piper_bridge
RUN source /opt/ros/humble/setup.bash \
    && cd /opt/piper_ws \
    && colcon build --packages-select piper_bridge

COPY docker/entrypoint.sh /opt/piper/entrypoint.sh
RUN chmod +x /opt/piper/entrypoint.sh

ENV RMW_IMPLEMENTATION=rmw_cyclonedds_cpp \
    ROS_DOMAIN_ID=0 \
    PIPER_CAN_NAME=can0 \
    PIPER_NAMESPACE=piper

ENTRYPOINT ["/opt/piper/entrypoint.sh"]
CMD ["__bridge__"]
