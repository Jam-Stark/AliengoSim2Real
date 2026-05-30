# Docker 容器迁移指南：更换宿主机

将 Aliengo 部署环境从当前 ROG 笔记本迁移到新宿主机。

---

## 迁移前的理解

```
当前 ROG 笔记本
├── Docker 容器 noetic-gpu （内含 ROS Noetic + LibTorch CPU + catkin 编译产物）
│   ├── /opt/libtorch/              ← 在容器内 ✅ (docker commit 可保存)
│   ├── /opt/ros/noetic/            ← 在容器内 ✅
│   ├── /root/catkin_ws/build/      ← 在容器内 ✅
│   ├── /root/catkin_ws/devel/      ← 在容器内 ✅
│   ├── /root/.bashrc               ← 在容器内 ✅
│   └── /root/catkin_ws/src/
│       ├── aliengo_deploy          → /work/.../ros1      (软链接，指向 volume)
│       └── unitree_legged_msgs     → /work/...           (软链接，指向 volume)
│
└── 宿主机磁盘 ~/Downloads/WorkSpace/   （通过 -v 挂载为 /work）
    ├── projects/AliengoSim2Real/       ← 源码 + 策略文件 ❌ 不在容器内
    └── projects/unitree_ros_to_real/   ← ROS 消息包     ❌ 不在容器内
```

**核心问题**：`docker commit` 只保存容器层，不保存 volume mount 的内容。
源码和策略文件在宿主机上，需要单独备份。

---

## 第 1 步：旧机器上导出

### 1.1 提交容器为镜像

```bash
# 确保容器已停止（运行中也能 commit，但建议先停）
docker stop noetic-gpu

# 提交容器当前状态为新镜像
docker commit noetic-gpu noetic-cpu:aliengo-deploy

# 验证
docker images | grep noetic-cpu
# 应看到 noetic-cpu:aliengo-deploy，大小约 8-12 GB
```

### 1.2 导出镜像为 tar 文件

```bash
# 压缩导出（约 3-5 GB）
docker save noetic-cpu:aliengo-deploy | gzip > noetic-cpu-aliengo-deploy.tar.gz

# 如果磁盘空间紧张或想更快，不压缩:
# docker save noetic-cpu:aliengo-deploy -o noetic-cpu-aliengo-deploy.tar
```

### 1.3 备份宿主机上的源码和数据

```bash
# 方法 A: 用 git (推荐)
cd ~/Downloads/WorkSpace/projects/AliengoSim2Real
git add -A && git commit -m "migration backup"
git push

# 方法 B: 打包
cd ~/Downloads/WorkSpace
tar czf workspace-backup.tar.gz \
    projects/AliengoSim2Real/ \
    projects/unitree_ros_to_real/unitree_legged_msgs/
```

> ⚠️ `unitree_ros_to_real/` 下只需要 `unitree_legged_msgs/` 子目录。
> 其他子目录（`unitree_legged_real/`、`unitree_legged_sdk/`）在当前 TX2 relay 架构下不需要。

### 1.4 传输到新机器的文件清单

| 文件 | 大小估计 | 必需？ |
|------|----------|:------:|
| `noetic-cpu-aliengo-deploy.tar.gz` | 3-5 GB | ✅ |
| 源码（git 仓库或 `workspace-backup.tar.gz`） | 200-500 MB | ✅ |
| TX2 上的 relay 程序 | 已在 TX2 上，不需要迁移 | — |

传输方式：U 盘、SCP、rsync、NAS 均可。

---

## 第 2 步：新宿主机准备环境

### 2.1 安装 Docker

```bash
sudo apt-get update
sudo apt-get install -y docker.io
sudo usermod -aG docker $USER
# !! 必须注销重新登录，使 docker 组生效
```

### 2.2 安装 NVIDIA Container Toolkit

