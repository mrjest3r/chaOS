# chaOS — Physical board bring-up (P3.1)

**Firmware freeze: legacy BIOS only (not UEFI).**  
The boot path is `boot/bootsect.asm` → real-mode disk load → optional VBE → protected mode → higher-half kernel. There is no UEFI / GRUB stage. Do not add an EFI path unless STATUS reopens it.

## Published minimum (2026-09-29)

This is the floor for a machine that runs chaOS. Faster CPUs, more RAM, and larger disks are in range. The kernel does not use 1 GB yet. That is the work list, not a reason to raise the floor.

| Floor | Value |
|-------|--------|
| CPU | 1 GHz, x86 |
| Firmware | Legacy BIOS / CSM |
| RAM | 1 GB |
| Disk | about 1 GB for the OS files (the size may change) |

Screen and keyboard are not extra lines. A BIOS PC is expected to provide a display and a keyboard the current drivers accept (VBE 800×600×32, PS/2 or BIOS USB-keyboard emulation). More RAM than 1 GB stays unused until the map grows past the cap below. The boot floppy stays small. The 1 GB lives on the IDE disk.

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
| 800×600×32 LFB | BIOS VBE, or Bochs DISPI if the BIOS has no 32 bpp mode (QEMU and VirtualBox VBoxVGA). VGA text only if both fail |

Good first targets: old ThinkPad / mini-ITX with IDE or “Legacy IDE”, or any box that can boot a 1.44 MiB USB-FDD image. Avoid “UEFI only” sticks until a separate bootloader exists.

## Host media (build)

```bash
make chaos.flp disk.img programs
make vbox-disk            # optional: chaos-disk.vdi for VMs
```

| Artifact | Use on hardware |
|----------|-----------------|
| `chaos.flp` (1474560 bytes) | Best: write to USB as floppy image; BIOS “USB Floppy” / FDD emulation. Self-contained: kernel + initrd with all programs |
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
- [ ] `make chaos.flp` on the host; write `chaos.flp`
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

## G7 — Display freeze (software)

The deck GUI (`chaoswm`) needs a **32 bpp** linear framebuffer. This is the
mode the boot path will ask for on the board; do not change it without a
STATUS note.

| Priority | Mode | VBE | Notes |
|----------|------|-----|--------|
| **1 (chosen)** | **800×600×32** | **0x115** + LFB bit (`0x4115`) | QEMU BIOS and Bochs DISPI both land here. Target for the x86 board. |
| 2 | 1024×768×32 | 0x118 | Only if 0x115 is missing |
| 3 | 640×480×32 | 0x112 | Last 32 bpp try |
| fail | VGA text 80×25 | — | Soft console + kernel shell; no desk. Acceptable for P3.1 proof, not for G6. |

Implementation (`boot/vbe.asm` + `drivers/fb.c`):

1. Real-mode `int 0x10` AX=0x4F02 tries 0x115 / 0x118 / 0x112, 32 bpp + LFB only (24 bpp is rejected).
2. Compact info block at `0x5000` (`CHAOSFB`).
3. If BIOS VBE failed, `fb_init` forces **800×600×32** via Bochs DISPI. QEMU std VGA and VirtualBox VBoxVGA both take this path.

On COM1 you should see one of:

```
[fb] BIOS VBE 32bpp
[fb] 800x600 ...
```

or `[fb] DISPI 32bpp ...` then the same 800×600 line. VGA text is the leftover when neither BIOS VBE nor DISPI works. The string `[fb] no 32bpp VBE — VGA text console` is no longer what VirtualBox prints; that machine takes the DISPI line.

**Board ask:** a BIOS 800×600×32 LFB, or a Bochs-compatible DISPI. If both are missing, the desk does not start and the VGA text console does. `q` and F1 do not leave the desk.

## After P3.1

- **P3.2** — persist FS across reboot on real storage (may need AHCI or USB bulk).
- **P3.3** — deck keymap / chords in LEXICON.

Related: [`MAP.md`](MAP.md) arch API, [`README.md`](../../README.md) VM recipes, `tools/vbox-setup.sh` for VirtualBox dry-runs.
