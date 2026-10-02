#!/usr/bin/env bash
# Create / refresh a VirtualBox VM wired for chaOS.
# Copies media to $HOME/chaos-vbox-media (avoids spaces in host paths).
# Boots the floppy image. IDE disk is optional persistence.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VM_NAME="${CHAOS_VBOX_VM:-chaOS}"
MEDIA="${CHAOS_VBOX_MEDIA:-$HOME/chaos-vbox-media}"
FLP_SRC="$ROOT/chaos.flp"
VDI_SRC="$ROOT/chaos-disk.vdi"

if ! command -v VBoxManage >/dev/null 2>&1; then
  echo "VBoxManage not found" >&2
  exit 1
fi

for f in "$FLP_SRC" "$VDI_SRC"; do
  if [[ ! -f "$f" ]]; then
    echo "missing: $f" >&2
    echo "Run: make chaos.flp && make vbox-disk" >&2
    exit 1
  fi
done

# Unregister first. --delete removes attached media files, so refresh copies
# only AFTER the old VM is gone.
if VBoxManage showvminfo "$VM_NAME" >/dev/null 2>&1; then
  echo "Removing existing VM '$VM_NAME'..."
  VBoxManage controlvm "$VM_NAME" poweroff >/dev/null 2>&1 || true
  sleep 1
  VBoxManage unregistervm "$VM_NAME" --delete >/dev/null 2>&1 || \
    VBoxManage unregistervm "$VM_NAME" >/dev/null 2>&1 || true
  sleep 1
fi

mkdir -p "$MEDIA"
cp -f "$FLP_SRC" "$MEDIA/chaos.flp"
cp -f "$VDI_SRC" "$MEDIA/chaos-disk.vdi"
FLP="$MEDIA/chaos.flp"
VDI="$MEDIA/chaos-disk.vdi"

echo "Creating VM '$VM_NAME'..."
VBoxManage createvm --name "$VM_NAME" --ostype "Other" --register
VBoxManage modifyvm "$VM_NAME" \
  --memory 128 \
  --vram 16 \
  --acpi on \
  --ioapic on \
  --boot1 floppy \
  --boot2 disk \
  --boot3 none \
  --boot4 none \
  --firmware bios \
  --nic1 none \
  --graphicscontroller vboxvga \
  --mouse ps2 \
  --keyboard ps2 \
  --uart1 0x3F8 4 \
  --uartmode1 file "$MEDIA/serial.log"

VBoxManage storagectl "$VM_NAME" --name "IDE" --add ide --controller PIIX4
VBoxManage storageattach "$VM_NAME" --storagectl "IDE" --port 0 --device 0 \
  --type hdd --medium "$VDI"

VBoxManage storagectl "$VM_NAME" --name "Floppy" --add floppy
VBoxManage storageattach "$VM_NAME" --storagectl "Floppy" --port 0 --device 0 \
  --type fdd --medium "$FLP"

echo ""
echo "Storage:"
VBoxManage showvminfo "$VM_NAME" | grep -E 'IDE|Floppy|Location:|Boot Device|Graphics'
echo ""
echo "VM ready:"
echo "  VBoxManage startvm \"$VM_NAME\" --type gui"
echo "Serial: $MEDIA/serial.log"
echo "Media:  $MEDIA"
