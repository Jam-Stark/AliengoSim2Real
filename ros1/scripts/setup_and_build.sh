#!/bin/bash
# ============================================================
# Aliengo ROS1 部署 — 远程机完整搭建步骤
#
# 前提条件：
#   1. 已按 scripts/ros1ENV.MD 构建了 noetic-gpu:2026-04 镜像
#   2. 本仓库 (AliengoSim2Real) 和 unitree_ros_to_real 在同级目录
#      即 ~/Downloads/WorkSpace/projects/ 下有：
#        AliengoSim2Real/
#        unitree_ros_to_real/
#
# 使用方式：按顺序执行每个步骤（不要直接 bash 整个脚本）
# ============================================================

echo "========================================================"
echo "  步骤 0: 确认镜像存在"
echo "========================================================"
echo ""
echo "运行以下命令确认镜像已构建："
echo ""
echo "  docker images | grep noetic-gpu"
echo ""
echo "应该看到 noetic-gpu:2026-04"
echo ""

echo "========================================================"
echo "  步骤 1: 启动 Docker 容器"
echo "========================================================"
echo ""
echo "直接复制运行以下命令（宿主机上执行）："
echo ""
cat << 'DOCKER_CMD'
# 如果需要 GUI 显示（可选）
xhost +si:localuser:root 2>/dev/null || true

# 启动容器（注意用 --name 方便后续操作）
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
DOCKER_CMD
echo ""
echo "进入容器后，继续执行后续步骤。"
echo ""

echo "========================================================"
echo "  步骤 2: 容器内 — 安装 LibTorch（只需做一次）"
echo "========================================================"
echo ""
cat << 'LIBTORCH_CMD'
# 检查是否已安装
if [ -d /opt/libtorch ]; then
  echo "LibTorch 已存在，跳过安装"
else
  echo "正在下载 LibTorch (CUDA 11.8)..."
  cd /opt
  wget -q "https://download.pytorch.org/libtorch/cu118/libtorch-cxx11-abi-shared-with-deps-2.1.0%2Bcu118.zip" -O libtorch.zip
  unzip -q libtorch.zip
  rm -f libtorch.zip
  echo "LibTorch 安装完成: /opt/libtorch"
fi

# 设置环境变量（每次进容器都需要，或写入 .bashrc）
export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"
export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"

# 写入 .bashrc 使其持久化（容器用 --rm 启动时无效）
grep -q 'libtorch' /root/.bashrc || {
  echo 'export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"' >> /root/.bashrc
  echo 'export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"' >> /root/.bashrc
}
LIBTORCH_CMD
echo ""
echo "⚠️  注意：如果容器用 --rm 启动，每次重新进入都要重新安装。"
echo "    建议之后改用 docker commit 保存安装后的状态，或去掉 --rm。"
echo ""

echo "========================================================"
echo "  步骤 3: 容器内 — 初始化 unitree_legged_sdk"
echo "========================================================"
echo ""
cat << 'SDK_CMD'
cd /work/projects/unitree_ros_to_real

# 方式1: 如果是 git submodule
git submodule update --init --recursive 2>/dev/null

# 方式2: 如果 submodule 失败，手动 clone
if [ ! -f unitree_legged_sdk/CMakeLists.txt ]; then
  echo "submodule 为空，尝试手动 clone..."
  rm -rf unitree_legged_sdk
  git clone https://github.com/unitreerobotics/unitree_legged_sdk.git
  echo "请确认 SDK 版本与你的 Aliengo 固件匹配！"
fi

ls unitree_legged_sdk/
SDK_CMD
echo ""

echo "========================================================"
echo "  步骤 4: 容器内 — 创建 catkin workspace 并编译"
echo "========================================================"
echo ""
cat << 'BUILD_CMD'
source /opt/ros/noetic/setup.bash

# LibTorch 环境变量（如果步骤2已写入 .bashrc 可省略）
export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"
export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"

# 创建 workspace
mkdir -p /root/catkin_ws/src

# 软链接所有包（如果已存在会跳过）
ln -sf /work/projects/unitree_ros_to_real/unitree_legged_msgs /root/catkin_ws/src/unitree_legged_msgs
ln -sf /work/projects/unitree_ros_to_real/unitree_legged_real /root/catkin_ws/src/unitree_legged_real
ln -sf /work/projects/unitree_ros_to_real/unitree_legged_sdk  /root/catkin_ws/src/unitree_legged_sdk
ln -sf /work/projects/AliengoSim2Real/ros1                    /root/catkin_ws/src/aliengo_deploy

# 编译
cd /root/catkin_ws
catkin_make -DCMAKE_BUILD_TYPE=Release 2>&1 | tee /tmp/build.log

# 检查编译结果
if [ $? -eq 0 ]; then
  echo ""
  echo "✅ 编译成功！"
  source devel/setup.bash
  echo "已 source devel/setup.bash"
else
  echo ""
  echo "❌ 编译失败，查看 /tmp/build.log"
fi
BUILD_CMD
echo ""

echo "========================================================"
echo "  步骤 5: 容器内 — 验证节点可以启动"
echo "========================================================"
echo ""
cat << 'VERIFY_CMD'
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash
export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"

# 检查节点是否可找到
rospack find aliengo_deploy
rospack find unitree_legged_real

# 列出可执行文件
ls -la /root/catkin_ws/devel/lib/aliengo_deploy/
ls -la /root/catkin_ws/devel/lib/unitree_legged_real/
VERIFY_CMD
echo ""

echo "========================================================"
echo "  步骤 6: 连接 Aliengo 并运行（实机部署时）"
echo "========================================================"
echo ""
cat << 'RUN_CMD'
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash
export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"

# 确认网络连接（Aliengo 默认 IP: 192.168.123.10）
ping -c 2 192.168.123.10

# 方式 A: 使用 launch 文件一键启动 (ros_udp + deploy node)
roslaunch aliengo_deploy aliengo_deploy.launch \
  policy_path:=/work/projects/AliengoSim2Real/policy/your_policy/

# 方式 B: 分两个终端手动启动
# 终端 1:
#   roslaunch unitree_legged_real real.launch ctrl_level:=lowlevel
# 终端 2:
#   rosrun aliengo_deploy aliengo_deploy policy_path=/work/projects/AliengoSim2Real/policy/your_policy/
RUN_CMD
echo ""
echo "========================================================"
echo "  操作说明"
echo "========================================================"
echo ""
echo "  遥控器 A 键 → 使能策略（开始运动）"
echo "  遥控器 B 键 → 受控停止（站立→卧倒）"
echo "  L2 + B      → 紧急阻尼制动"
echo "  Start       → 清零速度指令"
echo "  Select      → 重置策略状态"
echo "  左摇杆      → 前进/后退 + 转向"
echo "  右摇杆      → 侧移"
echo ""
echo "  ⚠️  首次实机测试务必使用保护架悬挂机器人！"
echo ""
