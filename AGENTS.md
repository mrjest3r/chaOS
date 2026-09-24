# chaOS — Agent entrypoint

Read these **before** exploring the tree. Do not re-derive architecture from source unless the task requires a specific file.

| Doc | Purpose |
|-----|---------|
| [`docs/agent/MAP.md`](docs/agent/MAP.md) | Neural/knowledge map: vision, arch API, key files, invariants |
| [`docs/agent/ROADMAP.md`](docs/agent/ROADMAP.md) | Step-by-step plan agents must follow (x86 cyberdeck Path A) |
| [`docs/agent/STATUS.md`](docs/agent/STATUS.md) | Live progress — **update when a step completes** |
| [`docs/agent/ABI.md`](docs/agent/ABI.md) | Syscall ABI for translators / ulib |
| [`docs/agent/BRINGUP.md`](docs/agent/BRINGUP.md) | Physical board BIOS bring-up (P3.1) |
| [`README.md`](README.md) | Human docs (commands, build, features) |

## Session protocol

1. Read `STATUS.md` → know current step and blockers.
2. Read only the **current step** section in `ROADMAP.md` (and MAP sections it cites).
3. Implement that step; run `make test` when kernel/user behavior changes.
4. Update `STATUS.md` (checkboxes + one-line notes). Do not rewrite the whole roadmap.
5. Prefer editing existing modules over new subsystems unless the roadmap step says otherwise.

## Non-negotiables

- Target remains **x86** (deck will be an x86 board). No ARM port unless STATUS says so.
- Path A corner-cuts: languages via **host translators / cross-compile**, not in-OS JVM/CPython.
- Do not commit/push unless the user asks.
- Keep changes scoped to the active roadmap step.
