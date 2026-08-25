<!-- managed-by: jam-coding-role; file: SCIENTIFIC_ENGINEERING.md -->
# Scientific Software and Robotics Extension

本文件用于 ML/RL、simulation、robotics、benchmark、causal probe 和长时实验。它扩展 `ROLE.md`，不替代项目安全手册。跨阶段方案可按需读取 `.ai/STAGE_DECISION.md`，阶段产出交接可按需读取 `.ai/ARTIFACT_HANDOFF.md`。

## 1. 先注册问题，不先注册答案

运行前至少明确：

```text
QUESTION: 要区分什么？
CLAIM CLASS: descriptive / predictive / causal / deployment
INTERVENTION: 改了什么，保持什么不变？
BASELINE: 与谁比较？
UNIT / TIMEBASE: 样本、episode、control step、physics step、秒或实机 trial？
EVENT: 在哪个物理或任务事件上测量？
METRIC: 直接因变量是什么？
POPULATION / DENOMINATOR: 哪些样本有资格进入统计？
ADMISSION / STOPPING: 何时可解释，何时停止？
ARTIFACTS: code、config、checkpoint、log、eval、render 的来源与路径。
```

没有预先定义的 denominator、event 或 metric 时，不把 post-hoc pattern 升级为强结论。

## 2. 分开 intent、realization 与 outcome

每次实验至少区分：

1. **Intended config**：希望施加的条件；
2. **Realized telemetry**：仿真或硬件实际经历的状态、力、接触、动作和参数；
3. **Outcome**：任务结果和因变量。

分桶和因果解释优先依据统一单位下的 realized telemetry。intended bucket 只说明抽样意图，不能替代实际暴露。

## 3. Claim 必须测在正确事件上

行为要求发生在 `crossing`，就不能只测 `release`；要求持续接触，就不能只测瞬时最大值。每轮汇总必须直接包含本轮因变量，而不是只展示容易变好的 proxy。

当 success rate 饱和或缺乏区分力时，换用与问题直接相关的 mechanics/quality metric，不从天花板指标推断策略等价。

## 4. Admission gate 先证明自己有效

- evaluation population 必须先在 easy、sham 或 known baseline 上复现已知体征；
- 参数域必须有已验证的 magnitude anchor；量级相近不等于物理等价；
- 依赖 derived quantity 的 gate，必须先证明该量的单位、单调性、可识别性和计算路径；
- policy-relative probe 不得被描述为环境固有属性；
- gate/reducer 只能执行预先授权的判据，不能看到结果后改变终局；
- admission 规则不能结构性排除“假设为真时本应出现”的样本。发现这种 coupling 时，改用 policy-free scene gate、独立 population 或明确降级结论。

## 5. Causal claim 要绑定干预边界

报告因果结果时写清：

- exact state restore、matched prefix 还是只匹配配置；
- intervention horizon；
- policy/checkpoint 与 observation history；
- randomization、seed、side、load、contact regime；
- sample size 和 missing/censored records；
- 可推广范围与明确不能推广的范围。

短窗 simulation intervention 不能自动证明长期 policy adaptation、实机力矩能力或跨 embodiment 泛化。

## 6. Source lock 与 artifact provenance

每个 formal run/analysis 至少可追溯到：

- repository revision / worktree；
- resolved config；
- checkpoint identity 与 lineage；
- command、seed、device/resource；
- output root 与完成状态；
- evaluator/reducer version；
- 关键 artifact 是否真实存在并可读。

不要根据文件名猜 checkpoint 或配置；不要把静态配置存在当作训练完成。缺失 artifact 是事实，不用 fallback 数据补齐。

## 7. 时间、单位与控制边界

- 明确 policy/control frequency、physics substeps、sensor frequency 和记录频率；
- streak、horizon、delay、filter 和 intervention 应以其语义所属的 timebase 定义；
- 所有跨模块张量、角度、力矩、长度和时间在边界处显式声明单位；
- 度/弧度、world/base frame、left/right handedness、in/out sign 等转换必须有单一 canonical implementation 与验证样例。

## 8. 负结果与不确定性是有效终局

允许并准确使用：

- `SUPPORTED`
- `NOT_SUPPORTED`
- `INCONCLUSIVE`
- `NOT_ADMITTED`
- `UNRESOLVED`
- `NOT_RUN`

不要为了“有结论”调 gate、换 denominator、忽略 missing data 或把 descriptive finding 改写成 causal pass。高信息量负结果应进入 memory，说明它排除了什么、没有排除什么。

## 9. Sim-to-real 与硬件安全

- simulation evidence 和 hardware evidence 分开；
- 不从 nominal effort limit、simulated force 或 controller command 推断真实安全裕量；
- 实机前先完成项目规定的机械、电气、急停、负载、工作空间和人员隔离检查；
- 硬件异常、松动、失配、过热或未知噪声/振动出现时停止，不以软件 fallback 继续；
- 所有实机动作遵守设备官方文档和项目 risk assessment。

## 10. 推荐实验闭环

```text
question -> source/measurement audit -> preregistered probe
-> baseline-vitals admission -> formal run
-> realized-telemetry classification -> registered reducer
-> claim-bounded conclusion -> durable memory
```

这个闭环可以很小；严谨来自 claim 与 evidence 对齐，不来自文件数量或仪式。
