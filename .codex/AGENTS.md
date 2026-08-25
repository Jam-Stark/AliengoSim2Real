# `.codex` adapter

Root `../AGENTS.md` is canonical. Read `.codex/TEAM.md` only for role routing、P2P、parallelism or optional persistent coordination.

FAST work does not require a team. Ordinary STANDARD spawns use concise prompt contracts and do not require disk-backed task state.

Project models、effort、concurrency and role definitions remain in `.codex/config.toml` and `.codex/agents/*.toml`; this pack does not own them.

Optional hooks are permissive while coordination is inactive and validate only managed tasks when active. `PreToolUse` no-op success must emit no stdout; deny must use the event-specific permission decision without `continue`. All repo-local hook commands resolve from the Git root. Strict parsing/root/validator failures fail closed. `PostToolUse` stores metadata only, and `SessionStart` consumes pending events once before archiving them. Review changed hook definitions with `/hooks`.