> 即使当前只用 CPU 推理，装上也没坏处。如果新机器完全没有 NVIDIA GPU，
> 跳过此步，启动容器时去掉 `--gpus all` 参数即可。

```bash
curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey | \
  sudo gpg --dearmor -o /usr/share/keyrings/nvidia-container-toolkit-keyring.gpg

curl -s -L https://nvidia.github.io/libnvidia-container/stable/deb/nvidia-container-toolkit.list | \
  sed 's#deb https://#deb [signed-by=/usr/share/keyrings/nvidia-container-toolkit-keyring.gpg] https://#g' | \
  sudo tee /etc/apt/sources.list.d/nvidia-container-toolkit.list

sudo apt-get update
sudo apt-get install -y nvidia-container-toolkit
sudo nvidia-ctk runtime configure --runtime=docker
sudo systemctl restart docker

# 验证
docker run --rm --gpus all nvidia/cuda:11.8.0-cudnn8-devel-ubuntu20.04 nvidia-smi
```

> **GPU 兼容性**：容器镜像基于 CUDA 11.8，但我们只用 LibTorch CPU 版。
> 新机器不管是什么 GPU / 什么 CUDA 驱动版本都没关系。

### 2.3 准备源码目录

```bash
# 方法 A: git clone
mkdir -p ~/Downloads/WorkSpace/projects
cd ~/Downloads/WorkSpace/projects
git clone <你的 AliengoSim2Real 仓库地址>
git clone https://github.com/unitreerobotics/unitree_ros_to_real.git

# 方法 B: 从备份恢复
mkdir -p ~/Downloads/WorkSpace
cd ~/Downloads/WorkSpace
tar xzf /path/to/workspace-backup.tar.gz
```

**最终目录结构必须是：**

```
~/Downloads/WorkSpace/          ← 这个路径可以不同，见下方说明
├── projects/
│   ├── AliengoSim2Real/
│   │   ├── ros1/              ← catkin 包源码
│   │   ├── policy/aliengo_new/ ← policy.pt 策略文件
│   │   ├── utils/             ← ManagerEnv 等共用代码
│   │   ├── scripts/           ← 文档
│   │   └── ...
│   └── unitree_ros_to_real/
│       └── unitree_legged_msgs/  ← ROS 消息定义
```

> **宿主机路径可以不同**！例如新机器上放在 `/home/newuser/workspace/`，
> 只需在 `docker run` 的 `-v` 参数中把宿主机侧路径改过来，
> 容器内路径始终保持 `/work` 不变。

---

## 第 3 步：导入镜像并启动

### 3.1 导入镜像

```bash
docker load < /path/to/noetic-cpu-aliengo-deploy.tar.gz

# 验证
docker images | grep noetic-cpu
# 应看到 noetic-cpu   aliengo-deploy   <hash>   约 13.6 GB
```

### 3.2 创建并启动容器

```bash
docker run -it \
  --name noetic-gpu \
  --hostname noetic-gpu \
  --gpus all \
  --network host \
  --ipc host \
  -v $HOME/Downloads/WorkSpace:/work \
  --device /dev/input:/dev/input \
  noetic-cpu:aliengo-deploy
```

> **参数调整说明**：
> - 没有 NVIDIA GPU → 去掉 `--gpus all`
> - 源码不在 `~/Downloads/WorkSpace` → 修改 `-v` 左边的路径
> - 不需要 USB 手柄 → 去掉 `--device /dev/input:/dev/input`
> - 已有同名容器 → 先 `docker rm noetic-gpu` 再创建

---

## 第 4 步：容器内验证

### 4.1 检查软链接

```bash
ls -la /root/catkin_ws/src/
```

预期输出：
```
aliengo_deploy -> /work/projects/AliengoSim2Real/ros1
unitree_legged_msgs -> /work/projects/unitree_ros_to_real/unitree_legged_msgs
```

如果两个链接 **目标存在且正确**（绿色显示），可以直接跳到 4.3。

