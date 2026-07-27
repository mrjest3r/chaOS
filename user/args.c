#include "ulib.h"

/* Demonstrates command-line arguments. The kernel copies the command line onto
 * this process's stack and crt0's 'call main' delivers a normal cdecl frame,
 * so plain main(argc, argv) just works:
 *
 *   ush> run args.elf hello world
 */
int main(int argc, char **argv) {
    uprint("[args] argc = ");
    uprint_dec((uint32_t) argc);
    uprint("\n");

    for (int i = 0; i < argc; i++) {
        uprint("[args] argv[");
        uprint_dec((uint32_t) i);
        uprint("] = \"");
        uprint(argv[i]);
        uprint("\"\n");
    }

    if (argc > 1) {
        uprint("[args] argv OK\n");
    } else {
        uprint("[args] no arguments given - try: run args.elf hello world\n");
    }
    return 0;
}
