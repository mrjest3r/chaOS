# chaOS — Agent status (live)

**Last updated:** 2026-09-29 (host SDK; ABI v9)  
**Hardware target:** x86 cyberdeck board (not ARM/Pi). Published floor: 1 GHz, BIOS, 1 GB RAM, about 1 GB disk (`BRINGUP.md`). QEMU tests pass `-m 1024`.  
**Strategy:** Path A + corner-cut languages (host translators / cross-compile)

## Active focus

- **Current step:** `P5.2` First translator (ABI v9); P3.1 remains the hardware gate  
    - **Blocked on:** P3.1 still needs physical board (prep done); continuing Path A on QEMU per roadmap  
    - **User preference:** continue phase-by-phase when asked; do not skip to GUI/languages early unless requested

## Checklist

### P0 Orientation
- [x] Agent map + roadmap + status created
- [x] x86 board chosen for deck

### P1 Foundation → deck personality
- [x] P1.1 IPC primitives — kernel pipes (`kernel/pipe.c`), syscalls 13–16, ulib wrappers, `pipedemo.elf`/`pipekid.elf` round-trip, AUTOTEST green (2026-07-29)
- [x] P1.2 Framebuffer graphics — VBE LFB, soft console with **2× font**, PgUp/PgDn history (VBox: BIOS-only 32bpp; QEMU: BIOS/DISPI)
- [x] P1.3 Mouse / pointer — PS/2 IRQ12, XOR FB cursor, `SYS_MOUSEGET` / `umouse`, `mouse` shell cmd, `mousedemo.elf` (2026-07-29)
- [x] P1.4 Personal command language — `docs/agent/LEXICON.md`, `ush` verbs (`summon`/`roster`/…), text caret + arrows/Home/End + mouse click-to-place (2026-07-29)
- [x] P1.5 Chaos compositor — `chaoswm.elf`, `SYS_FB`/`SYS_TRYCHAR`, askew windows, drag/focus/chaos; AUTOTEST green (2026-07-29)
- [x] P1.6 Editor + run pipeline — `scribe.elf` line editor; ush `scribe`/`ignite`; `tools/sdk/README.md`; AUTOTEST green (2026-07-29)

### P2 Hygiene
- [x] P2.1 Thin arch boundary — `cpu/arch.h` + `context.h`; kernel includes arch facade; Arch API in MAP (2026-08-01)
- [x] P2.2 ABI.md — `docs/agent/ABI.md` v1 matches syscall.h/ulib; version bump policy (2026-08-01)

### P3 Silicon
- [ ] P3.1 Board bring-up — **prep:** BIOS-only freeze, `docs/agent/BRINGUP.md`, `make write-usb`; exit still needs real hardware banner + keyboard
- [ ] P3.2 Storage persistence
- [ ] P3.3 Deck input profile

### P4 Daily use
- [x] P4.1 Files + notes — `folio.elf`, ush `folio`/`note`/`scrap`, `SYS_DELETEFILE` (20), ABI v2; AUTOTEST green (2026-08-03) [QEMU prototype; P3 open]
- [x] P4.2 Session / deck modes — ush `attune deck|notes|code` switches prompt + verb sets; AUTOTEST green (2026-08-03)
- [x] P4.3 Power / idle — `drivers/idle.c`, shell `doze`, ush `slumber`/`SYS_DOZE` (21), auto-blank ~30s; ABI v3; AUTOTEST green (2026-08-03)

