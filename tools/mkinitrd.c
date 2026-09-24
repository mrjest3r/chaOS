/* Host-side tool: pack files into a chaOS initrd blob.
 *
 * The blob rides in os-image.bin right after the kernel and is loaded by the
 * boot sector to INITRD_PHYS (cpu/memlayout.h). kernel/initrd.c parses it and
 * seeds the filesystem, so a bare floppy / ISO / USB stick carries every user
 * program without a separate disk.img.
 *
 * Layout (all little-endian, no padding between sections):
 *   initrd_hdr_t            magic "CHAOSIRD", count, total bytes
 *   initrd_ent_t[count]     name (NUL-padded), size, offset from blob start
 *   file data               concatenated, each 4-byte aligned
 *
 * Usage: mkinitrd <out.bin> <file> [file ...]      (names = basenames) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define NAME_LEN 24
#define MAX_FILES 32
#define MAX_FILESIZE (64 * 512)

typedef struct {
    char     magic[8];
    uint32_t count;
    uint32_t total;
} initrd_hdr_t;

typedef struct {
    char     name[NAME_LEN];
    uint32_t size;
    uint32_t offset;
} initrd_ent_t;

static const char *basename_of(const char *p) {
    const char *b = p;
    for (; *p; p++) if (*p == '/') b = p + 1;
    return b;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <out.bin> <file> [file ...]\n", argv[0]);
        return 1;
    }
    int n = argc - 2;
    if (n > MAX_FILES) { fprintf(stderr, "mkinitrd: too many files\n"); return 1; }

    initrd_ent_t ents[MAX_FILES];
    uint8_t *datas[MAX_FILES];
    uint32_t off = (uint32_t) (sizeof(initrd_hdr_t) + (size_t) n * sizeof(initrd_ent_t));
    off = (off + 3u) & ~3u;

    for (int i = 0; i < n; i++) {
        const char *path = argv[i + 2];
        const char *name = basename_of(path);
        if (strlen(name) >= NAME_LEN) { fprintf(stderr, "mkinitrd: name too long: %s\n", name); return 1; }
        FILE *f = fopen(path, "rb");
        if (!f) { fprintf(stderr, "mkinitrd: cannot open %s\n", path); return 1; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0 || sz > MAX_FILESIZE) { fprintf(stderr, "mkinitrd: %s too big\n", path); fclose(f); return 1; }
        datas[i] = malloc((size_t) sz + 4);
        if (sz > 0 && fread(datas[i], 1, (size_t) sz, f) != (size_t) sz) { fprintf(stderr, "mkinitrd: read error %s\n", path); fclose(f); return 1; }
        fclose(f);
        memset(&ents[i], 0, sizeof(ents[i]));
        strncpy(ents[i].name, name, NAME_LEN - 1);
        ents[i].size = (uint32_t) sz;
        ents[i].offset = off;
        off += ((uint32_t) sz + 3u) & ~3u;
    }

    FILE *out = fopen(argv[1], "wb");
    if (!out) { fprintf(stderr, "mkinitrd: cannot write %s\n", argv[1]); return 1; }
    initrd_hdr_t hdr;
    memcpy(hdr.magic, "CHAOSIRD", 8);
    hdr.count = (uint32_t) n;
    hdr.total = off;
    fwrite(&hdr, 1, sizeof(hdr), out);
    fwrite(ents, 1, (size_t) n * sizeof(initrd_ent_t), out);
    long pos = (long) (sizeof(hdr) + (size_t) n * sizeof(initrd_ent_t));
    static const uint8_t zero[4] = { 0, 0, 0, 0 };
    while (pos & 3) { fputc(0, out); pos++; }
    for (int i = 0; i < n; i++) {
        fwrite(datas[i], 1, ents[i].size, out);
        uint32_t pad = ((ents[i].size + 3u) & ~3u) - ents[i].size;
        fwrite(zero, 1, pad, out);
        free(datas[i]);
    }
    fclose(out);
    printf("mkinitrd: %d files, %u bytes -> %s\n", n, off, argv[1]);
    return 0;
}
