# chaOS — Agent roadmap (Path A, x86 deck)

Executable plan. Agents: do **one step** at a time. Mark progress in `STATUS.md`.

Hardware: **x86 cyberdeck board** (not ARM). Languages: **host translators / cross-compile**.

---

## Step format (how to execute)

For the active step in STATUS:

1. Goal + exit criteria below are mandatory.
2. Touch only listed areas unless a dependency forces a tiny fix elsewhere.
3. Add/extend AUTOTEST when behavior is kernel-visible.
4. Update README only if user-facing commands/features change.
5. On completion: STATUS checkbox + note; leave next step `in_progress` only if user said continue.

---

## P0 — Orientation (done when STATUS reflects it)

- [ ] Confirm MAP + this file + STATUS exist; hardware = x86.
- Exit: agents can start without repo archaeology.

---

## P1 — x86 foundation toward deck personality

### P1.1 IPC primitives
- Goal: pipes or message buffers between processes; at least spawn|wait already exists.
- Areas: `kernel/syscall*`, `kernel/task*`, `user/ulib*`, tiny demo ELF.
- Exit: two user programs exchange bytes; AUTOTEST covers it.

### P1.2 Framebuffer graphics (replace text-as-only UI)
- Goal: VBE/framebuffer (QEMU); plot pixels; bitmap font blit.
- Areas: new `drivers/fb*` or extend screen; boot may need VBE info; keep serial.
- Exit: boot shows pixels; can draw string; text shell still usable OR dual-mode.
- Note: chaos WM comes later; this is the canvas.

### P1.3 Mouse / pointer
- Goal: PS/2 mouse or QEMU USB tablet → cursor coords.
- Areas: drivers + syscall or shared input queue.
- Exit: cursor drawn on FB; click coords available to userspace.

### P1.4 Personal command language (userspace)
- Goal: replace/extend `ush` with **user-defined verbs** (document lexicon in `docs/agent/LEXICON.md` when introduced).
- Areas: `user/`, syscalls as needed.
- Exit: deck-facing commands are custom names; help lists *your* verbs.

### P1.5 Chaos compositor (software)
- Goal: windows intentionally misaligned; chaos level; focus rules; no grid-snapping default.
- Areas: userspace WM or kernel FB server — prefer userspace once FB+input exist.
- Exit: ≥2 windows askew; movable; personal commands can `summon`/`banish` windows.

### P1.6 Editor + run pipeline (corner-cut IDE)
- Goal: onboard editor; “build/run” invokes personal commands; artifacts from host SDK OK.
- Areas: user apps; optional host script under `tools/sdk/`.
- Exit: edit file on deck → run prebuilt or host-built ELF via command verb.

---

## P2 — Arch hygiene (still x86-only runtime)

### P2.1 Thin arch boundary
- Goal: isolate `cpu/` x86 bits behind clear headers so later ports don’t require full rewrite (ARM not scheduled).
- Exit: documented arch API in MAP; x86 green tests.

### P2.2 Stable syscall ABI doc
- Goal: `docs/agent/ABI.md` — numbers, structs, error codes for translators to target.
- Exit: ABI file matches `syscall.h` / ulib; version bump policy noted.

---

## P3 — First x86 silicon (physical board)

### P3.1 Board bring-up
- Goal: boot chaOS on chosen x86 board (UEFI or BIOS path — pick one and freeze).
- Areas: bootloader path, storage driver as needed.
- Exit: UART or screen banner on real hardware; keyboard works.

### P3.2 Storage + persistence on device
- Goal: real disk/SSD/SD; FS mount survives reboot.
- Exit: write file, reboot, read back on hardware.

### P3.3 Deck input profile
- Goal: keymap / chords for handheld; optional touch later.
- Exit: documented in LEXICON + input config.

---

## P4 — Daily-use minimum

### P4.1 Files + notes apps
- Exit: browse files; create/edit note without host OS.

### P4.2 Session / deck modes
- Exit: modes (e.g. code / notes) switch layouts + command sets.

