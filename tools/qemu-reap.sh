#!/bin/sh
# Stop leftover chaOS QEMU processes without kill(2).
# Each headless run (tools/qemu-guard.sh) listens on a monitor socket under
# /tmp/chaos-qemu-mon. `quit` over that socket works even when the sandbox
# rejects signals, so a hung guest does not need a reboot.
# `kill -9` is only a fallback for a QEMU that has no monitor socket.
MON_DIR=/tmp/chaos-qemu-mon
mkdir -p "$MON_DIR"

quit_sock() {
    sock=$1
    [ -S "$sock" ] || return 0
    if command -v socat >/dev/null 2>&1; then
        printf 'quit\n' | socat -t 2 - "UNIX-CONNECT:$sock" >/dev/null 2>&1 || true
    else
        printf 'quit\n' | nc -U "$sock" >/dev/null 2>&1 || true
    fi
}

for sock in "$MON_DIR"/*; do
    [ -e "$sock" ] || continue
    quit_sock "$sock"
done

# Give QEMU a moment to exit and unlink its socket.
sleep 0.3
for sock in "$MON_DIR"/*; do
    [ -S "$sock" ] || continue
    quit_sock "$sock"
done

# Fallback: real QEMU processes only. A shell whose command line mentions
# qemu-system-i386 (make, qemu-guard) must not match — killing it aborts the test.
for pid in $(ps -eo pid=,comm= | awk '$2=="qemu-system-i386"{print $1}'); do
    cmd=$(ps -p "$pid" -o args= 2>/dev/null) || continue
    case "$cmd" in
        qemu-system-i386\ *os-image.bin*|qemu-system-i386\ *chaos.flp*|qemu-system-i386\ *-cdrom\ chaos.iso*|qemu-system-i386\ *-monitor\ unix:/tmp/chaos-qemu-mon/*) ;;
        *) continue ;;
    esac
    kill -9 "$pid" 2>/dev/null || true
    ppid=$(ps -p "$pid" -o ppid= 2>/dev/null | tr -d ' ')
    pcmd=$(ps -p "$ppid" -o comm= 2>/dev/null)
    if [ "$pcmd" = "timeout" ]; then
        kill -9 "$ppid" 2>/dev/null || true
    fi
done
exit 0
