# Safety and operations contract

本文件定义软件与操作边界，不构成safety-rated certification。A2/PiPER实机始终需要独立physical emergency stop、支撑装置、现场operator和逐Gate批准。

## Operating modes

| Mode | Robot state | Hardware command | Current availability |
| --- | --- | --- | --- |
| offline validate/benchmark | disconnected | none | available |
| synthetic mock shadow | disconnected | none | available |
| direct dry-run/shadow | connected, output gates false | none | executable；site receipt pending |
| direct live | connected | A2 existing LowCmd path + PiPER semantic command | gated command exists；hardware acceptance pending |

`safety.output_enabled=false` 是site default。Live同时要求：site没有`TO_VERIFY`、operator手动设output flag为true、matching component approval receipt、`STAGE2_ALLOW_LIVE=1`、显式`--live`和前序Gates通过。任一条件缺失都fail-fast。

## Command safety boundary

完整action stages必须分别可观测：

```text
raw actor output
 -> nominal target: default + 0.25 * raw
 -> LMP model limit
 -> site/manufacturer hard limit
 -> previous-limited-target rate limit
 -> named semantic target
 -> bridge conversion
 -> hardware controller
```

Bundle key `final_joint_target_rad` 只到nominal stage。Arm reference `arm_j5` nominal target超出LMP range，说明hard limiting是实际会改变数值的stage。不得用nominal parity强迫limited target相等，也不得为了通过parity跳过limit。

Policy runtime的`last_action`缓存raw actor output；rate limiter缓存previous limited target。两个state不能共用。

## Local watchdog ownership

Policy host负责：

- snapshot completeness与finite checks；
- A2/PiPER state age；
- cross-source skew；
- inference deadline与consecutive miss；
- observation/action logging；
- 停止发送semantic commands。

A2 direct node与existing low-level adapter负责：

- direct snapshot freshness与fixed training-to-low-level mapping；
- low-level mode/CRC/PD path；
- remote Select immediate stop与L2+B controlled-down；
- process/network loss后的实际hardware行为仍必须在Fault Gate现场确认。

PC2 PiPER bridge负责：

- CAN唯一ownership；
- fresh command gate；
- command timeout与feedback timeout；
- local quick stop和fault latch；
- explicit resume后仍保持command gate关闭，直到新command。

Policy host process停止或网络中断时，靠近hardware的bridge必须自行进入已验证状态。Laptop停止发布不是完整watchdog。

## Fault response table

以下response在现场验证前均为`TO_VERIFY`，不得用表格当作已实现事实：

| Fault | Detection owner | Required site decision |
| --- | --- | --- |
| A2 state stale | direct node + A2 adapter | refuse publish、hold/stop、timeout、recovery |
| PiPER state stale | policy host + PC2 | PC2 quick stop/latch behavior |
| A2/PiPER skew | policy host | suppress tick、hold/stop threshold |
| inference deadline miss | policy host | single miss policy、consecutive limit |
| policy host/network loss | A2 control domain + PC2 | local timeout behavior |
| non-finite observation/action | policy host | no publish + latched stop/hold decision |
| joint target limit/rate hit | policy host | clip/log/escalation threshold |
| bridge/SDK/CAN fault | local bridge | stop、disable、resume sequence |

禁止静默补零、复用旧状态或自动降低检查来保持运行。故障应暴露并留下明确日志。

## Hold, stop, disable, emergency stop

这些概念必须现场分别定义：

- `hold`：执行器保持哪个target、使用哪些gain、允许多久；
- `stop`：受控停止还是quick stop，机械结果是什么；
- `disable`：何时去除驱动使能，机器人是否会下落；
- `emergency stop`：独立hardware mechanism，不能依赖ROS service；
- `recovery`：检查条件、人工动作、是否需要重新stand/initialize。

软件 `/piper/stop` 或A2 zero/raw command都不应被称为safety-rated emergency stop。

## Start procedure

每次session至少执行：

1. 确认physical E-stop、支撑与工作区；
2. 确认只有一个A2 command owner和一个PiPER CAN owner；
3. 记录site config与software versions；
4. 运行offline parity/host check；
5. 运行read-only state/freshness checks；
6. 确认前序Gate和operator approval；
7. 从最低允许output mode开始。

代码存在不等于Gate通过；当前session只允许进入`stage2_gate.sh status`显示的下一步。

## Stop and recovery procedure

停止时按现场已验证顺序：停止policy publish、确认local bridge进入hold/stop、必要时执行hardware E-stop、确认无active command traffic、保存logs/config。恢复必须重新检查state freshness、joint range、fault latch和operator approval；不得因process或network重连自动恢复live。

## Logging

Shadow/live日志至少包含：

- snapshot receipt/source times、age、skew；
- commands和gait phase；
- raw dog/arm outputs与arm plan；
- nominal、hard-limited、rate-limited named targets；
- limit/rate hit indicators；
- inference latency/deadline state；
- bridge diagnostics、watchdog/fault transitions；
- operator start/stop/approval events。

日志不能替代硬件急停或现场观察，但必须足以区分policy问题、semantic mapping问题和bridge/hardware问题。
