# chaOS syscall ABI

**ABI version:** 11  
**Arch:** x86-32, `int 0x80`  
**Sources of truth:** `kernel/syscall.h`, `user/ulib.h`, `kernel/fb_user.h`, `kernel/frame_user.h`, `kernel/event_user.h`, `drivers/mouse.h`

Translators and host SDK programs target this surface. When numbers or structs
change incompatibly, bump **ABI version** and note it in `STATUS.md`.

## Calling convention

| | |
|--|--|
| Entry | `int 0x80` |
| Number | `eax` = `SYS_*` |
| Args | `ebx`, `ecx`, `edx` (in that order) |
| Return | `eax` (signed where errors use `-1`) |
| Clobbers | As usual for the trap; preserve others across the call in ulib asm |

`SYS_EXIT` does not return to the caller.

**Pointer arguments** must lie entirely inside the user window
`[USER_BASE, USER_STACK_TOP)` = `[0x40000000, 0x44000000)` **or** inside a
surface the calling task currently owns (`SYS_SURFACE`, checked against the
logical `w*h*4` size). Strings must be NUL-terminated inside the window (max
4096 bytes). Buffers are checked as `ptr + len`. Any pointer outside those
regions makes the call return `-1` without touching memory
(`kernel/uaccess.h`). `SYS_PRINT` also returns `-1` in that case rather than 0.

## Surfaces (v5, GUI G2)

`SYS_SURFACE(w, h)` maps a zeroed `w*h*4` pixel buffer into the caller and
returns its address. Limits: `1 ≤ w,h ≤ 4096`, `w*h*4 ≤ 4 MiB`, at most **4**
live surfaces per task. Slot *n* is at `0x50000000 + n*0x400000`. Present it
with `FBOP_BLIT` (`src` = surface, `src_pitch` = `w`). `SYS_SURFFREE` unmaps
it (further use → `-1`); anything still mapped is reclaimed at task exit.
ulib: `usurface` / `usurface_free`; `user/ugfx.h` draws into one
(`ugfx_fill`, `ugfx_text`, `ugfx_present`).

## Frames (compositor)

`SYS_FRAME` (`uframe`, `frame_req_t` in `kernel/frame_user.h`) lets a program publish a pixel buffer (`FRAME_PUBLISH`: `ptr`, `w`, `h`, at most 1024×1024). The compositor sets where that buffer sits (`FRAME_PLACE`) and copies it into its own surface (`FRAME_BLIT`). `FRAME_QUERY` returns the caller's placement. `FRAME_KEYS` chooses which task receives raw key-up/down (`SYS_KEYEVT`); pass the compositor's pid to take them back.

## Input events (v6, GUI G3)

`SYS_EVENT_WAIT(ev, timeout)` pops the next input event for the calling task
into `*ev` (`uevent_t`, `kernel/event_user.h`). `timeout` is in PIT ticks:
`0` polls and returns at once, `UEV_FOREVER` (`0xFFFFFFFF`) blocks with no
timeout. Returns `1` (event stored), `0` (timeout), `-1` (bad pointer / not a
user task). While blocked the task is `TASK_BLOCKED` and uses no CPU.

The caller becomes the **input listener** (one per system; the newest caller
wins). From then on, keys typed while a program has focus go to its queue
instead of the `SYS_GETCHAR` ring — do not mix the two in one program — and
every mouse packet is reported as `UEV_MOUSE` with the absolute pointer
position and current buttons. Keys already sitting in the ring when the task
first listens are moved into its queue. Consecutive mouse events with the
same button state are coalesced (newest position wins); the queue holds 32
events and drops the newest when full. The listener is cleared at task exit,
returning input to the legacy ring / kernel console.

## Console redirection (v7, GUI G4)