### 4.2 如果软链接失效（红色显示 / 目标不存在）

```bash
# 删除旧链接
rm -f /root/catkin_ws/src/aliengo_deploy
rm -f /root/catkin_ws/src/unitree_legged_msgs

# 重新建立
ln -sf /work/projects/AliengoSim2Real/ros1 /root/catkin_ws/src/aliengo_deploy
ln -sf /work/projects/unitree_ros_to_real/unitree_legged_msgs /root/catkin_ws/src/

# 需要重新编译
cd /root/catkin_ws
rm -rf build devel
source /opt/ros/noetic/setup.bash
export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"
catkin_make -DCMAKE_BUILD_TYPE=Release
source devel/setup.bash
```

### 4.3 验证编译产物可用

```bash
source /opt/ros/noetic/setup.bash
source /root/catkin_ws/devel/setup.bash
export LD_LIBRARY_PATH="/opt/libtorch/lib:${LD_LIBRARY_PATH}"

# 检查包能找到
rospack find aliengo_deploy
# 应输出: /root/catkin_ws/src/aliengo_deploy

# 检查可执行文件存在
ls /root/catkin_ws/devel/lib/aliengo_deploy/
# 应看到: aliengo_deploy  fake_low_state_publisher  low_cmd_monitor

# 快速验证 LibTorch
python3 -c "import torch; print(torch.__version__)"
# 应输出: 2.1.0+cpu
```

### 4.4 静态测试（不需要实机）

按 [StaticTest.md](StaticTest.md) 操作，验证策略推理 + fake publisher 正常工作。

### 4.5 实机测试（连接 Aliengo 后）

```bash
# 验证网络连通
ping 192.168.123.12    # TX2 (relay)
ping 192.168.123.10    # Controller

# 按 ROS1TEST.md 步骤操作
```

---

## 常见问题

### Q: 新机器没有 NVIDIA GPU

完全没问题。当前使用 LibTorch CPU 版，不依赖 GPU。

```bash
docker run -it \
  --name noetic-gpu \
  --hostname noetic-gpu \
  --network host \
  --ipc host \
  -v $HOME/Downloads/WorkSpace:/work \
  noetic-cpu:aliengo-deploy
```

### Q: 编译报错 "could not find Torch"

```bash
export CMAKE_PREFIX_PATH="/opt/libtorch:${CMAKE_PREFIX_PATH}"
catkin_make -DCMAKE_BUILD_TYPE=Release
```

### Q: 导出的 tar.gz 文件太大 (> 5GB)

镜像包含完整的 CUDA 11.8 开发环境（约 6GB），即使我们只用 CPU。
如果想精简，可以在新机器上从零构建（见 [ros1ENV.MD](ros1ENV.MD)），
手动安装 LibTorch CPU + catkin_make。镜像会小很多（约 2-3 GB）。

### Q: 新机器是 ARM (aarch64)

当前镜像是 **x86_64** 架构，不能直接在 ARM 上运行。
需要在 ARM 机器上从 [ros1ENV.MD](ros1ENV.MD) 的 Dockerfile 重新构建。

### Q: catkin_ws 旧编译缓存还能用吗？

如果容器内路径和软链接都没变，旧的 `build/` `devel/` **可以直接用**，不需要重新编译。
但如果有任何路径变化，建议 `rm -rf build devel` 后重新编译（约 2 分钟）。

### Q: TX2 上的 relay 需要重新部署吗？

不需要。relay 运行在 Aliengo 板载 TX2 上，与你用哪台宿主机无关。
新机器只需能连到 192.168.123.x 网段（有线以太网）即可。

---

## 迁移清单

