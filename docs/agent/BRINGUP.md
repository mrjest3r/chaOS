# chaOS — Physical board bring-up (P3.1)

**Firmware freeze: legacy BIOS only (not UEFI).**  
The boot path is `boot/bootsect.asm` → real-mode disk load → optional VBE → protected mode → higher-half kernel. There is no UEFI / GRUB stage. Do not add an EFI path unless STATUS reopens it.

## Exit criteria (P3.1)

On the chosen **x86** board:

1. Banner visible (VGA text and/or COM1 serial).
2. Keyboard works at the `>` prompt.
3. Prefer also: `ls` / `exec hello.elf` if a suitable disk is attached (may slip to P3.2).

## Board constraints (current software)

| Need | Why |
|------|-----|
| BIOS boot (CSM / Legacy), not pure UEFI | Boot sector + int 0x13 |
| Optional COM1 (I/O `0x3F8`) | Debug when display fails |
| Storage: ATA primary master at `0x1F0`, or none | Driver is single-sector PIO IDE; SATA AHCI will not work until P3.2 grows a driver |
| PS/2 keyboard (or USB with legacy PS/2 emulation) | Current kbd driver |
| Optional VBE 32 bpp LFB | Soft console; else VGA text (same as many VirtualBox hosts) |

Good first targets: old ThinkPad / mini-ITX with IDE or “Legacy IDE”, or any box that can boot a 1.44 MiB USB-FDD image. Avoid “UEFI only” sticks until a separate bootloader exists.

## Host media (build)

```bash
make clean && make iso    # chaos.flp + chaos.iso + disk.img + user ELFs
make vbox-disk            # optional: chaos-disk.vdi for VMs
```

| Artifact | Use on hardware |
|----------|-----------------|
| `chaos.flp` (1474560 bytes) | Best: write to USB as floppy image; BIOS “USB Floppy” / FDD emulation. Self-contained: kernel + initrd with all programs |
| `chaos.iso` | Burn / write as CD/DVD; El Torito floppy emulation (same self-contained image) |
| `disk.img` | Optional. Raw IDE disk image for **persistent** files (dd to a spare disk/partition carefully). Any blank IDE disk works too: the kernel formats and seeds it from the initrd on first boot. Without a disk everything runs from a RAM filesystem |

### Write floppy image to USB (destructive)

```bash
# Identify the stick (e.g. /dev/sdX) — wrong device wipes a disk.
lsblk
make write-usb USB=/dev/sdX    # writes chaos.flp only; requires confirmation
```

Or manually:

```bash
sudo dd if=chaos.flp of=/dev/sdX bs=512 conv=fsync status=progress
```

### Serial on the host

115200 is **not** used — chaOS COM1 is polled at the divisor set in `drivers/serial.c` (**38400** 8N1). On Linux:

```bash
picocom -b 38400 /dev/ttyUSB0
# or: screen /dev/ttyUSB0 38400
```

Expect `[boot] serial ready` then the banner (also mirrored to the screen when VGA works).

## Bring-up checklist

- [ ] Board chosen; BIOS set to **Legacy / CSM**, Secure Boot off
- [ ] Boot order: USB-FDD or CD before HDD
- [ ] `make clean && make iso` on host; write `chaos.flp` (or burn ISO)
- [ ] Power on → see `chaOS boot` / banner / COM1 lines
- [ ] Type `help`, `frames`, `ls` (if disk attached)
- [ ] Note in STATUS: board model, boot medium, VGA vs serial, any quirks
- [ ] Mark P3.1 complete in STATUS when banner + keyboard confirmed

## Failure hints

| Symptom | Likely cause |
|---------|----------------|
| Nothing at all | UEFI-only boot; wrong USB mode (need FDD emu); bad dd target |
| `Disk read error` | BIOS geometry / USB not emulating 1.44 MiB floppy |
| Banner then hang | Less common; capture COM1 log |
| Banner says "No IDE disk: files live in RAM" | Expected without an IDE primary master (SATA without legacy mode). Programs still run from the initrd; only persistence is missing |
| No FB, text OK | BIOS lacks 32 bpp VBE — acceptable for P3.1 |

## After P3.1

- **P3.2** — persist FS across reboot on real storage (may need AHCI or USB bulk).
- **P3.3** — deck keymap / chords in LEXICON.

Related: [`MAP.md`](MAP.md) arch API, [`README.md`](../../README.md) VM recipes, `tools/vbox-setup.sh` for VirtualBox dry-runs.
