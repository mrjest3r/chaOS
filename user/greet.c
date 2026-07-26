#include "ulib.h"

/* The first interactive chaOS program: it reads from the keyboard through the
 * SYS_READLINE syscall. While it runs, the shell hands it the keyboard focus;
 * the kernel does the echoing and Backspace editing. */
int main(void) {
    char name[64];

    uprint("What is your name? ");
    ureadline(name, (int) sizeof(name));

    if (name[0] == '\0') {
        uprint("Too shy? Hello anyway!\n");
        return 0;
    }

    uprint("Hello, ");
    uprint(name);
    uprint("! Welcome to chaOS.\n");
    return 0;
}
