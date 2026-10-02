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

The filesystem stores at most 1 MiB under a name of at most 23 characters.
The volume is about 1 GiB. A program and its heap live in a 64 MiB window;
`SYS_BRK` allows 32 MiB of heap.
`make sdk` prints the size and stops when the ELF is larger. There is no
libc and no heap syscall. A program and its globals must also fit in the
64 MiB user window (`USER_PROG_LIMIT` in `cpu/vmm.h`).

Doom does not install. The engine is a hosted C program (libc, files) and the
shareware WAD is larger than one file.

## Boot image vs disk

`make programs` still packs the programs in the Makefile into the boot image
and the disk. `make sdk` only adds a file to `disk.img`. A boot with no IDE
disk will not see it. `make run` attaches the disk, so `summon` finds it.
