# chaOS

A 32-bit x86 hobby operating system built from scratch in C and assembly. chaOS boots from a floppy image, runs in protected mode with paging, and provides a small but real userspace: ring-3 processes with their own address spaces, system calls, and programs loaded from disk as ELF binaries.

```
              888                  .d88888b.   .d8888b.
              888                 d88P" "Y88b d88P  Y88b
              888                 888     888 Y88b.
     .d8888b  88888b.  8888b.     888     888  "Y888b.
    d88P"     888 "88b     "88b   888     888     "Y88b.
    888       888  888 .d888888   888     888       "888
    Y88b.     888  888 888  888   Y88b. .d88P Y88b  d88P
     "Y8888P  888  888 "Y888888    "Y88888P"   "Y8888P"
```

## Features

| Area | What works |
|------|------------|
| **Boot** | Real-mode boot sector, per-sector disk loader (kernel + initrd), 32-bit protected mode, paging stub into the higher-half kernel |
| **CPU** | GDT/TSS, IDT/ISRs/IRQs, PIC remap, PIT timer, `int 0x80` syscalls |
| **Memory** | Higher-half kernel at `0xC0010000`, paging (16 MiB kernel map + identity alias, supervisor-only), kernel heap (`malloc`/`free`), physical frame pool |
| **Isolation** | Per-process page directories; user programs at `0x40000000`; kernel memory not user-accessible |
| **Multitasking** | Preemptive round-robin scheduler; kernel threads and ring-3 user tasks with sleep/yield/exit |
| **I/O** | VBE framebuffer console (800×600), PS/2 keyboard + mouse (FB cursor), COM1 serial, ATA PIO disk |
| **Input** | Blocking `SYS_GETCHAR` / `SYS_READLINE` syscalls; foreground programs own the keyboard |
| **Storage** | Simple filesystem (fixed directory, up to 32 files) on the IDE disk, or in RAM when no disk is attached |
| **Userspace** | ELF32 loader, separate user build (`user/`), programs packed into the boot image (initrd) and installed at boot |
| **Processes** | `SYS_SPAWN` / `SYS_WAIT`: programs can launch other programs and wait for them |
| **Arguments** | Full `argc`/`argv`: command lines are delivered onto the new process's stack |
| **Shell** | Kernel shell plus `ush.elf` lexicon; `attune` modes (`deck` / `notes` / `code`) |
| **Graphics** | VBE soft console, PS/2 mouse cursor, `chaoswm.elf` askew compositor |
| **Notes** | `folio.elf` file browser; `scribe.elf` editor; host cross-build notes in `tools/sdk/` |

## Requirements

