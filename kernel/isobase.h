#ifndef ISOBASE_H
#define ISOBASE_H

/* If a CD is attached, copy the base-OS catalog onto the mounted
 * filesystem. Files already the right size are left alone. Names that
 * are not in the catalog (accounts, notes, anything the user added)
 * are not touched. No CD is not an error. */
void base_install(void);

#endif