`SYS_SPAWNIO(cmdline, stdin_pipe, stdout_pipe)` is `SYS_SPAWN` with the
child's console moved onto pipes the caller created with `SYS_PIPE`
(`-1` = inherit the caller's). In the child, `SYS_PRINT` writes to
`stdout_pipe`; `SYS_GETCHAR` / `SYS_READLINE` / `SYS_TRYCHAR` read from
`stdin_pipe` (readline echo also goes to `stdout_pipe`). Grandchildren spawned
with plain `SYS_SPAWN` inherit both pipes. A child that reads **EOF** on its
stdin (the compositor closed the pipe) is terminated by the kernel. When the
child exits, the kernel closes `stdout_pipe`, so the reader sees EOF and can
free it. Programs written against the plain console (`folio`, `scribe`,
`ush`) run unchanged inside a window this way.

`SYS_PIPEPOLL(id)` is the non-blocking companion: bytes readable (`>0`),
`0` empty-and-open, `-1` invalid or closed-and-drained (a read now returns
EOF and frees the slot). Compositors use it so they never block on a client.

## Version bump policy

- **Patch (docs only):** clarify wording; version stays.
- **Minor:** add a new `SYS_*` at the next free number; old numbers unchanged → bump ABI version (1→2…).
- **Major / breaking:** reuse or renumber existing calls, or change struct layouts → bump ABI version and update ulib + AUTOTEST in the same change.

Never silently renumber. Prefer new numbers over overloading.

## Syscall table

| # | Name | Args | Return |
|---|------|------|--------|
| 0 | `SYS_PRINT` | ebx = `const char *` | 0 |
| 1 | `SYS_EXIT` | — | (task ends) |
| 2 | `SYS_GETPID` | — | task id |
| 3 | `SYS_WRITEFILE` | ebx = name, ecx = buf, edx = len | 0 / -1 |
| 4 | `SYS_READFILE` | ebx = name, ecx = buf, edx = max | n / -1 |
| 5 | `SYS_LISTFILES` | ebx = `fs_fileinfo_t *`, ecx = max | count |
| 6 | `SYS_UPTIME` | — | PIT ticks since boot |
| 7 | `SYS_YIELD` | — | 0 |
| 8 | `SYS_SLEEP` | ebx = ticks | 0 |
| 9 | `SYS_GETCHAR` | — | char (blocks) |
| 10 | `SYS_READLINE` | ebx = buf, ecx = max | length |
| 11 | `SYS_SPAWN` | ebx = cmdline (`"prog.elf args…"`) | pid / &lt;0 |
| 12 | `SYS_WAIT` | ebx = pid | 0 / -1 |
| 13 | `SYS_PIPE` | — | pipe id / -1 |
| 14 | `SYS_PIPEREAD` | ebx = id, ecx = buf, edx = max | n / 0=EOF / -1 |
| 15 | `SYS_PIPEWRITE` | ebx = id, ecx = buf, edx = len | len / -1 |
| 16 | `SYS_PIPECLOSE` | ebx = id | 0 / -1 |
| 17 | `SYS_MOUSEGET` | ebx = `mouse_state_t *` | 0 / -1 |
| 18 | `SYS_FB` | ebx = `fb_req_t *` | 0 / -1 |
| 19 | `SYS_TRYCHAR` | — | char or 0 if none |
| 20 | `SYS_DELETEFILE` | ebx = name | 0 / -1 |
| 21 | `SYS_DOZE` | — | 0 (after wake) |
| 22 | `SYS_SURFACE` | ebx = w, ecx = h | user pointer to `w*h` `0x00RRGGBB` pixels, or 0 |
| 23 | `SYS_SURFFREE` | ebx = pointer from `SYS_SURFACE` | 0 / -1 |
| 24 | `SYS_EVENT_WAIT` | ebx = `uevent_t *`, ecx = timeout ticks (`0` poll, `UEV_FOREVER` block) | 1 event / 0 timeout / -1 |
| 25 | `SYS_SPAWNIO` | ebx = cmdline, ecx = stdin pipe, edx = stdout pipe (`-1` inherit) | pid / &lt;0 |
| 26 | `SYS_PIPEPOLL` | ebx = id | bytes readable / 0 empty / -1 EOF or invalid |
| 27 | `SYS_PROCS` | ebx = `uproc_t *`, ecx = max | count written / -1 |
| 28 | `SYS_PSCTL` | ebx = pid, ecx = `PSOP_*` | 0 / -1 |
| 29 | `SYS_POWER` | ebx = `PWOP_*` | does not return; -1 if the op is unknown |
| 30 | `SYS_CLOCK` | ebx = `uclock_t *` | 0, or -1 if the pointer is bad or the RTC read is nonsense |
| 31 | `SYS_BRK` | ebx = new heap end, or 0 to query | current break, or -1 |

Next free number: **32**.

`SYS_PSCTL` acts on **user** tasks only. It refuses pid 0, kernel threads, and the caller. `PSOP_SLAY` (1) kills one process. `PSOP_OBLITERATE` (2) also kills user processes it spawned (and theirs). `PSOP_FREEZE` (3) takes it off the scheduler until `PSOP_AWAKEN` (4).

`SYS_POWER` stops every other task first. `PWOP_REBOOT` (1) pulses the keyboard-controller reset. `PWOP_SHUTDOWN` (2) and `PWOP_OFF` (3) both cut power through ACPI (`power_off`).

### `uclock_t` (`kernel/clock_user.h`, v9)

```c
uint32_t year, month, day, hour, min, sec;
```

Local CMOS time. `month` is 1–12, `hour` is 0–23. There is no timezone.

### `uproc_t` (`kernel/proc_user.h`, v8)

```c
int32_t  id;
uint32_t state;   /* PROC_READY 1, RUNNING 2, SLEEPING 3, BLOCKED 5, FROZEN 6 */
uint32_t user;    /* 1 ring 3, 0 kernel */
char     name[24];
```

## Shared structs

### `fs_fileinfo_t` / `ufileinfo_t`

```c
char     name[24];
uint32_t size;
```

### `mouse_state_t` / `umouse_t`

```c
int32_t  x, y;
uint32_t buttons; /* bit0=left, bit1=right, bit2=middle */
```

### `uevent_t` (`kernel/event_user.h`, v6)

```c
uint32_t type;     /* UEV_KEY (1) | UEV_MOUSE (2) */
int32_t  x, y;     /* UEV_MOUSE: pointer position */
uint32_t key;      /* UEV_KEY: char, or UEV_KEY_LEFT..UEV_KEY_DOWN = 0x11..0x16 */
uint32_t buttons;  /* UEV_MOUSE: bit0=left, bit1=right, bit2=middle */
```

### `fb_req_t` (`kernel/fb_user.h`)

| `op` | Meaning |
|------|---------|
| `FBOP_INFO` (0) | fill `width`/`height`/`pitch`; `ok` |
| `FBOP_FILL` (1) | fill rect `x,y,w,h` with `color` |
| `FBOP_TEXT` (2) | draw `text[]` at `x,y`; `color` fg, `color2` bg |
| `FBOP_CLEAR` (3) | clear screen to `color` |
| `FBOP_RESTORE` (4) | restore kernel soft console |
| `FBOP_BLIT` (5) | **v4** — copy `w×h` pixels from user buffer `src` (row stride `src_pitch` pixels, `≥ w`) to `x,y` |

`text` is at most 47 chars + NUL. `src` / `src_pitch` were appended in v4;
earlier field offsets are unchanged. Blit limits: `w,h,src_pitch ≤ 4096`;
`src` must satisfy the pointer rule for `h*src_pitch*4` bytes. All draw ops
clip to the screen; the kernel keeps the mouse cursor coherent (no restore
op needed after partial paints).

## Errors

- Most failures: `eax == (uint32_t)-1` (ulib returns `int` -1).
- `SYS_PIPEREAD` returns `0` for EOF (writer closed), not an error.
- A redirected task (`SYS_SPAWNIO`) that reads EOF from its stdin pipe is terminated, not returned to.
- Invalid syscall number: `-1`.
- Pointer args outside the user window return `-1` (see calling convention).

## ulib wrappers

| C name | SYS |
|--------|-----|
| `uprint` | PRINT |
| `uexit` | EXIT |
| `ugetpid` | GETPID |
| `uwritefile` / `ureadfile` / `udelete` | WRITEFILE / READFILE / DELETEFILE |
| `ulistfiles` | LISTFILES |
| `uuptime` | UPTIME |
| `usleep` | SLEEP (+ yield via wait paths) |
| `ugetchar` / `ureadline` / `utrychar` | GETCHAR / READLINE / TRYCHAR |
| `uspawn` / `uwait` / `urun` | SPAWN / WAIT |
| `upipe` / `upiperead` / `upipewrite` / `upipeclose` / `upipepoll` | PIPE* / PIPEPOLL |
| `uspawnio` | SPAWNIO |
| `umouse` | MOUSEGET |
| `ufb` / `ufbblit` | FB |
| `udoze` | DOZE |
| `ulistprocs` / `upsctl` / `upower` | PROCS / PSCTL / POWER |
| `uclock` | CLOCK |
| `ubrk` / `umalloc` / `ufree` | BRK |
| `ukeyevt` | KEYEVT |
| `uplay` | AUDIO |
| `uframe` | FRAME |
| `usurface` / `usurface_free` | SURFACE / SURFFREE |
| `uevent_wait` | EVENT_WAIT |

## Memory layout (userspace)

| Region | Address |
|--------|---------|
| User code/data link | `USER_BASE` `0x40000000` |
| User stack top | `USER_STACK_TOP` `0x44000000` |
| Surfaces (4 × 4 MiB slots) | `SURF_BASE` `0x50000000` … `0x51000000` |
| Kernel (not user-accessible) | `≥ 0xC0000000` |
| Framebuffer LFB (kernel map; drawn via SYS_FB) | `0xE0000000` |

## Key events (v11)

`SYS_KEYEVT` returns the next press or release as `(pressed << 8) | scancode`
(set-1 make code, break bit already cleared), or `-1` when the queue is empty.
The character ring (`SYS_GETCHAR`) still only carries key-down text. A game
that must see the key go up uses this queue. ulib: `ukeyevt`.
