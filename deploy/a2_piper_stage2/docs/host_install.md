# Policy host安装与配置

完整的新手步骤见 [operator_runbook_zh_CN.md](operator_runbook_zh_CN.md)。本页只说明安装脚本的真实边界。

## 基线

```text
host: Ubuntu 22.04 x86_64
container: ROS 2 Humble + Cyclone DDS + host networking
offline Python: CPU Torch 2.7.0
direct C++: official cxx11 ABI CPU LibTorch 2.7.0
```

当前只有CPU build path；`build_container.sh --cuda`会明确拒绝。容器不安装Isaac Sim/Isaac Lab。PiPER SDK仍只在PC2，A2 Unitree环境来自已验证A2 base image。

## 一步安装Docker

```bash
./scripts/bootstrap_policy_host_ubuntu.sh
```

该脚本仅接受 Ubuntu 22.04 amd64，使用Docker官方apt repository安装Engine与Compose v2。它会先列出并移除Docker官方文档指定的冲突package，但不删除`/var/lib/docker`中的image/container/volume；最后用`sudo docker run hello-world`核实。首次PASS后logout/login，再运行：

```bash
./scripts/bootstrap_policy_host_ubuntu.sh --verify-only
```

精确PASS：

```text
[PASS] Docker Engine, Compose v2, daemon access, and hello-world are ready.
```

## 生成配置

脚本要求operator明确给出NIC、host IPv4/CIDR和ROS domain：

```bash
./scripts/configure_policy_host.sh \
  --iface enp131s0 \
  --host-ip 192.168.123.222/24 \
  --domain-id 0 \
  --site "$PWD/config/site.mock.yaml"
```

它写`docker/.env`，但默认不改network。只有显式`--apply-network`才执行`sudo ip link/ip addr replace`。这是当前启动周期的配置，重启后要重新执行 connected 配置与检查；脚本不擅自修改 NetworkManager/netplan 持久配置。`--force`只用于operator已经review旧`.env`后明确覆盖。

## Build与offline verification

```bash
./scripts/check_policy_host.sh
./scripts/build_container.sh
./scripts/run_shadow.sh mock
```

精确PASS分别以这些文本结尾：

```text
[PASS] ...
[PASS] built Stage2 image: ...
[PASS] shadow sequence completed; evidence: ...
```

Image内同时构建`a2_lowlevel`与`a2_piper_stage2_direct`。Python Torch只执行offline parity；C++ actor使用独立official cxx11 ABI LibTorch，不能把Python ABI当成C++ build依据。
Compose 将 `OMP_NUM_THREADS=1` 与 `MKL_NUM_THREADS=1` 固定为 export benchmark 使用的单线程 CPU 条件；目标 host 仍必须以实测 p50/p95/p99/max 判定 deadline margin。

## Connected检查

改成direct site后：

```bash
cp config/site.template.yaml config/site.yaml
./scripts/configure_policy_host.sh \
  --iface enp131s0 \
  --host-ip 192.168.123.222/24 \
  --domain-id 0 \
  --site "$PWD/config/site.yaml" \
  --force
./scripts/check_policy_host.sh --connected
./scripts/probe_policy_host_read_only.sh
./scripts/probe_pc2_read_only.sh --ssh <user>@192.168.123.162
./scripts/probe_ros_graph_read_only.sh
```

这些probe只收集OS/network/ROS evidence，不enable、不resume、不发布A2/PiPER command。能够build image不证明NIC、DDS、PC2 CAN、joint mapping、watchdog或hardware Gate通过。