- **Cross compiler:** `i386-elf-gcc` and `i386-elf-ld` (e.g. from [osdev cross-compiler guide](https://wiki.osdev.org/GCC_Cross-Compiler))
- **Assembler:** `nasm`
- **Emulator:** `qemu-system-i386`
- **Host C compiler:** `cc` or `gcc` (builds the disk-image tool only)

If your toolchain is not on `PATH`, set the path in the Makefile:

```makefile
CROSS = /usr/local/i386elfgcc/bin
```

## Quick start

```bash
# Build the kernel, compile user programs, inject them into disk.img, and boot
make run
```

At the `>` prompt, try:

```
help
ls
exec greet.elf
exec ush.elf
tasks
frames
shutdown
```

### Headless self-test

Runs the full test suite and powers off QEMU automatically (no manual kill needed):

```bash
make test
```

This checks the heap, filesystem, multitasking, ELF loading, concurrent isolated user tasks (`spin.elf`), and memory isolation (`crash.elf`).

### Clean rebuild

```bash
make clean && make run
```

Delete `disk.img` to reset the on-disk filesystem.

## Running as a VM (ISO)

`make iso` produces `chaos.iso`, a bootable CD image. It uses El Torito
**floppy emulation** (1.44 MiB image + boot catalog): the BIOS presents that
image as drive 0 so the existing bootloader works. After `make test`, always
`make clean && make iso` — AUTOTEST objects must not ship in an interactive ISO.

The boot image is **self-contained**: `os-image.bin` / `chaos.flp` / the ISO
carry the kernel *and* an **initrd** with every user program (`initrd.bin`,
built by `tools/mkinitrd.c`). At boot the kernel mounts the IDE disk if one is
attached and otherwise falls back to a **RAM filesystem**; either way it then
installs any program from the initrd that is missing or has a different size.
So a CD/floppy-only VM has the same **programs** as `make run`. They used to
diverge because the ISO was only the 1.44 MiB floppy (kernel) while `make run`
also attached `disk.img` where every `.elf` lived — boot the ISO without that
disk and `exec ush.elf` failed. The initrd is what closed that gap.

They still are not *identical* in three other ways (none of them “missing files”):

1. **Persistence.** `make run` / `make run-iso` attach `-hda disk.img`. Files
   you create survive reboot. `make run-iso-bare` (ISO, no disk) uses the RAM
   filesystem — same programs, nothing persists.
2. **How the BIOS sees the image.** `make run` is `-fda` (real floppy). The ISO
   is El Torito *floppy emulation* (BIOS presents the same 1.44 MiB image as
   drive 0). QEMU SeaBIOS does this correctly; VMware often cannot start those
   ISOs at all — use `chaos.flp` there.
3. **Display.** QEMU usually gets the 800×600×32 LFB. VirtualBox BIOS often
   has no 32 bpp mode, so you get VGA text (shell still works; `chaoswm` needs
   the framebuffer).

```bash
make run-iso-bare        # closest to “just the ISO”
make test-iso            # CD + disk smoke (serial + initrd)
make test-iso-bare       # CD, no disk (RAM fs + initrd)
```

```bash
make clean && make iso   # chaos.iso + chaos.flp + disk.img + user ELFs
make test-iso            # QEMU CD boot smoke test
make run-iso             # interactive QEMU from the ISO
make vbox                # rebuild + chaos-disk.vdi + print VBox checklist
MAKE_VBOX_VM=1 make vbox # also create a ready VM via tools/vbox-setup.sh
```

### VirtualBox (recommended)

Host paths with spaces (e.g. `drive E/...`) break VBox media attach. The
helper copies images to `~/chaos-vbox-media/` and boots **floppy first**
(most reliable). El Torito CD boot also works once the ISO is attached from
that folder.

```bash
make clean && make iso && make vbox-disk
MAKE_VBOX_VM=1 make vbox
VBoxManage startvm chaOS --type gui
```

Manual UI setup:

1. New VM → **Other** → Other → **128 MiB** → Firmware **BIOS** (not EFI).
2. **Storage → IDE** (PIIX4), not SATA:
   - Primary master: `chaos-disk.vdi` (`make vbox-disk`)
   - Secondary master: `chaos.iso`
3. Add a **Floppy** controller → `chaos.flp` (preferred boot medium).
4. Boot order: **Floppy**, then Optical, then Hard Disk.
5. Display: **VBoxVGA**. chaOS never uses Bochs DISPI on VirtualBox. Many VBox
   BIOS builds do not expose a 32 bpp LFB mode — you get a clean **VGA text**
   console (shell, disk, mouse still work). Use QEMU (`make run` / `make run-iso`)
   for the framebuffer soft console / `chaoswm`.

Without the hard disk the OS boots into the RAM filesystem with all programs
from the initrd; attach the disk only if you want files to persist. Rebuilt
programs reach an existing `chaos-disk.vdi` automatically (the initrd
re-installs anything whose size changed).

### Physical x86 board (BIOS)

Firmware is **legacy BIOS only** (no UEFI). See [`docs/agent/BRINGUP.md`](docs/agent/BRINGUP.md).

```bash
make clean && make iso
make write-usb USB=/dev/sdX    # writes chaos.flp; confirm the device carefully
```

COM1 debug is **38400 8N1**. Prefer boards with IDE legacy mode or USB-FDD boot.

### VMware Workstation (Windows) — do not use the ISO

VMware’s CD boot often **cannot** start El Torito floppy-emulation ISOs. The
VM powers on to a **black screen** with no chaOS text at all (the BIOS
bootloader never runs). BIOS/EFI settings being “correct” does not fix that.

Send / use **`chaos.flp`** (or `os-image.bin` renamed to `chaos.flp` /
`chaos.img`) instead:

1. New VM → **Other** → **Other 32-bit** → Firmware **BIOS** (not EFI).
2. **Remove** the CD/DVD ISO (or leave it disconnected).
3. **Add a Floppy drive** → “Use floppy image” → select `chaos.flp`.
4. Boot order: **Floppy** first (VM settings → Options → Boot Options, or F2).
5. Optional (persistence only): add `disk.img` — or any blank IDE disk — as an
   **IDE** hard disk (convert to VMDK if needed):

```text
vmware-vdiskmanager -r disk.img -t 0 chaos-disk.vmdk
```
(or in the UI: use an existing virtual disk / convert raw as your VMware version allows)

You should see the chaOS banner immediately. If it is still black with the
floppy image, the file was corrupted in transfer — compare size to **1474560**
bytes and unblock the file on Windows (Properties → Unblock) if present.

## Writing user programs

User programs live in `user/`. Each `.c` file becomes a freestanding ELF binary linked at **`0x40000000`** (1 GiB). Every process gets its own address space, so many programs can share that virtual address without colliding.

1. Add a source file, e.g. `user/myprog.c`:

```c
#include "ulib.h"

int main(void) {
    uprint("Hello from myprog!\n");
    return 0;
}
```

2. Register it in the Makefile:

```makefile
USER_PROGS = user/hello.elf user/spin.elf user/crash.elf user/myprog.elf
```

3. Rebuild and run:

```bash
make run
# at the shell:
exec myprog.elf
```

Available user library calls (`user/ulib.h`): `uprint`, `ugetchar`, `ureadline`, `uspawn`, `uwait`, `urun`, `ulistfiles`, `upipe`, `upiperead`, `upipewrite`, `upipeclose`, `umouse`, `ugetpid`, `usleep`, `uwritefile`, `ureadfile`, `uuptime`, `uexit`.

`ureadline` blocks until Enter; the kernel echoes keystrokes and handles Backspace. Try `exec greet.elf` for a demo.

### Pipes (inter-process communication)

Processes can exchange bytes through kernel pipes: `upipe()` returns a small pipe id, `upipewrite`/`upiperead` move data (blocking when full/empty), and `upipeclose` signals end-of-stream — a reader that drains a closed pipe gets `0` (EOF). Pipe ids are passed to children on the command line. Try it:

```text
exec pipedemo.elf
```

`pipedemo.elf` creates two pipes, spawns `pipekid.elf` with the ids as arguments, sends it a message and prints the uppercased reply the child sends back.

### The user-space shell

`exec ush.elf` starts a shell that is itself just a ring-3 program: it lists
files, runs programs in the foreground (`run FILE ARGS...`) or background
(`bg FILE`), and survives its children crashing — `run crash.elf` kills only
the child. `exit` drops you back to the kernel shell.

### Command-line arguments

Anything after the program name is delivered as `argc`/`argv` (with `argv[0]`
being the program name), both from the kernel shell and from ush:

```
exec args.elf hello world     # kernel shell
run args.elf hello world      # inside ush
```

Programs simply declare `int main(int argc, char **argv)`; the kernel copies
the command line onto the new process's stack and `crt0`'s `call main`
produces a normal cdecl frame.

Syscall numbers are defined in `kernel/syscall.h` and shared with userspace.

## Shell commands

| Command | Description |
|---------|-------------|
| `help` | List commands |
| `clear` | Clear the screen |
| `echo TEXT` | Print text |
| `mem` | Heap allocation test and stats |
| `ls` | List files on disk |
| `cat FILE` | Print a file |
| `write FILE TEXT` | Write text to a file |
| `rm FILE` | Delete a file |
| `format` | Wipe the filesystem |
| `tasks` | List live tasks and states |
| `exec FILE.elf` | Load and run an ELF program (blocks until it exits) |
| `frames` | Show physical frame pool usage |
| `page` | Allocate a page-aligned block |
| `fault` | Trigger a kernel page fault (halts) |
| `reboot` / `shutdown` / `end` | Restart, power off, or halt |

**PgUp / PgDn** scroll back through the last 200 lines of screen history; any new output snaps back to the live view.

## Project layout

```
boot/          Boot sector, real-mode disk loader, switch to protected mode
cpu/           GDT, IDT, paging, VMM (frame allocator + address spaces), timer, context switch
drivers/       VGA/FB console, keyboard, serial, ATA disk
kernel/        Main kernel, shell, heap, filesystem, tasks, syscalls, ELF loader
libc/          Minimal freestanding C library
user/          User-space programs, crt0, syscall wrappers (ulib)
tools/         Host-side tools: mkfs (disk.img), mkinitrd (initrd.bin packed into the boot image)
Makefile
```

## Memory map (simplified)

| Region | Purpose |
|--------|---------|
| `0x00010000` (phys) | Kernel load address; boot stub runs here |
| `0x00100000`–`0x00400000` (phys) | Kernel heap |
| `0x00400000`–`0x01000000` (phys) | Physical frame pool (page tables, user backing frames) |
| `0x40000000`–`0x40400000` (virt) | User virtual window (program + stack), one per process |
| `0xC0000000`–`0xC1000000` (virt) | **Higher-half kernel**: alias of physical `0`–`16 MiB` |
| `0xE0000000`+ (virt) | VBE linear framebuffer (phys from BIOS ModeInfo) |

The kernel is a **higher-half kernel**: it is linked at `0xC0010000` and runs from the top of the address space, like Linux and Windows. A small boot stub (`boot/kernel_entry.asm`) runs at the physical load address, builds a temporary page directory of 4 MiB pages that maps the first 16 MiB both at `0x00000000` and at `0xC0000000`, enables paging and jumps into the high kernel. `init_paging()` later rebuilds the same double mapping with proper 4 KiB page tables.

Boot sets a VESA graphics mode in real mode (`boot/vbe.asm`); `fb_init()` maps the LFB at `0xE0000000` and takes over `kprint` via a soft console. VGA text scrollback remains available only if VBE fails.

Both kernel mappings are **supervisor-only** in every address space. User programs cannot read kernel memory; attempts page-fault and terminate the task.

## Development history (phases)

1. Bootloader, protected mode, interrupts, VGA, keyboard  
2. Paging, heap, serial debug, modular shell  
3. ATA disk, simple filesystem, power management  
4. Preemptive multitasking (kernel threads)  
5. Ring 3, TSS, `int 0x80` syscalls, kernel relocation  
6. Expanded syscalls, schedulable user tasks, fault isolation  
7. **Per-process address spaces**, ELF loader, concurrent isolated programs  
8. Keyboard input syscalls (interactive programs), screen scrollback, bootable ISO  
9. `SYS_SPAWN`/`SYS_WAIT` process management, user-space shell (`ush.elf`)  
10. Command-line arguments (`argc`/`argv`) for user programs  
11. **Higher-half kernel**: linked at `0xC0010000`, boot stub with 4 MiB boot pages  
12. **Pipes / IPC**: blocking byte-stream pipes between processes (`pipedemo.elf` + `pipekid.elf`)  
13. **Framebuffer graphics**: real-mode VBE LFB, soft text console, `fbdemo` shell command  
14. **PS/2 mouse**: IRQ12 driver, XOR cursor on the LFB, `mouse` / `exec mousedemo.elf`, `umouse()`  
15. **GUI groundwork**: clipped fill/blit primitives, per-task pixel surfaces (`SYS_SURFACE`), blocking input events (`SYS_EVENT_WAIT` — `chaoswm` sleeps at 0% CPU between keys/mouse)  
16. **Apps in windows**: `chaoswm` runs `folio` / `scribe` / `ush` as client processes (`f` / `e` / `u`) with their console on pipes (`SYS_SPAWNIO`); output renders into an askew terminal window, keys route to the focused one, Esc toggles compositor commands  
17. **Terminal widget**: `utext` grid with scrollback (Up/Down), 1×/2× glyphs (`2`), F1 drops back to the kernel console without quitting chaoswm  


## Possible next steps

- Mouse / pointer (cursor on the framebuffer)
- Personal command language + chaos compositor
- ELF shared libraries or dynamic linking
- Virtual filesystem layers, larger disk support
