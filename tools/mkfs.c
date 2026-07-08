/* Host-side tool that injects files into a chaOS disk image.
 *
 * The on-disk layout mirrors kernel/fs.c exactly:
 *   - LBA 0..2 : superblock (magic + fixed 32-entry directory)
 *   - LBA 4+   : each directory slot i owns SECTORS_PER_FILE sectors starting
 *                at DATA_START_LBA + i * SECTORS_PER_FILE
 *
 * Usage: mkfs <disk.img> <file> [file ...]
 * Files are stored under their basename. The image is formatted first if it
 * does not already contain a valid filesystem.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define FS_NAME_LEN      24
#define FS_MAX_FILES     32
#define FS_MAGIC         0x4D594653u
#define SB_SECTORS       3
#define DATA_START_LBA   4
#define SECTORS_PER_FILE 64
#define SECTOR           512
#define FS_MAX_FILESIZE  (SECTORS_PER_FILE * SECTOR)

typedef struct {
    char     name[FS_NAME_LEN];
    uint32_t size;
    uint32_t start_lba;
    uint32_t used;
} fs_entry_t;

typedef struct {
    uint32_t   magic;
    uint32_t   num_files;
    fs_entry_t entries[FS_MAX_FILES];
} superblock_t;

static void format_sb(superblock_t *sb) {
    memset(sb, 0, sizeof(*sb));
    sb->magic = FS_MAGIC;
    sb->num_files = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        sb->entries[i].used = 0;
        sb->entries[i].size = 0;
        sb->entries[i].start_lba = DATA_START_LBA + i * SECTORS_PER_FILE;
        sb->entries[i].name[0] = '\0';
    }
}

static const char *basename_of(const char *path) {
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '/') b = p + 1;
    return b;
}

static int add_file(FILE *img, superblock_t *sb, const char *path) {
    const char *name = basename_of(path);
    if (strlen(name) >= FS_NAME_LEN) {
        fprintf(stderr, "mkfs: name too long: %s\n", name);
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "mkfs: cannot open %s\n", path); return -1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > FS_MAX_FILESIZE) {
        fprintf(stderr, "mkfs: %s too big (%ld > %d)\n", path, sz, FS_MAX_FILESIZE);
        fclose(f);
        return -1;
    }

    static uint8_t data[FS_MAX_FILESIZE];
    memset(data, 0, sizeof(data));
    if (sz > 0 && fread(data, 1, (size_t) sz, f) != (size_t) sz) {
        fprintf(stderr, "mkfs: read error on %s\n", path);
        fclose(f);
        return -1;
    }
    fclose(f);

    /* Find an existing entry with this name, or allocate a free slot. */
    int idx = -1;
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (sb->entries[i].used && strcmp(sb->entries[i].name, name) == 0) { idx = i; break; }
    if (idx < 0) {
        for (int i = 0; i < FS_MAX_FILES; i++)
            if (!sb->entries[i].used) { idx = i; break; }
        if (idx < 0) { fprintf(stderr, "mkfs: directory full\n"); return -1; }
        memset(sb->entries[idx].name, 0, FS_NAME_LEN);
        strncpy(sb->entries[idx].name, name, FS_NAME_LEN - 1);
        sb->entries[idx].used = 1;
        sb->entries[idx].start_lba = DATA_START_LBA + idx * SECTORS_PER_FILE;
        sb->num_files++;
    }
    sb->entries[idx].size = (uint32_t) sz;

    /* Write the data, padded up to a whole number of sectors. */
    long nsectors = (sz + SECTOR - 1) / SECTOR;
    if (nsectors == 0) nsectors = 1;
    if (fseek(img, (long) sb->entries[idx].start_lba * SECTOR, SEEK_SET) != 0) {
        fprintf(stderr, "mkfs: seek failed\n");
        return -1;
    }
    if (fwrite(data, 1, (size_t) (nsectors * SECTOR), img) != (size_t) (nsectors * SECTOR)) {
        fprintf(stderr, "mkfs: write failed\n");
        return -1;
    }

    printf("mkfs: added %-16s %6ld bytes -> slot %d (lba %u)\n",
           name, sz, idx, sb->entries[idx].start_lba);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <disk.img> <file> [file ...]\n", argv[0]);
        return 1;
    }

    FILE *img = fopen(argv[1], "r+b");
    if (!img) { fprintf(stderr, "mkfs: cannot open image %s\n", argv[1]); return 1; }

    /* Read (or initialize) the superblock. */
    uint8_t sbbuf[SB_SECTORS * SECTOR];
    memset(sbbuf, 0, sizeof(sbbuf));
    fseek(img, 0, SEEK_SET);
    /* A short read on a fresh (zeroed) image is fine; the magic check handles it. */
    size_t got = fread(sbbuf, 1, sizeof(sbbuf), img);
    (void) got;

    superblock_t sb;
    memcpy(&sb, sbbuf, sizeof(sb));
    if (sb.magic != FS_MAGIC) {
        printf("mkfs: no filesystem found, formatting %s\n", argv[1]);
        format_sb(&sb);
    }

    for (int i = 2; i < argc; i++) {
        if (add_file(img, &sb, argv[i]) != 0) { fclose(img); return 1; }
    }

    /* Write the superblock back (zero-padded to SB_SECTORS). */
    memset(sbbuf, 0, sizeof(sbbuf));
    memcpy(sbbuf, &sb, sizeof(sb));
    fseek(img, 0, SEEK_SET);
    if (fwrite(sbbuf, 1, sizeof(sbbuf), img) != sizeof(sbbuf)) {
        fprintf(stderr, "mkfs: superblock write failed\n");
        fclose(img);
        return 1;
    }

    fclose(img);
    return 0;
}
