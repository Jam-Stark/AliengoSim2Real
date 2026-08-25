# Project AI entrypoint

System、developer、Owner/user 指令优先。本文件是路由表，不要求每次任务全量读取所有 `.ai/*` 文档。

## Minimal core

Read `.ai/ROLE.md`, `.ai/PROJECT.md`, `.ai/WORKFLOW.md`, then the minimum relevant project memory and actual source/config/runtime path.

## Conditional documents

- runtime-specific tools -> `.ai/RUNTIME_ADAPTERS.md` and the matching adapter;
- multiple writers、exclusive resources、cross-session state、formal review/QA -> `.ai/TEAM_STATE.md`;
- durable memory candidate or classification repair -> `.ai/MEMORY_GOVERNANCE.md`;
- long run -> `.ai/LONG_RUNNING_TASKS.md`;
- ML/RL/simulation/robotics claim -> `.ai/SCIENTIFIC_ENGINEERING.md`;
- Owner-selected stage planning -> `.ai/STAGE_DECISION.md`;
- Owner-requested/declared stage handoff -> `.ai/ARTIFACT_HANDOFF.md`.

## Routes

- FAST: simple QA、temporary test、clear small change; Main directly, no persistent facilities.
- STANDARD: ordinary implementation/debugging; 0–3 agents as useful, P2P allowed, no disk contract by default.
- HIGH_RISK: destructive/external/hardware/hard-to-reverse/unapproved expensive work; brief Owner and wait for approval.

Main owns scope、acceptance、write/resource authority、Git、external writes and final integration. Git commit/push require current explicit authorization. Do not activate ledger、freeze、curator or artifact handoff merely because those tools exist.
