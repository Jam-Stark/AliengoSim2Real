#!/bin/bash
# ============================================================
# Aliengo ROS1 RL Policy Deployment Startup Script
#
# Usage:
#   ./start_aliengo_deploy.sh [policy_path]
#
# Prerequisites:
#   1. Aliengo is connected via Ethernet (192.168.123.10)
#   2. Aliengo is powered on and in LOW-LEVEL mode
#   3. Protection frame is attached for first tests
#   4. catkin workspace is built and sourced
# ============================================================

set -e

# Source ROS environment
if [ -f /opt/ros/noetic/setup.bash ]; then
    source /opt/ros/noetic/setup.bash
fi

# Source catkin workspace
CATKIN_WS="${CATKIN_WS:-/root/catkin_ws}"
if [ -f "${CATKIN_WS}/devel/setup.bash" ]; then
    source "${CATKIN_WS}/devel/setup.bash"
else
    echo "[ERROR] catkin workspace not found at ${CATKIN_WS}"
    echo "        Build first: cd ${CATKIN_WS} && catkin_make"
    exit 1
fi

# Default policy path
POLICY_PATH="${1:-}"

echo "============================================"
echo "  Aliengo ROS1 RL Policy Deployment"
echo "============================================"
echo ""
echo "  Checklist:"
echo "    [1] Aliengo connected via Ethernet (192.168.123.10)"
echo "    [2] Aliengo powered ON, LOW-LEVEL mode"
echo "    [3] Protection frame attached"
echo "    [4] Remote controller powered ON"
echo ""
echo "  Controls:"
echo "    A button  -> Enable policy (start walking)"
echo "    B button  -> Controlled stop (stand -> crouch)"
echo "    L2+B      -> Emergency damping stop"
echo "    Start     -> Clear velocity command"
echo "    Select    -> Reset policy state"
echo "    Sticks    -> Velocity command [vx, vy, wz]"
echo ""
echo "============================================"

if [ -n "$POLICY_PATH" ]; then
    echo "  Policy path: $POLICY_PATH"
else
    echo "  Policy path: (default from CMake)"
fi
echo ""

read -p "Press Enter to start (Ctrl+C to abort)... "

# Launch
if [ -n "$POLICY_PATH" ]; then
    roslaunch aliengo_deploy aliengo_deploy.launch \
        policy_path:="$POLICY_PATH"
else
    roslaunch aliengo_deploy aliengo_deploy.launch
fi
