
### 📍 远程机宿主机 terminal（提示符类似 `dreams@dreams-ROG:~$`）

```bash
# 1. 进入 WorkSpace 目录
cd ~/Downloads/WorkSpace

# 2. clone unitree_ros_to_real（如果之前已 clone 可跳过）
git clone https://github.com/unitreerobotics/unitree_ros_to_real.git

# 3. 初始化 SDK submodule (在宿主机做不会有 safe.directory 问题)
cd unitree_ros_to_real
git submodule update --init --recursive
# 如果这步也失败，用方案B:
#   rm -rf unitree_legged_sdk
#   git clone https://github.com/unitreerobotics/unitree_legged_sdk.git
cd ..

# 4. 如果 SDK 之前已经独立 clone 在 ~/Downloads/WorkSpace/unitree_legged_sdk，
#    直接拷贝到 unitree_ros_to_real 里面:
#    cp -r unitree_legged_sdk unitree_ros_to_real/unitree_legged_sdk

# 5. 验证
ls unitree_ros_to_real/unitree_legged_msgs/msg/LowCmd.msg
ls unitree_ros_to_real/unitree_legged_real/src/exe/ros_udp.cpp
ls unitree_ros_to_real/unitree_legged_sdk/CMakeLists.txt
ls AliengoSim2Real/ros1/CMakeLists.txt
# 以上四个 ls 都应该有输出

# 6. 启动容器（去掉 --rm 以便保持安装的 LibTorch）
docker run -it --name noetic-gpu --gpus all --network host \
  -v $HOME/Downloads/WorkSpace:/work \
  noetic-gpu:2026-04
```

---

### 📍 Docker 容器内（提示符 `root@noetic-gpu:/work#`）

```bash
# 7. 验证挂载
ls /work/
# 应该看到: AliengoSim2Real  unitree_ros_to_real  (和其他文件)

# 8. 安装 LibTorch（只做一次）
cd /opt
wget -q "https://download.pytorch.org/libtorch/cu118/libtorch-cxx11-abi-shared-with-deps-2.1.0%2Bcu118.zip" -O libtorch.zip
unzip -q libtorch.zip && rm libtorch.zip
export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"
export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"

# 9. 创建 catkin workspace + 软链接
source /opt/ros/noetic/setup.bash
mkdir -p /root/catkin_ws/src
ln -sf /work/unitree_ros_to_real/unitree_legged_msgs /root/catkin_ws/src/
ln -sf /work/unitree_ros_to_real/unitree_legged_real /root/catkin_ws/src/
ln -sf /work/unitree_ros_to_real/unitree_legged_sdk  /root/catkin_ws/src/
ln -sf /work/AliengoSim2Real/ros1                    /root/catkin_ws/src/aliengo_deploy

# 10. 编译
cd /root/catkin_ws
catkin_make -DCMAKE_BUILD_TYPE=Release

# 11. 验证
source devel/setup.bash
rospack find aliengo_deploy
rospack find unitree_legged_real
```

代码准备好在宿主机上、submodule 初始化在宿主机上（避免权限问题），其他全在容器里。
