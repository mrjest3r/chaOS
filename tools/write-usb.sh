#!/usr/bin/env bash
# Write chaos.flp to a block device (USB stick as BIOS floppy image).
# Usage: USB=/dev/sdX bash tools/write-usb.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="${IMG:-$ROOT/chaos.flp}"
DEV="${USB:-}"

if [[ -z "$DEV" ]]; then
  echo "usage: USB=/dev/sdX make write-usb" >&2
  echo "  (writes chaos.flp — destructive to that device)" >&2
  exit 1
fi
if [[ ! -f "$IMG" ]]; then
  echo "missing $IMG — run: make clean && make iso" >&2
  exit 1
fi
if [[ ! -b "$DEV" ]]; then
  echo "not a block device: $DEV" >&2
  exit 1
fi

sz=$(stat -c%s "$IMG")
if [[ "$sz" != "1474560" ]]; then
  echo "error: $IMG must be 1474560 bytes (got $sz)" >&2
  exit 1
fi

echo "About to write $IMG ($sz bytes) -> $DEV"
lsblk -o NAME,SIZE,TYPE,MOUNTPOINT "$DEV" || true
if [[ "${CONFIRM:-}" != "yes" ]]; then
  printf "Type the device path again to confirm: "
  read -r ans
  if [[ "$ans" != "$DEV" ]]; then
    echo "aborted."
    exit 1
  fi
fi

sudo dd if="$IMG" of="$DEV" bs=512 conv=fsync status=progress
echo "done. Boot the board with Legacy BIOS / USB-FDD."
