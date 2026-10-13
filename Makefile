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
INITRD_PHYS = 212992
INITRD_MAX  = 360448

# ---- user-space programs -------------------------------------------------
# User programs are compiled and linked separately from the kernel, then
# injected into the disk image so the kernel's ELF loader can run them.
UFLAGS = -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
         -fno-asynchronous-unwind-tables -m32 -Wall -Wextra
HOSTCC = cc
USER_PROGS = user/hello.elf user/spin.elf user/crash.elf user/greet.elf user/terminal.elf user/gate.elf user/args.elf user/pipedemo.elf user/pipekid.elf user/mousedemo.elf user/chaoswm.elf user/scribe.elf user/folio.elf user/badptr.elf

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
boot/bootsect.bin: boot/bootsect.asm boot/disk.asm boot/gdt.asm boot/vbe.asm boot/switch_pm.asm initrd.bin
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

# Persistent disk image (primary master). About 1 GiB for OS files.
# Grown in place when an older image is smaller; delete it to reset.
.PHONY: ensure-disk
ensure-disk:
	@if [ ! -f disk.img ] || [ $$(stat -c%s disk.img) -lt 1073741824 ]; then truncate -s 1G disk.img; fi

disk.img: ensure-disk
	@:

QEMU = qemu-system-i386 -m 1024 -audiodev pa,id=snd \
	-device intel-hda -device hda-duplex,audiodev=snd \
	-usb -device usb-audio,audiodev=snd,msos-desc=off

run: os-image.bin disk.img programs
	${QEMU} -vga std -boot a -fda os-image.bin -hda disk.img -serial stdio

# Same bytes as os-image.bin, named for VMware/VirtualBox floppy attachment.
chaos.flp: os-image.bin
	cp os-image.bin chaos.flp

# CD for VirtualBox. El Torito floppy emulation (fd1.4): the BIOS presents
# chaos.flp as drive 0x00, which is the geometry boot/disk.asm already uses.
# Do not pass -no-emul-boot (2048-byte CD sectors) or -boot-info-table (it
# patches the boot sector). Firmware must be BIOS, not EFI.
chaos.iso: chaos.flp
	rm -rf isodir
	mkdir isodir
	cp chaos.flp isodir/chaos.flp
	xorriso -as mkisofs -o $@ -V CHAOS -b chaos.flp -c boot.cat isodir
	rm -rf isodir
	@xorriso -indev $@ -report_el_torito plain 2>/dev/null | grep -q fd1.4 || \
	  { echo "error: chaos.iso is not a 1.44 MB El Torito floppy"; exit 1; }

# VirtualBox helper: floppy + IDE disk. Optional MAKE_VBOX_VM=1 registers a VM.
vbox: chaos.flp
	$(MAKE) -s vbox-disk
	@echo ""
	@echo "VirtualBox: floppy (chaos.flp) + IDE disk. Firmware BIOS. Display VBoxVGA."
	@echo "  MAKE_VBOX_VM=1 make vbox   creates the VM (copies media to ~/chaos-vbox-media)."
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

# Boot the floppy image the way VMware should.
run-flp: chaos.flp disk.img programs
	${QEMU} -vga std -boot a -fda chaos.flp -hda disk.img -serial stdio

# Automated headless test build. Compiles with -DAUTOTEST so the kernel runs
# its self-tests and then powers off QEMU through the isa-debug-exit device.
# tools/qemu-guard.sh gives QEMU a monitor socket and sends `quit` if the guest
# hangs. `make qemu-kill` does the same for anything already left behind.
# Do not use timeout(1): it puts QEMU in another process group, and this
# environment often cannot SIGKILL those orphans.
#
# Runs twice: once with the IDE disk (persistent FS), once with no disk at all
# so the whole suite must work from the initrd on the RAM-backed filesystem —
# exactly what a bare ISO / USB stick boot gets.
test: CFLAGS += -DAUTOTEST
test: clean os-image.bin disk.img programs
	@dtmp=$$(mktemp); cp disk.img $$dtmp; \
	bash tools/qemu-guard.sh 45 ${QEMU} -boot a -fda os-image.bin \
		-drive format=raw,file=$$dtmp,if=ide \
		-display none -serial stdio \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04; \
	ec=$$?; rm -f $$dtmp; exit 0
	@echo "=== second run: no IDE disk (initrd + RAM filesystem) ==="
	@bash tools/qemu-guard.sh 45 ${QEMU} -boot a -fda os-image.bin \
		-display none -serial stdio \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04 \
		| grep -E '^\[(test|initrd|fs)\]|FAIL|PASS'; exit 0

# Stop headless QEMU via its monitor socket (no reboot, no SIGKILL required).
qemu-kill:
	bash tools/qemu-reap.sh

# Just the no-disk variant (quicker when iterating on initrd / fs code).
test-ram: CFLAGS += -DAUTOTEST
test-ram: clean os-image.bin
	@bash tools/qemu-guard.sh 45 ${QEMU} -boot a -fda os-image.bin \
		-display none -serial stdio \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04; exit 0

# ---- user program + disk image tooling -----------------------------------
# User C objects use the freestanding user flags, not the kernel flags.
# terminal.elf must stay within one filesystem slot (32 KiB). -Os keeps the
# directory verbs inside that budget; the pattern rule would not.
user/terminal.o: user/terminal.c user/ulib.h user/verbs.h user/accounts.h user/ugfx.h user/utext.h user/attune.h kernel/syscall.h kernel/fb_user.h kernel/event_user.h
	${CC} ${UFLAGS} -Os -c $< -o $@

# The desk must stay inside the initrd file cap (32 KiB).
user/chaoswm.o: user/chaoswm.c user/ulib.h user/ugfx.h user/utext.h user/attune.h kernel/syscall.h kernel/fb_user.h kernel/event_user.h kernel/frame_user.h
	${CC} ${UFLAGS} -Os -c $< -o $@

user/%.o: user/%.c user/ulib.h user/ugfx.h user/utext.h user/attune.h kernel/syscall.h kernel/fb_user.h kernel/event_user.h
	${CC} ${UFLAGS} -c $< -o $@

# Link a user program: crt0 first (entry), then the program, then the libs.
user/%.elf: user/%.o user/crt0.o user/ulib.o user/ugfx.o user/utext.o user/linker.ld
	${LD} -T user/linker.ld -o $@ user/crt0.o $< user/ulib.o user/ugfx.o user/utext.o

# P5.1 host SDK: cross-build one C file and drop the ELF onto disk.img.
#   make sdk SRC=tools/sdk/examples/beacon.c
sdk: disk.img
	@test -n "$(SRC)" || { echo "usage: make sdk SRC=path/to/prog.c"; exit 1; }
	bash tools/sdk/chaos-sdk "$(SRC)"

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
	bash tools/qemu-reap.sh
	${QEMU} -s -boot a -fda os-image.bin -hda disk.img -d guest_errors,int &
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
