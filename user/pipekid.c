#include "ulib.h"

/* IPC demo, child side. Receives two pipe ids on its command line, reads its
 * input pipe until EOF, uppercases the bytes and writes them back on the
 * output pipe. Proves a spawned process can talk to its parent. */

int main(int argc, char **argv) {
    if (argc < 3) {
        uprint("[pipekid] usage: pipekid.elf IN_PIPE OUT_PIPE\n");
        return 1;
    }
    int in  = uatoi(argv[1]);
    int out = uatoi(argv[2]);

    char buf[64];
    int total = 0, n;
    while (total < (int) sizeof(buf) &&
           (n = upiperead(in, buf + total, (int) sizeof(buf) - total)) > 0) {
        total += n;
    }

    for (int i = 0; i < total; i++)
        if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] = (char) (buf[i] - 32);

    upipewrite(out, buf, total);
    upipeclose(out);
    return 0;
}
