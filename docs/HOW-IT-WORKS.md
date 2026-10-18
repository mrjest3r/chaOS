# How chaOS fits together

This page is for someone who has written programs but has not built an operating system. It follows one boot, from the moment the machine turns on to the moment a window is on the desk. Names in `code font` are real files in this repository.

chaOS is small on purpose. It is a 32-bit x86 PC system. There is no Linux underneath it, and it does not pretend to be one.

## What an operating system is doing

A normal program asks someone else for memory, files, and the screen. That someone is the operating system. chaOS is that someone.

It has three jobs that show up everywhere below:

1. **Start the machine.** The PC firmware only knows how to load one sector of a disk. Everything after that is our code.
2. **Keep programs apart.** Each program gets its own memory. One program cannot read the kernel, and it cannot scribble on another program's memory.
3. **Hand out the shared things.** The screen, the keyboard, the disk, and the ability to start another program all go through a system call. The program asks. The kernel does the work.

A system call here is `int 0x80`. The program puts a request number in a register and traps into the kernel. The numbers live in `kernel/syscall.h`. Userspace wrappers with friendlier names live in `user/ulib.h` (`uprint`, `summon`'s `urun`, and so on).

## From power to the desk

Think of boot as four people handing a box to the next person.

**The firmware (BIOS).** It is not our code. It looks at the boot device, loads 512 bytes into memory at address `0x7C00`, and jumps there. chaOS requires a legacy BIOS. A pure UEFI machine will not start it.

**The boot sector (`boot/bootsect.asm`).** Those 512 bytes load the rest of the kernel and a small bundle of programs called the initrd. They also ask the BIOS for a graphics mode and turn on the A20 gate. VirtualBox leaves that gate shut. If it stays shut, the kernel's heap (which starts at 1 MiB) wraps around and overwrites the kernel. The boot stub in `boot/kernel_entry.asm` opens it before paging.

**Protected mode and paging.** The CPU starts in a 16-bit mode that can only see a megabyte easily. `boot/switch_pm.asm` switches to 32-bit protected mode. Paging then turns on. Paging is a table that says "when this program uses address X, the RAM chip at address Y is what it actually gets." That table is how two programs can both believe they live at `0x40000000` and not collide.

**The kernel (`kernel/kernel.c`).** `kernel_main` sets up interrupts, memory, the screen, the disk, and the clock, then starts `gate.elf` (login) and `chaoswm.elf` (the desk). If there is no framebuffer, it falls back to a text shell inside the kernel. That shell is not what you see on a normal boot.

The initrd is loaded at physical address `0x38000`. It holds the system programs that have to exist before any disk is mounted. Each of those files is at most 32 KiB, because the boot image is a 1.44 MB floppy and that is the cap in `tools/mkinitrd.c`.

## Two kinds of storage

This split is the one to remember.

**The base OS** is what you get by booting the install disc (`make chaos.iso`). It is the kernel, the system programs, and Doom. The disc is a CD. The BIOS only boots a floppy image, so the CD contains `chaos.flp` as an El Torito floppy, and a `BASE/` directory beside it. After the kernel is running it reads the CD and copies any base file that is missing or the wrong size. A machine with no hard disk keeps that set in a 40 MiB RAM volume, which is large enough for Doom's data file.

**Everything else stays on the hard disk.** Accounts, the logged-in session, the desk palette (`sys/attune`), and notes (`home/`) are not in the base catalog. A new program you install later is an app, not part of the base OS, until someone deliberately adds it to that catalog.

On disk the layout is:

| Path | What it is |
|------|------------|
| `bin/` | Programs. `summon hello.elf` looks here when you do not type a slash |
| `sys/` | `accounts`, `session`, `attune`, `bootcount` |
| `home/` | Notes such as `ideas.txt` |
| `freedoom1.wad` | Doom's data file, left at the root because the game opens that exact name |

A directory is not a special filesystem object. It is a zero-byte file whose name ends in `/`. A file inside it is just a longer name, like `home/ideas.txt`. Names are at most 23 characters.

## Notes for the talk

These are for you, not for a first-time reader. Use them as the spine of a demo. The sections above and below are the explanation you can hand to someone afterward.

**One sentence.** chaOS is a from-scratch 32-bit PC operating system that boots itself, isolates programs, and runs a desk with a terminal and Doom in a window.

**What to show, in order.**

1. `make run` in QEMU. Say that nothing under this window is Linux.
2. Login. First boot creates an account. Later boots list the names and ask for a password. That file is `sys/accounts` on the hard disk, not on the install disc.
3. The desk. Point at the terminal icon. Mention that windows are drawn askew on purpose. This is a cyberdeck, not a copy of a desktop you already know.
4. Terminal: `lexicon`, then `reveal`, then `wander bin`. The command names are ours (`user/verbs.h`). `ls` is rejected.
5. `summon hello.elf`. One sentence: the program is an ELF linked at `0x40000000`, and the kernel loaded it into a fresh address space.
6. If the disc or the disk has Doom, open it from the desk. It does not take over the screen. It publishes a 640×400 frame and the desk copies that frame into a window. Fullscreen is letterboxed, not stretched. Esc then `b` closes it. Music is silent. Sound effects go to the sound chip. There is no MIDI synth.

**Numbers that are safe to say.**

- Syscall ABI version 11. The next free number is 35.
- Screen: 800×600, 32 bits per pixel.
- Each program: a 64 MiB window, of which 48 MiB can be heap.
- Disk: about 1 GiB, 32 MiB per file, 10000 names.
- Serial debug, if you need it: COM1, 38400 8N1.

**Do not claim these.** Network. Packages (`empower` is a name with no installer). Icons that appear by themselves when a program is added (the desk list is still written in `user/chaoswm.c`). UEFI. A Unix shell. Doom as a `make sdk` program (the engine needs a hosted C library; the data file is installed from the disc, not from the SDK).

**If someone asks why the ISO is weird.** The BIOS boots a 1.44 MB floppy. Doom's data file is about 28 MB, so it cannot live in that floppy. The CD boots the floppy image, then the kernel reads the rest of the disc and copies the base set. Personal files never ride on that disc.

**If someone asks what is hard.** The part that is easy to underestimate is the handoff: real mode, A20, paging, then a higher-half kernel that cannot touch BIOS services anymore. After that, the hard part is sharing one screen among windows without one program painting over another. That is `chaoswm` plus `SYS_FRAME`.

**Where you are in the plan.** Hardware bring-up (a real board) is still open. The next software step is a host-side translator that feeds `make sdk`. The desk, the terminal, accounts, audio output, and the install disc are already in the tree.

## The repository, top to bottom

Read the tree in the same order the machine runs.

`boot/` runs before C is a comfortable place to be. Assembly loads the kernel, switches CPU mode, and asks for a graphics mode (`boot/vbe.asm`). If the BIOS cannot do 800×600×32, the kernel sets that mode itself through the Bochs display interface. QEMU and VirtualBox's VBoxVGA both take that path. Leave the VirtualBox display on VBoxVGA. VMSVGA does not speak it.

`cpu/` is the processor-shaped code: the interrupt table, the task-switch trampoline, paging, and the pool of 4 KiB frames. Interrupts are how the timer, keyboard, and mouse poke the kernel while a program is running. The timer tick is what makes the scheduler preemptive. A program does not have to yield. The clock takes the CPU away.

`drivers/` talk to devices. The framebuffer is memory the video card shows on the monitor. The keyboard and mouse are PS/2. The disk is ATA, one sector at a time, because multi-sector reads hang VirtualBox. The CD reader (`drivers/atapi.c`) exists so the install disc can be read after the BIOS is gone. Sound is an Intel HDA codec path plus a small USB audio check.

`kernel/` is the policy. `kernel/task.c` is processes. `kernel/elf.c` loads programs. `kernel/fs.c` is the filesystem. `kernel/syscall.c` is the front door from user programs. The kernel shell in `kernel/shell.c` is the fallback when graphics did not start. Its commands (`help`, `ls`, `exec`) are not the desk terminal's commands.

`libc/` is a few freestanding C routines for the kernel (`memcpy` and friends). It is not a hosted C library. User programs cannot `#include <stdio.h>` and expect `printf` to work, unless they are linked with the small Doom support library under `user/libc/`, which is still not a full libc.

`user/` is everything that runs as a normal program. `crt0` is the real entry. It calls `main`. `chaoswm` is the desk. `terminal` is the shell. `gate` is login. `scribe` is the text editor. Each of these is linked at the same virtual address, `0x40000000`, and each gets its own page tables, so that reuse is safe.

`tools/` runs on Linux, not on chaOS. `mkfs` copies files onto `disk.img`. `mkinitrd` packs the floppy bundle. `stagebase` pulls Doom and the system programs into the `BASE/` directory of the ISO.

`docs/agent/` is the map, the syscall list, the hardware notes, and the command lexicon. `docs/agent/STATUS.md` is the live "what is done" page.

## What happens when you summon a program

`summon hello.elf` in the terminal does not jump to that file. The terminal asks the kernel, through `urun`, to create a process, load the ELF, and wait until it exits.

The kernel checks that the file is in the user window, builds a new address space, copies the program in, and builds a stack with `argc` and `argv`. The new process starts at `crt0`, which calls `main`. When `main` returns, the process exits and the terminal continues.

If the program only prints text, it can run inside a desk window. The desk creates two pipes, one for keystrokes and one for output, and the kernel connects those to the program's reads and writes. The desk draws the text. The program never touches the framebuffer.

Doom is the other kind. It draws its own pixels and publishes that buffer with `SYS_FRAME`. The desk copies the buffer into a window. That is why Doom can sit on the desk next to the terminal instead of replacing the screen.

A bad pointer in a user program page-faults. The kernel ends that process. The desk and the other windows keep running. `crash.elf` is the demo that tries to read kernel memory and dies.

## What is left out, on purpose

There is no network. The verb list includes names like `fetch` and `voyage` so the language has a place for them. They answer "not in this OS yet."

There is no package installer. Putting a new ELF in `bin/` does not grow a desk icon. The icons are a table in `user/chaoswm.c`. That is the next piece of desk work, not a hidden feature.

There is no UEFI boot, no USB mass-storage stack, and no multi-sector ATA. The disk path is IDE, primary master, one 512-byte sector per command.

Doom's music is not played. Effects are mixed into the HDA stream. The desk does not stretch Doom to fill 800×600. Fullscreen letterboxes the 640×400 picture.

That is the whole machine: a boot sector, a kernel that owns the hardware, and programs that ask for what they need.