### P4.3 Power / display idle
- Exit: blank or sleep path appropriate to board; wake works.

---

## G — GUI track (chaoswm from demo to deck UI)

Runs alongside P5; G1–G3 are kernel work and bump the ABI, so finish them
before P5.1 targets the SDK at a final ABI. G4+ is userspace only.

### G1 Fast drawing primitives
- Clipped row-fill `fb_fill_rect`, `fb_blit`, cursor coherence inside the primitives.
- Exit: full-screen fill < 1 tick; AUTOTEST covers clip + blit + timing.

### G2 Shared-memory surfaces
- `SYS_SURFACE`: w×h×4 buffer mapped into the caller; present via blit/damage.
- Exit: `chaoswm` renders locally and presents once per frame.

### G3 Input events
- `SYS_EVENT_WAIT` blocking event queue per task (key / mouse); `TASK_BLOCKED` state.
- Exit: `chaoswm` idles at 0% CPU; `mousedemo` converted.

### G4 Compositor server / app clients
- `chaoswm` long-lived; clients get a surface + pipe; skew stays in the compositor.
- Exit: `folio` or `scribe` runs inside a chaoswm window.
- Done as: console-over-pipes (`SYS_SPAWNIO` / `SYS_PIPEPOLL`, ABI v7) + `user/utext.c`
  text grid; text apps run unchanged. Pixel clients (a surface shared with the
  compositor) are not needed for the text-centric deck — revisit only if a
  graphical client appears.

### G5 Text widget + terminal window
- `utext` exists from G4; this step: `ush` in a window (verbs `summon`/`folio`/`scribe`
  spawn into the same window via inherited pipes), larger 2× text scale option,
  scrollback in the grid, kernel console = fallback via hotkey.
- Done as: chaoswm `u` → `ush.elf` at 2× via `SYS_SPAWNIO`; children inherit
  pipes (G4); `utext` history ring + Up/Down; `2` toggles 1×/2×; F1 kernel
  override swallows compositor blits and gives the shell the keyboard.

### G6 Deck identity
- Boot into compositor; palette per `attune` mode; toolbar of verbs.

### G7 Freeze for hardware
- Pick the board's VBE mode; record in `BRINGUP.md`.

---

## P5 — Language corner-cut pipeline

### P5.1 Host SDK
- Goal: `tools/sdk` cross-builds C → chaOS ELF against ABI.
- Exit: one-command host build drops binary onto disk image.

### P5.2 First translator integration
- Goal: external project (e.g. Python-subset → C/native) feeds SDK; chaOS runs output only.
- Exit: documented flow; sample program runs on QEMU + board.

### P5.3 Optional tiny bytecode VM (only if needed)
- Goal: small scripting without full Python — explicit STATUS approval.
- Exit: VM demo + limits documented.

### P5.4 Java strategy (deferred default)
- Prefer host `javac` + tiny interpreter **or** transpile — never HotSpot-on-chaOS unless STATUS reopened.

---

## P6 — Network + packages

### P6.1 One NIC path + minimal TCP
- Exit: fetch a blob or speak to host SDK machine.

### P6.2 Package format
- Exit: personal-command install of signed/simple archives.

---

## P7 — Identity polish (ongoing)

- Boot splash, chaos visuals, sound optional, in-OS docs.
- Freeze board + resolution; battery UI if hardware supports.

---

## Explicit non-goals (until STATUS changes)

- ARM / Raspberry Pi port
- In-OS full CPython / OpenJDK / GCC
- GPU-accelerated compositor
- Multi-board support matrix
- Perfect POSIX

---

## Dependency order (do not skip ahead)

```text
P1.1 IPC
  → P1.2 FB → P1.3 mouse → P1.4 commands → P1.5 chaos WM → P1.6 editor
P2 can overlap late P1
P3 after P1.2+ at minimum (FB optional on UART-first bring-up, but deck needs P1.2)
P4 after P3 (or advanced P1 on QEMU-only prototype)
P5 can start on QEMU in parallel with P3
P6 after P3 storage stable
```
