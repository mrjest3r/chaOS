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
| **Boot** | Real-mode boot sector, per-sector disk loader, switch to 32-bit protected mode, kernel at `0x10000` |
| **CPU** | GDT/TSS, IDT/ISRs/IRQs, PIC remap, PIT timer, `int 0x80` syscalls |
| **Memory** | Paging (16 MiB kernel identity map, supervisor-only), kernel heap (`malloc`/`free`), physical frame pool |
| **Isolation** | Per-process page directories; user programs at `0x40000000`; kernel memory not user-accessible |
| **Multitasking** | Preemptive round-robin scheduler; kernel threads and ring-3 user tasks with sleep/yield/exit |
| **I/O** | VGA text mode, PS/2 keyboard, COM1 serial, ATA PIO disk |
| **Storage** | Simple persistent filesystem on `disk.img` (fixed directory, up to 32 files) |
| **Userspace** | ELF32 loader, separate user build (`user/`), programs injected onto disk at build time |
| **Shell** | Interactive command shell with filesystem, task, memory, and `exec` commands |

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
exec hello.elf
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

Available user library calls (`user/ulib.h`): `uprint`, `ugetpid`, `usleep`, `uwritefile`, `ureadfile`, `uuptime`, `uexit`.

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

## Project layout

```
boot/          Boot sector, real-mode disk loader, switch to protected mode
cpu/           GDT, IDT, paging, VMM (frame allocator + address spaces), timer, context switch
drivers/       VGA, keyboard, serial, ATA disk
kernel/        Main kernel, shell, heap, filesystem, tasks, syscalls, ELF loader
libc/          Minimal freestanding C library
user/          User-space programs, crt0, syscall wrappers (ulib)
tools/         Host-side mkfs tool (injected via Makefile into disk.img)
Makefile
```

## Memory map (simplified)

| Region | Purpose |
|--------|---------|
| `0x00010000` | Kernel code and data |
| `0x00100000`–`0x00400000` | Kernel heap |
| `0x00400000`–`0x01000000` | Physical frame pool (page tables, user backing frames) |
| `0x40000000`–`0x40400000` | User virtual window (program + stack), one per process |

Kernel low memory is mapped **supervisor-only** in every address space. User programs cannot read kernel memory; attempts page-fault and terminate the task.

## Development history (phases)

1. Bootloader, protected mode, interrupts, VGA, keyboard  
2. Paging, heap, serial debug, modular shell  
3. ATA disk, simple filesystem, power management  
4. Preemptive multitasking (kernel threads)  
5. Ring 3, TSS, `int 0x80` syscalls, kernel relocation  
6. Expanded syscalls, schedulable user tasks, fault isolation  
7. **Per-process address spaces**, ELF loader, concurrent isolated programs  

## Possible next steps

- Higher-half kernel (map kernel at a fixed high address)
- User-readable command-line arguments (`argc`/`argv`)
- Blocking keyboard input syscall
- ELF shared libraries or dynamic linking
- Virtual filesystem layers, larger disk support

## License

No license file is included yet. Treat this as a personal learning project unless a license is added.
