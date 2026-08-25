# SDK and runtime matrix

本矩阵区分 repository code facts、已记录环境 evidence 和现场 `TO_VERIFY`。首次部署不替换已经验证过的 A2/PiPER 底层路径。

| Domain | Expected stack | Responsibility | Current evidence | Remaining verification |
| --- | --- | --- | --- | --- |
| Policy host | m45 Ubuntu 24.04 host；Ubuntu 22.04 container、ROS 2 Humble、Cyclone DDS、Python Torch + C++ LibTorch | offline parity；direct dual-policy inference、obs/action、logs、supervision | target image build、bundle parity、benchmark、500 tick mock shadow PASS | NIC/DDS；site与hardware Gates |
| A2 control domain | existing `ros2/A2`, `unitree_hg`, `A2LowLevelInterface` composed by direct node | raw LowState/LowCmd、mapping、CRC、mode、PD | main成功path、mapping与API是repository facts | same-A2 attitude/mapping reconfirmation、real watchdog/hold/stop Gate |
| A2 PC2 / PiPER | PC2 Ubuntu 22.04 RT + ROS 2 Humble；planned `ros2/Piper` + selected `krushell/piper_sdk` + SocketCAN | `/piper/*` semantics、USB-CAN ownership、SDK conversion、local stop | PC2 host/network/USB-CAN read-only inventory complete；USB recognized as `can0` | install/image/SDK；CAN UP/bitrate/feedback；firmware与motion |
| Development/reference | official Unitree/PiPER docs, LMP bundle | interface and source-contract comparison | bundle metadata included | source revision and resolved training env unavailable |

## Policy host versions

Export environment receipt：

```text
Python 3.11.15
Torch 2.7.0+cu128
NumPy 1.26.0
PyYAML 6.0.2
Linux x86_64 / glibc 2.39
```

CPU deployment：

```text
Python 3.10
pip 25.1.1
Torch 2.7.0+cpu
NumPy 1.26.0
PyYAML 6.0.2
C++ LibTorch 2.7.0 cxx11 ABI CPU
```

Export Torch install-source provenance明确unavailable。m45目标host已通过Python CPU wheel parity；C++ direct runtime使用official cxx11-ABI CPU LibTorch 2.7.0。两者runtime library path保持隔离；当前不提供CUDA direct build。

## A2 stack

现有 A2 memory 记录的 Unitree reference revisions为：

```text
unitree_ros2       5204e6e
unitree_sdk2       63c6f53
unitree_sdk2_python f7a5526
```

这些是现有A2 deployment evidence，不自动证明现场仍运行同一版本。Site AI必须重新记录实际image。Stage2正式C++ node直接组合A2 adapter，因此raw `/lowstate`/`/lowcmd`是预期boundary，不需要新增named endpoint。已锁定raw order、training mapping、`wxyz` extraction与PD；仍需现场复核joint direction/zero、projected gravity、receipt age/skew和fault/stop行为。

## PiPER stack

Repository contract：

```text
/piper/joint_states   sensor_msgs/msg/JointState
/piper/joint_command  trajectory_msgs/msg/JointTrajectory
/piper/diagnostics    diagnostic_msgs/msg/DiagnosticArray
/piper/resume         std_srvs/srv/Trigger
/piper/enable         std_srvs/srv/Trigger
/piper/stop           std_srvs/srv/Trigger
/piper/disable        std_srvs/srv/Trigger
```

Command 为一个 point、`arm_j1..arm_j6`、absolute radians。Repository defaults 是 `50 Hz` control、`0.20 s` command timeout、`0.50 s` feedback timeout。Gripper interface absent。

实机已经确认USB-CAN为`1d50:606f`、driver`gs_usb`、serial与kernel path`1-6:1.0`；当前`can0 DOWN/STOPPED`，并且没有bridge。以上仍不能证明：

- PC2 当前使用的 SDK source URL、branch/tag 与工作区状态；
- USB-CAN 已枚举并绑定 `can_piper`；
- 1 Mbit/s CAN 配置生效；
- firmware/API 与 bridge 兼容；
- enable、command、timeout、stop、resume 在硬件上有效。

## Upgrade rule

首次部署保持现有验证版本。不得仅因官方仓库有更新就替换 PC2 SDK、改变 CAN 参数或改写 A2 raw path。升级需要独立 branch、interface diff、offline build 和重新执行对应硬件 Gate。
