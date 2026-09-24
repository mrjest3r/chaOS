# chaOS host SDK (Path A corner-cut)

Build user ELFs on the **host** with the same `i386-elf` toolchain the kernel
uses, then inject them into `disk.img`. There is no in-OS compiler.

## Build one program

From the repo root (same flags as `Makefile` `user/%.elf`):

```bash
CROSS=/path/to/i386elfgcc/bin   # if needed
$CROSS/i386-elf-gcc -m32 -ffreestanding -fno-pic -fno-stack-protector \
  -nostdlib -Wall -Wextra -Iuser -c myprog.c -o myprog.o
# crt0 is assembled via nasm (see Makefile); reuse user/crt0.o from `make`
$CROSS/i386-elf-gcc -m32 -ffreestanding -fno-pic -c user/ulib.c -o user/ulib.o
$CROSS/i386-elf-ld -m elf_i386 -T user/linker.ld -o myprog.elf \
  user/crt0.o myprog.o user/ulib.o
```

Or add `user/myprog.elf` to `USER_PROGS` in the top-level Makefile and run
`make programs`.

## Inject onto the deck disk

```bash
make programs          # rebuilds disk.img with USER_PROGS
# or, after building myprog.elf:
#   host mkfs tool packs named files into disk.img (see Makefile `programs`)
```

On the deck: `summon myprog.elf` / `ignite myprog.elf`, or edit sources with
`scribe` and keep binaries host-built until a translator lands (P5).

## Workflow (P1.6)

1. Host: edit / translate / cross-compile → `*.elf`
2. Host: `make programs` (or `make iso`)
3. Deck: `scribe notes.txt` for onboard text; `ignite prog.elf` to run
