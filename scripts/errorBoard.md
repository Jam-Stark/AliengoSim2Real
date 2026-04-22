
从错误日志中看出两个问题：

### 问题 1: 远程机上只有 `unitree_legged_sdk`，没有 `unitree_ros_to_real`

从日志第 2 行看到 `/work/` 下是：

```
AliengoSim2Real/
unitree_legged_sdk/     ← 这只是 SDK 库，不是完整的 ROS 包
```

但我们需要的是 `unitree_ros_to_real` 这个**完整仓库**，它里面包含：

- `unitree_legged_msgs/` — ROS 消息定义（必须）
- `unitree_legged_real/` — ros_udp bridge 节点（必须）
- `unitree_legged_sdk/` — SDK 库

**修复**：在远程机**宿主机**上执行：

```bash
cd ~/Downloads/WorkSpace
git clone https://github.com/unitreerobotics/unitree_ros_to_real.git
cd unitree_ros_to_real
git submodule update --init --recursive
```

### 问题 2: 软链接路径错误

你用了 `/work/projects/...`（带 projects），但实际路径是 `/work/...`。

**修复**：重新进入容器后，**先清理旧的坏链接**，再用正确路径：

```bash
# 清理
rm -f /root/catkin_ws/src/unitree_legged_msgs
rm -f /root/catkin_ws/src/unitree_legged_real
rm -f /root/catkin_ws/src/unitree_legged_sdk
rm -f /root/catkin_ws/src/aliengo_deploy

# 添加 git safe directory（解决 dubious ownership 问题）
git config --global --add safe.directory '*'

# 用正确路径重新链接
ln -sf /work/unitree_ros_to_real/unitree_legged_msgs /root/catkin_ws/src/
ln -sf /work/unitree_ros_to_real/unitree_legged_real /root/catkin_ws/src/
ln -sf /work/unitree_ros_to_real/unitree_legged_sdk  /root/catkin_ws/src/
ln -sf /work/AliengoSim2Real/ros1                    /root/catkin_ws/src/aliengo_deploy

# 验证链接是否有效
ls -la /root/catkin_ws/src/
ls /root/catkin_ws/src/unitree_legged_msgs/msg/   # 应能看到 LowCmd.msg 等
ls /root/catkin_ws/src/aliengo_deploy/CMakeLists.txt  # 应能看到文件

# 重新编译
cd /root/catkin_ws
rm -rf build devel   # 清除上次空编译的产物
source /opt/ros/noetic/setup.bash
catkin_make -DCMAKE_BUILD_TYPE=Release
```

### 总结：先在宿主机 clone `unitree_ros_to_real`，再进容器用正确路径链接和编译。
