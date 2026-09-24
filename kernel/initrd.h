#ifndef INITRD_H
#define INITRD_H

#include <stdint.h>

/* Initrd: the user programs packed into os-image.bin by tools/mkinitrd.c and
 * loaded by the boot sector at INITRD_PHYS (cpu/memlayout.h). It makes a bare
 * floppy / ISO / USB boot self-contained: the kernel seeds the filesystem
 * from it (RAM disk when no IDE disk exists, or a blank/stale real disk). */

typedef struct {
    char     magic[8];      /* "CHAOSIRD"                                   */
    uint32_t count;
    uint32_t total;         /* blob size in bytes                           */
} initrd_hdr_t;

typedef struct {
    char     name[24];      /* == FS_NAME_LEN                               */
    uint32_t size;
    uint32_t offset;        /* from blob start                              */
} initrd_ent_t;

/* 1 if a valid initrd is present in memory. */
int initrd_present(void);
/* Number of files in the initrd (0 if none). */
int initrd_count(void);

/* Copy every initrd file that is missing from the mounted filesystem, or
 * whose size differs (a rebuilt program), into it. Returns the number of
 * files written, or -1 if there is no initrd / no filesystem. */
int initrd_install(void);

#endif
