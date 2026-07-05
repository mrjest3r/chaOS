C_SOURCES = $(wildcard kernel/*.c drivers/*.c cpu/*.c libc/*.c)
HEADERS = $(wildcard kernel/*.h drivers/*.h cpu/*.h libc/*.h)
# Nice syntax for file extension replacement, plus our hand-written asm objects
OBJ = ${C_SOURCES:.c=.o} cpu/interrupt.o cpu/paging_asm.o cpu/context.o cpu/usermode.o

# Change this if your cross-compiler is somewhere else
CROSS = /usr/local/i386elfgcc/bin
CC = ${CROSS}/i386-elf-gcc
LD = ${CROSS}/i386-elf-ld
GDB = ${CROSS}/i386-elf-gdb
# -g: Use debugging symbols in gcc
CFLAGS = -g -ffreestanding -Wall -Wextra -fno-exceptions -fno-builtin -m32

# ---- user-space programs -------------------------------------------------
# User programs are compiled and linked separately from the kernel, then
# injected into the disk image so the kernel's ELF loader can run them.
UFLAGS = -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
         -fno-asynchronous-unwind-tables -m32 -Wall -Wextra
HOSTCC = cc
USER_PROGS = user/hello.elf

# First rule is run by default
os-image.bin: boot/bootsect.bin kernel.bin
	cat $^ > os-image.bin
	# Pad to a standard 1.44 MiB floppy so sector reads never run past EOF
	truncate -s 1474560 os-image.bin

# '--oformat binary' deletes all symbols as a collateral, so we don't need
# to 'strip' them manually on this case
kernel.bin: boot/kernel_entry.o ${OBJ}
	${LD} -o $@ -Ttext 0x10000 $^ --oformat binary

# Used for debugging purposes
kernel.elf: boot/kernel_entry.o ${OBJ}
	${LD} -o $@ -Ttext 0x10000 $^ 

# Persistent disk image used by the ATA driver / filesystem (primary master).
# Created once; delete it to reset the filesystem.
disk.img:
	truncate -s 16M disk.img

run: os-image.bin disk.img programs
	qemu-system-i386 -boot a -fda os-image.bin -hda disk.img -serial stdio

# Automated headless test build. Compiles with -DAUTOTEST so the kernel runs
# its self-tests and then powers off QEMU through the isa-debug-exit device.
# No -no-shutdown and nothing to force-kill: QEMU stops on its own.
test: CFLAGS += -DAUTOTEST
test: clean os-image.bin disk.img programs
	-timeout -s KILL 30 qemu-system-i386 -boot a -fda os-image.bin -hda disk.img \
		-display none -serial stdio \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04

# ---- user program + disk image tooling -----------------------------------
# User C objects use the freestanding user flags, not the kernel flags.
user/%.o: user/%.c user/ulib.h kernel/syscall.h
	${CC} ${UFLAGS} -c $< -o $@

# Link a user program: crt0 first (entry), then the program, then the lib.
user/%.elf: user/%.o user/crt0.o user/ulib.o user/linker.ld
	${LD} -T user/linker.ld -o $@ user/crt0.o $< user/ulib.o

# Build all user programs and inject them into the disk image. The injector is
# compiled to and run from a temporary file: the project may live on a
# filesystem (e.g. FAT) that refuses to grant execute permission to files
# created in the workspace.
programs: disk.img ${USER_PROGS}
	@tmp=`mktemp /tmp/chaos-mkfs.XXXXXX` && \
	 ${HOSTCC} -O2 -Wall -o $$tmp tools/mkfs.c && chmod +x $$tmp && \
	 $$tmp disk.img ${USER_PROGS} && rm -f $$tmp

# Open the connection to qemu and load our kernel-object file with symbols
debug: os-image.bin kernel.elf disk.img
	qemu-system-i386 -s -boot a -fda os-image.bin -hda disk.img -d guest_errors,int &
	${GDB} -ex "target remote localhost:1234" -ex "symbol-file kernel.elf"

# Generic rules for wildcards
# To make an object, always compile from its .c
%.o: %.c ${HEADERS}
	${CC} ${CFLAGS} -c $< -o $@

%.o: %.asm
	nasm $< -f elf -o $@

%.bin: %.asm
	nasm $< -f bin -o $@

clean:
	rm -rf *.bin *.dis *.o os-image.bin *.elf
	rm -rf kernel/*.o boot/*.bin drivers/*.o boot/*.o cpu/*.o libc/*.o
	rm -rf user/*.o user/*.elf
