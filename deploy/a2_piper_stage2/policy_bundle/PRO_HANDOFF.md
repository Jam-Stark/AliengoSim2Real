# Stage2 policy handoff

## Result

- Authoritative checkpoint：`runner_state_020000.pt`
- Embedded iteration：`20000`
- Embedded total timesteps：`1,966,080,000`
- Deployment scheduler state：hybrid open
- Online models：dog actor + arm actor；runner不是第三个 actor

## Validation evidence

- Saved TorchScript vs checkpoint actor，batch 3：dog/arm max absolute error `0.0`
- Reference vs direct checkpoint final action：dog/arm max absolute error `4.768e-7`
- arm plan→dog preview、29+1 dog history、plan postprocess：max absolute error `0.0`
- projected gravity matrix/component/frame：max absolute error `0.0`
- LMP MuJoCo 20 s locomotion/position-tracking：simulation runtime pass；不是 hardware pass

## Not included

- Training runner checkpoint、critic、optimizer、Gaussian std
- Isaac Sim/Isaac Lab runtime
- Hardware credentials、site config、CAN config
- Live enablement

## Unresolved downstream fact

用户描述的 GeneralSim2Real Stage2 schema 在交付时未出现在公开 remote/bundle mount，因此 downstream parser validation 未执行。LMP 字段本身已由 checkpoint/source/parity 固定。
