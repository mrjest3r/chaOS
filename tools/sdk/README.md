# chaOS host SDK (P5.1)

One command on the host cross-builds a C file into a chaOS ELF and writes it
onto `disk.img`. The deck does not have a compiler. It only runs the binary.

```bash
make sdk SRC=tools/sdk/examples/beacon.c
```

From the terminal: `summon beacon.elf`.

`SRC` is any freestanding C file that includes `ulib.h` and uses the syscall
wrappers (`uprint`, `uwritefile`, `ufb`, …). A second argument renames the
installed file:

```bash
bash tools/sdk/chaos-sdk prog.c myprog.elf
```

The toolchain is `i386-elf-gcc` / `i386-elf-ld` / `nasm`. If they are not in
`/usr/local/i386elfgcc/bin`, set `CROSS` to that `bin` directory.

## What the command will refuse

`make sdk` stops when the ELF is over 1 MiB or the name is longer than 23
characters. That 1 MiB limit is the tool, not the filesystem. A disk file
may be 32 MiB (`FS_MAX_FILESIZE`). The volume is about 1 GiB and holds
10000 names.

A program and its heap live in a 64 MiB window (`USER_BASE` through
`USER_STACK_TOP`). `SYS_BRK` grows the heap by at most 48 MiB
(`USER_HEAP_MAX` in `cpu/vmm.h`). There is no hosted libc. Programs that
need one cannot be built this way.

Doom does not install through `make sdk`. The engine is hosted C. The WAD
fits one filesystem file (under 32 MiB) and is placed on the disk by hand.

## Boot image vs disk

`make programs` still packs the programs in the Makefile into the boot image
and the disk. `make sdk` only adds a file to `disk.img`. A boot with no IDE
disk will not see it. `make run` attaches the disk, so `summon` finds it.