### G GUI track
- [x] G1 Fast primitives — clipped row-fill, `fb_blit`, `FBOP_BLIT` (ABI v4), cursor coherence in `fb.c`; 20 full-screen fills = 6 ticks; AUTOTEST green (2026-09-24)
- [x] G2 Shared-memory surfaces — `SYS_SURFACE`/`SYS_SURFFREE` (22/23, ABI v5), `task_t.surf[]`, `vmm_unmap_pde`, `user/ugfx.*`, shared `kernel/font8x8.h`; chaoswm composes into a back buffer and presents with one blit; badptr covers lifecycle + leak check; AUTOTEST green (2026-09-24)
- [x] G3 Input events — `SYS_EVENT_WAIT` (24, ABI v6), `uevent_t` in `kernel/event_user.h`, per-task 32-entry queue + `TASK_BLOCKED`, single input listener fed by keyboard/mouse IRQs (mouse motion coalesced); `chaoswm` and `mousedemo` block instead of polling (chaoswm wakes 12×/s only while chaos > 0); `run_event_selftest` verifies BLOCKED-while-idle + delivery; badptr probes it; AUTOTEST green (2026-09-24)
- [x] G4 Compositor server / clients — `SYS_SPAWNIO` (25) + `SYS_PIPEPOLL` (26), ABI v7; `task_t.stdin_pipe/stdout_pipe` (inherited; owner closes stdout at exit; EOF on stdin ends the client); `user/utext.*` text grid; chaoswm `f`/`e` summon `folio`/`scribe` as client processes in askew windows, Esc toggles cmd/app key routing, `b` banish hangs up, quit waits for clients; `run_wm_client_selftest` (folio scraps a file via the window pipe; no task/pipe leak); AUTOTEST green; screenshot-verified on QEMU (2026-09-24)
- [x] G5 Text widget + terminal window — chaoswm `u` summons `ush.elf` at 2× (`SYS_SPAWNIO`); ush children inherit the window pipes; `utext` 48-line history + Up/Down; `2` toggles 1×/2×; F1 kernel console override (`kbd_console_override`, SYS_FB draws swallowed); `run_ush_window_selftest` (folio-via-ush scraps a file); AUTOTEST green (2026-09-24)
- [x] G6 Deck identity — interactive boot execs `chaoswm` when the LFB is up; shared `attune` file (`user/attune.h`) drives ush + compositor palettes; clickable verb toolbar; `a` cycles modes; `run_desk_selftest` green on disk + no-disk (2026-09-27)
- [x] G7 Freeze for hardware — desk mode frozen at **800×600×32** (VBE 0x115); recorded in `BRINGUP.md` § G7 (2026-09-27)

### P5 Languages (corner-cut)
- [x] P5.1 Host SDK — `make sdk SRC=file.c` cross-builds a freestanding ELF and injects it into `disk.img` (`tools/sdk/chaos-sdk`). Refuses names over 23 characters and files over 32 KiB (2026-09-29)
- [ ] P5.2 First translator
- [ ] P5.3 Tiny VM (optional)
- [ ] P5.4 Java path (deferred)

### P6 Network
- [ ] P6.1 NIC + minimal TCP
- [ ] P6.2 Packages

### P7 Polish
- [ ] Identity / freeze hardware

## Already shipped (kernel lab — do not redo)

- Boot → PM → higher-half kernel (Phase 11)
- IDT/IRQ/PIT, VGA text + scrollback, keyboard focus
- Heap, ATA single-sector, simple FS
- Tasks, ELF, syscalls, ush, argc/argv, spawn/wait
- Pipes / IPC (P1.1) through editor (P1.6), arch/ABI (P2)
- Folio notes browser + delete syscall (P4.1)
- ISO/FLP artifacts (self-contained: kernel + initrd, RAM fs fallback); `make test` AUTOTEST suite (disk + no-disk runs)

## Notes for next agent

