#include "ulib.h"
#include "accounts.h"

/* First screen of an interactive boot. Creates the first account when
 * the machine has none, then asks for a username and a password. */

int main(void) {
    uprint("chaOS\n");
    acct_load();
    if (acct_count() == 0) acct_create_first();
    char who[ACCT_NAME];
    acct_login(who);
    if (!session_save(who)) uprint("could not store the session\n");
    uprint("welcome, ");
    uprint(who);
    uprint("\n");
    return 0;
}
