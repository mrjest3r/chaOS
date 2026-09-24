C_SOURCES = $(wildcard kernel/*.c drivers/*.c cpu/*.c libc/*.c)
HEADERS = $(wildcard kernel/*.h drivers/*.h cpu/*.h libc/*.h)
# Nice syntax for file extension replacement, plus our hand-written asm objects
OBJ = ${C_SOURCES:.c=.o} cpu/interrupt.o cpu/paging_asm.o cpu/context.o cpu/usermode.o

# Change this if your cross-compiler is somewhere else
CROSS = /usr/local/i386elfgcc/bin
CC = ${CROSS}/i386-elf-gcc
LD = ${CROSS}/i386-elf-ld
GDB = ${CROSS}/i386-elf-gdb
NM = ${CROSS}/i386-elf-nm
# -g: Use debugging symbols in gcc
CFLAGS = -g -ffreestanding -Wall -Wextra -fno-exceptions -fno-builtin -m32

# ---- boot image layout (mirror of cpu/memlayout.h) -----------------------
# Sector 0 boot sector, then KERNEL_SECTORS of kernel.bin (zero-padded; loaded
# at 0x10000), then initrd.bin (loaded at INITRD_PHYS 0x30000). The kernel's
# .bss must also end below 0x30000 and the initrd below 0x88000.
KERNEL_SECTORS = 256
INITRD_PHYS = 196608
INITRD_MAX  = 360448

# ---- user-space programs -------------------------------------------------
# User programs are compiled and linked separately from the kernel, then
# injected into the disk image so the kernel's ELF loader can run them.
UFLAGS = -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
         -fno-asynchronous-unwind-tables -m32 -Wall -Wextra
HOSTCC = cc
USER_PROGS = user/hello.elf user/spin.elf user/crash.elf user/greet.elf user/ush.elf user/args.elf user/pipedemo.elf user/pipekid.elf user/mousedemo.elf user/chaoswm.elf user/scribe.elf user/folio.elf user/badptr.elf

# First rule is run by default. The image is self-contained: kernel + initrd
# with every user program, so it boots to a full system with no disk attached
# (RAM-backed filesystem) and seeds a blank IDE disk on first boot.
os-image.bin: boot/bootsect.bin kernel.bin kernel.elf initrd.bin
	@ksz=$$(stat -c%s kernel.bin); kmax=$$(( ${KERNEL_SECTORS} * 512 )); \
	if [ $$ksz -gt $$kmax ]; then \
	  echo "error: kernel.bin is $$ksz bytes, budget is $$kmax (raise KERNEL_SECTORS + INITRD_PHYS)"; exit 1; fi
	@bss=$$(${NM} kernel.elf | awk '/ __bss_end$$/ {print $$1}'); end=$$(( 0x$$bss - 0xC0000000 )); \
	if [ $$end -gt ${INITRD_PHYS} ]; then \
	  echo "error: kernel .bss ends at $$end, overlaps initrd at ${INITRD_PHYS}"; exit 1; fi
	cat boot/bootsect.bin kernel.bin > os-image.bin
	truncate -s $$(( 512 + ${KERNEL_SECTORS} * 512 )) os-image.bin
	cat initrd.bin >> os-image.bin
	# Pad to a standard 1.44 MiB floppy so sector reads never run past EOF
	truncate -s 1474560 os-image.bin

# All user programs packed for the boot sector to load (tools/mkinitrd.c).
initrd.bin: ${USER_PROGS} tools/mkinitrd.c
	@tmp=`mktemp /tmp/chaos-mkinitrd.XXXXXX` && \
	 ${HOSTCC} -O2 -Wall -o $$tmp tools/mkinitrd.c && chmod +x $$tmp && \
	 $$tmp $@ ${USER_PROGS} && rm -f $$tmp
	@sz=$$(stat -c%s $@); if [ $$sz -gt ${INITRD_MAX} ]; then \
	  echo "error: initrd.bin is $$sz bytes, INITRD_MAX is ${INITRD_MAX}"; rm -f $@; exit 1; fi

# The boot sector needs to know how many sectors to pull for each part.
boot/bootsect.bin: boot/bootsect.asm boot/disk.asm boot/print.asm boot/gdt.asm boot/vbe.asm boot/switch_pm.asm boot/32bit_print.asm boot/print_hex.asm initrd.bin
	nasm boot/bootsect.asm -f bin -DKERNEL_SECTORS=${KERNEL_SECTORS} \
	  -DINITRD_SECTORS=$$(( ( $$(stat -c%s initrd.bin) + 511 ) / 512 )) -o $@

# Higher-half kernel: boot/kernel.ld links the boot stub at 0x10000 (its load
# address) and the rest of the kernel at 0xC0000000 + phys. '--oformat binary'
# deletes all symbols as a collateral, so we don't need to 'strip' them.
kernel.bin: boot/kernel_entry.o ${OBJ} boot/kernel.ld
	${LD} -o $@ -T boot/kernel.ld boot/kernel_entry.o ${OBJ} --oformat binary

# Used for debugging purposes
kernel.elf: boot/kernel_entry.o ${OBJ} boot/kernel.ld
	${LD} -o $@ -T boot/kernel.ld boot/kernel_entry.o ${OBJ}

# Persistent disk image used by the ATA driver / filesystem (primary master).
# Created once; delete it to reset the filesystem.
disk.img:
	truncate -s 16M disk.img

run: os-image.bin disk.img programs
	qemu-system-i386 -boot a -fda os-image.bin -hda disk.img -serial stdio

# Bootable CD via El Torito *floppy emulation*: BIOS presents the embedded
# 1.44 MiB image as drive 0 (dl=0), so boot/disk.asm CHS geometry works.
# Explicit boot catalog (-c) is required by El Torito / VirtualBox BIOS.
# Prefer genisoimage; fall back to xorriso. VMware CD boot is unreliable —
# use chaos.flp there (see README).
chaos.iso: os-image.bin
	@sz=$$(stat -c%s os-image.bin); \
	if [ "$$sz" != "1474560" ]; then \
	  echo "error: os-image.bin must be exactly 1474560 bytes (got $$sz)"; exit 1; \
	fi
	rm -rf isodir && mkdir -p isodir
	cp os-image.bin isodir/os-image.bin
	if command -v genisoimage >/dev/null 2>&1; then \
	  genisoimage -quiet -input-charset iso8859-1 -R -V "CHAOS" \
	    -b os-image.bin -c boot.cat -o chaos.iso isodir; \
	elif command -v xorriso >/dev/null 2>&1; then \
	  xorriso -as mkisofs -R -V "CHAOS" \
	    -b os-image.bin -c boot.cat -o chaos.iso isodir; \
	else \
	  echo "error: need genisoimage or xorriso to build chaos.iso"; exit 1; \
	fi
	rm -rf isodir
	@echo "chaos.iso ready ($$(stat -c%s chaos.iso) bytes) — El Torito floppy emu"

# Same bytes as os-image.bin, named for VMware/VirtualBox floppy attachment.
chaos.flp: os-image.bin
	cp os-image.bin chaos.flp

# Interactive ISO + floppy + FS image (never use after `make test` without clean —
# AUTOTEST contaminates objects). Prefer: make clean && make iso
iso: chaos.iso chaos.flp disk.img programs

# VirtualBox helper: rebuild interactive ISO, wrap disk as VDI, print attach steps.
# Optional: MAKE_VBOX_VM=1 creates/registers a ready-to-start VM (needs vboxdrv).
vbox: iso
	$(MAKE) -s vbox-disk
	@echo ""
	@echo "VirtualBox: prefer MAKE_VBOX_VM=1 (copies media to ~/chaos-vbox-media)."
	@echo "  Floppy first + IDE disk + ISO; Display VBoxVGA; Firmware BIOS."
	@if [ "$(MAKE_VBOX_VM)" = "1" ]; then bash tools/vbox-setup.sh; fi

vbox-disk: disk.img programs
	@command -v VBoxManage >/dev/null 2>&1 || { echo "VBoxManage not found"; exit 1; }
	rm -f chaos-disk.vdi
	VBoxManage convertfromraw disk.img chaos-disk.vdi --format VDI
	@echo "chaos-disk.vdi ready (IDE primary master)"

# Write chaos.flp to a USB stick for legacy BIOS USB-FDD boot (P3.1).
# Example: make write-usb USB=/dev/sdX
write-usb: chaos.flp
	@test -n "$(USB)" || { echo "usage: make write-usb USB=/dev/sdX"; exit 1; }
	bash tools/write-usb.sh

# Boot from the ISO in QEMU (same setup a desktop VM would use).
run-iso: iso
	qemu-system-i386 -boot d -cdrom chaos.iso -hda disk.img -serial stdio

# ISO only — no disk.img. Programs come from the initrd; files do not persist.
run-iso-bare: iso
	qemu-system-i386 -boot d -cdrom chaos.iso -serial stdio

# Quick headless smoke: ISO must reach tasking on COM1. Copies disk.img so a
# leftover QEMU write-lock on the workspace image cannot break the test.
test-iso: iso
	@tmp=$$(mktemp); dtmp=$$(mktemp); cp disk.img $$dtmp; \
	timeout -s KILL 12 qemu-system-i386 -boot d -cdrom chaos.iso \
	  -drive format=raw,file=$$dtmp,if=ide \
	  -display none -serial file:$$tmp >/dev/null 2>&1 || true; \
	rm -f $$dtmp; \
	if grep -q '\[boot\] serial ready' $$tmp && grep -q 'tasking initialized' $$tmp \
	   && grep -q '\[initrd\]' $$tmp; then \
	  echo "[test-iso] PASS (El Torito CD boot + kernel + initrd)"; rm -f $$tmp; \
	else \
	  echo "[test-iso] FAIL — serial log:"; cat $$tmp; rm -f $$tmp; exit 1; \
	fi

# Same ISO, no IDE disk: must seed the RAM filesystem from the initrd so
# `make run-iso` without a disk still has every user program (the old mismatch
# vs `make run`, which always attached disk.img).
test-iso-bare: iso
	@tmp=$$(mktemp); \
	timeout -s KILL 12 qemu-system-i386 -boot d -cdrom chaos.iso \
	  -display none -serial file:$$tmp >/dev/null 2>&1 || true; \
	if grep -q '\[boot\] serial ready' $$tmp && grep -q 'tasking initialized' $$tmp \
	   && grep -q '\[initrd\]' $$tmp && grep -q '\[fs\] no IDE disk' $$tmp; then \
	  echo "[test-iso-bare] PASS (ISO-only = initrd + RAM fs)"; rm -f $$tmp; \
	else \
	  echo "[test-iso-bare] FAIL — serial log:"; cat $$tmp; rm -f $$tmp; exit 1; \
	fi

# Boot the floppy image the way VMware should (no CD El Torito involved).
run-flp: chaos.flp disk.img programs
	qemu-system-i386 -boot a -fda chaos.flp -hda disk.img -serial stdio

# Automated headless test build. Compiles with -DAUTOTEST so the kernel runs
# its self-tests and then powers off QEMU through the isa-debug-exit device.
# No -no-shutdown and nothing to force-kill: QEMU stops on its own.
#
# Runs twice: once with the IDE disk (persistent FS), once with no disk at all
# so the whole suite must work from the initrd on the RAM-backed filesystem —
# exactly what a bare ISO / USB stick boot gets.
test: CFLAGS += -DAUTOTEST
test: clean os-image.bin disk.img programs
	@dtmp=$$(mktemp); cp disk.img $$dtmp; \
	timeout -s KILL 30 qemu-system-i386 -boot a -fda os-image.bin \
		-drive format=raw,file=$$dtmp,if=ide \
		-display none -serial stdio \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04; \
	ec=$$?; rm -f $$dtmp; exit 0
	@echo "=== second run: no IDE disk (initrd + RAM filesystem) ==="
	@timeout -s KILL 30 qemu-system-i386 -boot a -fda os-image.bin \
		-display none -serial stdio \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04 \
		| grep -E '^\[(test|initrd|fs)\]|FAIL|PASS'; exit 0

# Just the no-disk variant (quicker when iterating on initrd / fs code).
test-ram: CFLAGS += -DAUTOTEST
test-ram: clean os-image.bin
	@timeout -s KILL 30 qemu-system-i386 -boot a -fda os-image.bin \
		-display none -serial stdio \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04; exit 0

# ---- user program + disk image tooling -----------------------------------
# User C objects use the freestanding user flags, not the kernel flags.
user/%.o: user/%.c user/ulib.h user/ugfx.h user/utext.h kernel/syscall.h kernel/fb_user.h kernel/event_user.h
	${CC} ${UFLAGS} -c $< -o $@

# Link a user program: crt0 first (entry), then the program, then the libs.
user/%.elf: user/%.o user/crt0.o user/ulib.o user/ugfx.o user/utext.o user/linker.ld
	${LD} -T user/linker.ld -o $@ user/crt0.o $< user/ulib.o user/ugfx.o user/utext.o

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
	rm -rf *.bin *.dis *.o os-image.bin *.elf *.iso *.flp isodir initrd.bin
	rm -rf kernel/*.o boot/*.bin drivers/*.o boot/*.o cpu/*.o libc/*.o
	rm -rf user/*.o user/*.elf
