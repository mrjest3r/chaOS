#include "ulib.h"

/* IPC demo, parent side. Creates two pipes, spawns pipekid.elf with the pipe
 * ids on its command line, sends it a message, and reads the echoed (upper-
 * cased) reply. The verdict is also written to the file "pipetest" so the
 * kernel AUTOTEST can verify real bytes crossed between two processes. */

static void append_dec(char *s, int v) {
    int len = ustrlen(s);
    char tmp[12];
    int i = 0;
    if (v == 0) tmp[i++] = '0';
    while (v > 0) { tmp[i++] = (char) ('0' + (v % 10)); v /= 10; }
    while (i > 0) s[len++] = tmp[--i];
    s[len] = '\0';
}

int main(void) {
    int to_child   = upipe();
    int from_child = upipe();
    if (to_child < 0 || from_child < 0) {
        uprint("[pipedemo] FAIL: no free pipes\n");
        uwritefile("pipetest", "BAD", 3);
        return 1;
    }

    char cmd[48] = "bin/pipekid.elf ";
    append_dec(cmd, to_child);
    cmd[ustrlen(cmd) + 1] = '\0';
    cmd[ustrlen(cmd)] = ' ';
    append_dec(cmd, from_child);

    int pid = uspawn(cmd);
    if (pid < 0) {
        uprint("[pipedemo] FAIL: cannot spawn pipekid.elf\n");
        uwritefile("pipetest", "BAD", 3);
        return 1;
    }

    const char *msg = "ping from parent";
    upipewrite(to_child, msg, ustrlen(msg));
    upipeclose(to_child);

    char reply[64];
    int total = 0, n;
    while (total < (int) sizeof(reply) - 1 &&
           (n = upiperead(from_child, reply + total,
                          (int) sizeof(reply) - 1 - total)) > 0) {
        total += n;
    }
    reply[total] = '\0';
    uwait(pid);

    uprint("[pipedemo] child replied: \"");
    uprint(reply);
    uprint("\"\n");

    const char *want = "PING FROM PARENT";
    int ok = (total == ustrlen(want));
    for (int i = 0; ok && want[i]; i++)
        if (reply[i] != want[i]) ok = 0;

    uprint(ok ? "[pipedemo] pipe round-trip OK\n"
              : "[pipedemo] pipe round-trip FAILED\n");
    uwritefile("pipetest", ok ? "OK" : "BAD", ok ? 2 : 3);
    return ok ? 0 : 1;
}
