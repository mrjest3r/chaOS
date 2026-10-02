#!/bin/sh
# tools/qemu-guard.sh SECONDS -- qemu-args...
#
# Headless QEMU with a monitor socket. On the deadline (or if this script is
# asked to stop), send HMP `quit` — no SIGKILL required. `timeout(1)` is not
# used: it moves QEMU into a new process group, so cancelling make orphans it.
set -u
secs=$1
shift
MON_DIR=/tmp/chaos-qemu-mon
mkdir -p "$MON_DIR"
sock=$(mktemp -u "$MON_DIR/mon.XXXXXX")

sh "$(dirname "$0")/qemu-reap.sh"

"$@" -monitor "unix:${sock},server,nowait" &
qpid=$!

quit_this() {
    if [ -S "$sock" ] && command -v socat >/dev/null 2>&1; then
        printf 'quit\n' | socat -t 2 - "UNIX-CONNECT:$sock" >/dev/null 2>&1 || true
    elif [ -S "$sock" ]; then
        printf 'quit\n' | nc -U "$sock" >/dev/null 2>&1 || true
    fi
    kill -9 "$qpid" 2>/dev/null || true
}

(
    i=0
    while [ "$i" -lt "$secs" ]; do
        [ -d "/proc/$qpid" ] || exit 0
        sleep 1
        i=$((i + 1))
    done
    if [ -S "$sock" ] && command -v socat >/dev/null 2>&1; then
        printf 'quit\n' | socat -t 2 - "UNIX-CONNECT:$sock" >/dev/null 2>&1 || true
    fi
    kill -9 "$qpid" 2>/dev/null || true
) &
wpid=$!

trap 'quit_this; kill -9 "$wpid" 2>/dev/null || true' INT TERM
wait "$qpid"
rc=$?
kill -9 "$wpid" 2>/dev/null || true
wait "$wpid" 2>/dev/null || true
rm -f "$sock"
exit "$rc"
