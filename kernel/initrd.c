#include "initrd.h"
#include "fs.h"
#include "../cpu/arch.h"
#include "../drivers/serial.h"

static const initrd_hdr_t *hdr(void) {
    /* Physical INITRD_PHYS is inside the permanent identity map. */
    const initrd_hdr_t *h = (const initrd_hdr_t *) INITRD_PHYS;
    static const char magic[8] = { 'C', 'H', 'A', 'O', 'S', 'I', 'R', 'D' };
    for (int i = 0; i < 8; i++)
        if (h->magic[i] != magic[i]) return 0;
    if (h->count > FS_MAX_FILES || h->total > INITRD_MAX) return 0;
    return h;
}

int initrd_present(void) {
    return hdr() != 0;
}

int initrd_count(void) {
    const initrd_hdr_t *h = hdr();
    return h ? (int) h->count : 0;
}

int initrd_install(void) {
    const initrd_hdr_t *h = hdr();
    if (!h) return -1;
    const initrd_ent_t *ents = (const initrd_ent_t *) (h + 1);
    int written = 0;
    for (uint32_t i = 0; i < h->count; i++) {
        const initrd_ent_t *e = &ents[i];
        if (e->size > FS_MAX_FILESIZE || e->offset > h->total ||
            e->size > h->total - e->offset)
            continue;                                 /* corrupt entry */
        char raw[24];
        for (int k = 0; k < 24; k++) raw[k] = e->name[k];
        raw[23] = '\0';
        /* Programs are stored under bin/. The initrd itself keeps the
         * basename; the prefix is applied here so a rebuilt ELF replaces
         * bin/name rather than a second copy at the root. */
        char name[24];
        int slash = 0;
        for (int k = 0; raw[k]; k++)
            if (raw[k] == '/') slash = 1;
        if (!slash) {
            int n = 0;
            while (raw[n]) n++;
            if (n + 4 >= 24) continue;
            name[0] = 'b'; name[1] = 'i'; name[2] = 'n'; name[3] = '/';
            for (int k = 0; k < n; k++) name[4 + k] = raw[k];
            name[4 + n] = '\0';
        } else {
            for (int k = 0; k < 24; k++) name[k] = raw[k];
        }
        if (!fs_exists("bin/")) fs_write_file("bin/", 0, 0);
        if (fs_exists(name) && fs_size(name) == (int) e->size) continue;
        const uint8_t *data = (const uint8_t *) INITRD_PHYS + e->offset;
        if (fs_write_file(name, data, e->size) == 0) written++;
        else {
            serial_write("[initrd] could not install ");
            serial_write(name);
            serial_write("\n");
        }
    }
    return written;
}