- Kernel linked via `boot/kernel.ld`; stub at phys `0x10000`.
- **Self-contained boot image (2026-09-24):** `os-image.bin`/`chaos.flp`/`chaos.iso` = bootsect + kernel (256-sector budget) + `initrd.bin` (all user ELFs, `tools/mkinitrd.c`) at `INITRD_PHYS 0x30000`. No IDE disk → `fs.c` RAM backend; `kernel/initrd.c` installs missing/changed programs at boot either way. This fixed the "ISO differs from `make run`" report (the ISO used to carry only the floppy, programs lived in `disk.img`) and two latent bugs: kernel size vs the old 60 KiB load budget, and `.bss` never being zeroed (now done in `kernel_entry.asm`). `make test` runs the suite twice (IDE disk, then no disk); `run_initrd_selftest` checks the seeding. `disk.img` is now optional persistence only. Remaining VBox caveat is display (VGA text if no 32 bpp VBE), not files.
- Prefer `#include "../cpu/arch.h"` from kernel; do not include `idt.h` outside `cpu/`.
- Syscall ABI: `docs/agent/ABI.md` **version 10** (`SYS_BRK` = 31; `SYS_CLOCK` = 30; `SYS_PROCS` = 27, `SYS_PSCTL` = 28, `SYS_POWER` = 29). Next free number 32. Identity map is installed RAM up to 1 GiB; the higher-half alias stays 16 MiB. User window is 64 MiB with a 32 MiB heap cap. Disk volume is about 1 GiB, 1 MiB per file. ATA PIO remains one sector per command.
- **P3.1** still open (hardware). Next software step: **P5.1** Host SDK. GUI track G1–G7 is done.
- F1 and `q` do not leave the desk (2026-09-28). `kbd_console_override` still swallows `SYS_FB` draws if set, but no key sets it. The kernel shell runs only with no framebuffer, or if chaoswm itself exits.
- Windowed clients: any program that only uses `uprint`/`ureadline`/`ugetchar`/`utrychar` runs in a chaoswm window unchanged (MAP invariant 13). Do not add `kprint`/`kbd_getchar` calls to console syscalls. Pipe pool is 16 → max 8 client windows.
- AUTOTEST gotcha: earlier tests leave two kernel worker threads alive and may leave pipes — compare `task_count()` / free-pipe count before vs after, never against absolute numbers.
- User drawing: allocate a surface (`ugfx_create`), draw with `ugfx_*`, `ugfx_present` once per frame. Font lives in `kernel/font8x8.h` (shared with fb.c).
- User input for GUI programs: `uevent_wait(&ev, timeout)` (MAP "Input routing"). A program that calls it owns keys + mouse until exit; do not mix with `ugetchar`/`utrychar` in the same program. Invariant 12 governs `TASK_BLOCKED`.
- FB drawing: primitives clip + handle the mouse cursor (MAP "Framebuffer drawing rules"); never add `mouse_fb_cleared()` to callers.
- Idle: `drivers/idle.c`; kernel shell `doze`. Deck verbs `slumber` and `extinguish` power the machine off (ACPI). `rebirth` reboots. All three need `ascend`.
- Hardening (2026-09-24): syscall user-pointer validation (`kernel/uaccess.h`, invariant 10); `badptr.elf` AUTOTEST. Prereq for GUI G2 shared surfaces.
- Folio / modes: as P4.1–P4.2.
- Keep `make test` green. Interactive image: `make clean && make run` (not an AUTOTEST build).
- Hung headless QEMU: `make qemu-kill` (HMP `quit` on `/tmp/chaos-qemu-mon`).
- Runtime (2026-09-27): `MAX_PIPES` 16 × 1024 B; `TASK_EVQ_SIZE` 64; `MAX_TASKS` 16; event-listener stack (depth 4); user ELF RX/RW PHDRs; `SYS_FB` in `syscall_fb.c`; AUTOTEST suite in `kernel/autotest.c`. ATA stays single-sector PIO (invariant 4).
- Terminal (2026-09-30): `user/verbs.h` is the only name table (`terminal.elf`). Classic names are rejected. Desk icon `terminal` (or `t`) on every mode. PgUp/PgDn scroll the focused window. At the prompt, Up/Down scroll that history and Left/Right/Home/End move the caret; `scribe` still receives arrows raw. ush panel removed. folio/scribe are not desk panels. Verbs that need network or packages answer "not in this OS yet". Package upgrade verb is `empower` (stub).
- Clock (2026-09-28): `moment` prints CMOS local time (`SYS_CLOCK`). `age` stays uptime. No timezone.
- Editor (2026-09-28): `scribe FILE` opens `scribe.elf` in the terminal (arrows, Enter, Backspace, Ctrl-O save, Ctrl-X quit). `scribe FILE TEXT` still writes TEXT in one shot when the file is new. An existing file opens the editor and is not replaced. A graphical editor is a later app.
- Host SDK (2026-09-29): `make sdk SRC=...` installs one ELF onto `disk.img` (1 MiB file cap). Doom was tried via doomgeneric and was not installed: it needs a hosted libc, and the shareware WAD is larger than one file.
- Audio (2026-09-30): Intel HDA (`drivers/hda.c`) programs a real codec graph: pin config, connection list back to the DAC, amplifier unmute, EAPD for Realtek (`10ec`). UHCI plus a USB audio-class packet (`drivers/uhci.c`, `drivers/usb_audio.c`). QEMU adds `intel-hda`, `hda-duplex`, and `usb-audio`. No userspace mixer yet.
- Processes (2026-09-28): `observe` / `oversee` list tasks. `slay` kills one user process. `obliterate` also kills user processes it started. `freeze` / `awaken` take a user process off the scheduler and put it back. Pid 0, kernel threads, and the caller are refused. ABI v8.
- Directories (2026-09-28): `wander`, `abode`, `forge` are live. A directory is a zero-byte name ending in `/`; files inside use a path in the same 24-byte name. The current directory is per terminal. `summon` is not prefixed with it. No syscall and no on-disk layout change.
- Accounts (2026-09-30): interactive boot runs `gate.elf` first. No `accounts` file → ask for a user and password and store a hash. Every later boot lists the names, then asks for a username and a password (one account no longer skips the name). `session` names the logged-in user. Prompt is `name> ` / `name# `. `ascend` checks that password. `manifest` / `exile` / `key` / `assume` need ascend. `accounts` and `session` are system files.
- ISO build is removed for now (`tools/mkiso.sh` gone). Boot media is `chaos.flp` / `make run`.