| # | 操作 | 位置 | 状态 |
|---|------|------|:----:|
| 1 | `docker commit noetic-gpu noetic-cpu:aliengo-deploy` | 旧机 | ✅ |
| 2 | `docker save ... \| gzip > noetic-cpu-aliengo-deploy.tar.gz` | 旧机 | ✅ |
| 3 | 备份源码 (`git push` 或 `tar`) | 旧机 | ✅ |
| 4 | 传输 tar.gz + 源码到新机器 | — | ✅ |
| 5 | 安装 Docker + NVIDIA Container Toolkit | 新机 | ✅ |
| 6 | `docker load < noetic-cpu-aliengo-deploy.tar.gz` | 新机 | ✅ |
| 7 | 准备源码目录结构 (放到 `/work` 挂载路径下) | 新机 | ✅ |
| 8 | `docker run -it --name noetic-gpu -v ...:/work ...` | 新机 | ✅ |
| 9 | 容器内验证软链接有效 (或重建 + 重编译) | 新机 | ✅ |
| 10 | 静态测试通过 (fake publisher) | 新机 | ✅ |
| 11 | 连 Aliengo 网络 + 实机测试 | 新机 | ⬜ |

---

## 迁移完成记录

> **迁移日期**：2026-05-14 20:12 HKT
> **来源**：ROG Strix G16 (Ubuntu 22.04, RTX 5070)
> **目标**：lt5.precognition.team (Ubuntu 24.04, RTX 5090)

### 目标机器环境

| 项目 | 值 |
|------|-----|
| OS | Ubuntu 24.04.3 LTS (Noble) |
| 内核 | 6.17.0-23-generic, x86_64 |
| GPU | NVIDIA GeForce RTX 5090 (24 GB), 驱动 580.142, CUDA 13.0 |
| Docker | 28.5.1 |
| NVIDIA Container Toolkit | 1.17.8-1 |
| cmake | 3.28.3 |
| 用户 | baoquanc (docker 组) |

### 迁移过程记录

1. **镜像导入**：`docker load < noetic-cpu-aliengo-deploy.tar.gz` → 镜像名 `noetic-cpu:aliengo-deploy` (13.6 GB)
2. **启动容器**：
   ```bash
   docker run -it --name noetic-gpu --hostname noetic-gpu \
     --gpus all --network host --ipc host \
     -v $HOME/Downloads/WorkSpace:/work \
     noetic-cpu:aliengo-deploy
   ```
3. **软链接需修复**：旧容器内的软链接指向 `/work/AliengoSim2Real/ros1`（无 `projects/` 中间目录），新机器实际路径为 `/work/projects/AliengoSim2Real/ros1`。需重建软链接：
   ```bash
   rm -f /root/catkin_ws/src/aliengo_deploy
   rm -f /root/catkin_ws/src/unitree_legged_msgs
   rm -f /root/catkin_ws/src/unitree_legged_real
   rm -f /root/catkin_ws/src/unitree_legged_sdk
   ln -sf /work/projects/AliengoSim2Real/ros1 /root/catkin_ws/src/aliengo_deploy
   ln -sf /work/projects/unitree_ros_to_real/unitree_legged_msgs /root/catkin_ws/src/
   ```
   > **根因**：旧 ROG 笔记本上源码放在 `~/Downloads/WorkSpace/AliengoSim2Real/`（无 `projects/`），镜像 commit 时保存了那时的软链接路径。新机器按规范使用 `~/Downloads/WorkSpace/projects/AliengoSim2Real/`，路径不匹配。`unitree_legged_real` 和 `unitree_legged_sdk` 在新架构下不需要，已删除。
4. **重新编译**：路径变化后旧 `build/`/`devel/` 缓存不可用，`rm -rf build devel && catkin_make` 重新编译（约 2 分钟），编译成功。
5. **静态测试通过**：`roslaunch aliengo_deploy test_deploy.launch` + `fake_low_state_publisher`，策略使能后 Kp 正常变非零，NaN 计数为 0，无异常。

### 待完成

- [ ] 连接 Aliengo 网络 (192.168.123.x) 进行实机测试（见 [ROS1TEST.md](ROS1TEST.md)）
