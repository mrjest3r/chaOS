#include "ulib.h"

/* Built only by the host SDK (make sdk SRC=tools/sdk/examples/beacon.c).
 * It is not part of the boot image. */
int main(void) {
    uprint("sdk beacon\n");
    return 0;
}
