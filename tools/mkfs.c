/* Host-side tool that injects files into a chaOS disk image.
 *
 * The on-disk layout mirrors kernel/fs.c exactly:
 *   - LBA 0..2 : superblock (magic, version, 32-entry directory)
 *   - LBA 4+   : contiguous file runs, up to 1 GiB on the image
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
#define FS_VERSION       2
#define SB_SECTORS       3
#define DATA_START_LBA   4
#define LEGACY_SECTORS   64
#define SECTOR           512
#define FS_MAX_FILESIZE  (32 * 1024 * 1024)
#define DISK_CAP_SECTORS (1024 * 1024 * 1024 / SECTOR)

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
    uint32_t   version;
} superblock_t;

static uint32_t volume_end = DISK_CAP_SECTORS;

static uint32_t sectors_of(uint32_t size) {
    if (size == 0) return 0;
    return (size + SECTOR - 1) / SECTOR;
}

static int range_busy(superblock_t *sb, uint32_t start, uint32_t nsec, int ignore) {
    uint32_t end = start + nsec;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (i == ignore || !sb->entries[i].used) continue;
        uint32_t n = sectors_of(sb->entries[i].size);
        if (n == 0) continue;
        uint32_t a = sb->entries[i].start_lba;
        if (start < a + n && end > a) return 1;
    }
    return 0;
}

static uint32_t alloc_run(superblock_t *sb, uint32_t nsec, int ignore) {
    uint32_t c = DATA_START_LBA;
    while (c + nsec <= volume_end) {
        if (!range_busy(sb, c, nsec, ignore)) return c;
        uint32_t next = c + 1;
        for (int i = 0; i < FS_MAX_FILES; i++) {
            if (i == ignore || !sb->entries[i].used) continue;
            uint32_t n = sectors_of(sb->entries[i].size);
            if (n == 0) continue;
            uint32_t b = sb->entries[i].start_lba + n;
            if (c < b && c + nsec > sb->entries[i].start_lba && b > next) next = b;
        }
        if (next <= c) return 0;
        c = next;
    }
    return 0;
}

static void format_sb(superblock_t *sb) {
    memset(sb, 0, sizeof(*sb));
    sb->magic = FS_MAGIC;
    sb->version = FS_VERSION;
    sb->num_files = 0;
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
        sb->entries[idx].size = 0;
        sb->entries[idx].start_lba = 0;
        sb->num_files++;
    }
    uint32_t need = sectors_of((uint32_t) sz);
    if (need > sectors_of(sb->entries[idx].size)) {
        uint32_t lba = alloc_run(sb, need, idx);
        if (!lba) { fprintf(stderr, "mkfs: disk full\n"); return -1; }
        sb->entries[idx].start_lba = lba;
    }
    sb->entries[idx].size = (uint32_t) sz;
    if (need == 0) return 0;

    if (fseek(img, (long) sb->entries[idx].start_lba * SECTOR, SEEK_SET) != 0) {
        fprintf(stderr, "mkfs: seek failed\n");
        return -1;
    }
    if (fwrite(data, 1, (size_t) need * SECTOR, img) != (size_t) need * SECTOR) {
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

    fseek(img, 0, SEEK_END);
    long bytes = ftell(img);
    volume_end = bytes > 0 ? (uint32_t) (bytes / SECTOR) : 0;
    if (volume_end > DISK_CAP_SECTORS) volume_end = DISK_CAP_SECTORS;

    superblock_t sb;
    memcpy(&sb, sbbuf, sizeof(sb));
    if (sb.magic != FS_MAGIC) {
        printf("mkfs: no filesystem found, formatting %s\n", argv[1]);
        format_sb(&sb);
    } else if (sb.version != FS_VERSION) {
        /* Pack the old fixed slots into the contiguous layout before adding. */
        typedef struct { char name[FS_NAME_LEN]; uint32_t size; uint8_t *data; } saved_t;
        saved_t saved[FS_MAX_FILES];
        int ns = 0;
        memset(saved, 0, sizeof(saved));
        for (int i = 0; i < FS_MAX_FILES; i++) {
            if (!sb.entries[i].used) continue;
            uint32_t size = sb.entries[i].size;
            if (size > LEGACY_SECTORS * SECTOR) size = LEGACY_SECTORS * SECTOR;
            uint8_t *data = NULL;
            if (size) {
                data = malloc(size);
                if (!data) { fprintf(stderr, "mkfs: out of memory\n"); return 1; }
                if (fseek(img, (long) (DATA_START_LBA + i * LEGACY_SECTORS) * SECTOR, SEEK_SET) != 0 ||
                    fread(data, 1, size, img) != size) {
                    free(data);
                    fprintf(stderr, "mkfs: could not read legacy file\n");
                    return 1;
                }
            }
            memcpy(saved[ns].name, sb.entries[i].name, FS_NAME_LEN);
            saved[ns].name[FS_NAME_LEN - 1] = '\0';
            saved[ns].size = size;
            saved[ns].data = data;
            ns++;
        }
        format_sb(&sb);
        printf("mkfs: migrated %d files to the 1 GiB layout\n", ns);
        for (int i = 0; i < ns; i++) {
            int idx = -1;
            for (int s = 0; s < FS_MAX_FILES; s++)
                if (!sb.entries[s].used) { idx = s; break; }
            if (idx < 0) { fprintf(stderr, "mkfs: directory full\n"); return 1; }
            memset(sb.entries[idx].name, 0, FS_NAME_LEN);
            for (int k = 0; k < FS_NAME_LEN - 1 && saved[i].name[k]; k++)
                sb.entries[idx].name[k] = saved[i].name[k];
            sb.entries[idx].used = 1;
            sb.entries[idx].size = 0;
            sb.entries[idx].start_lba = 0;
            sb.num_files++;
            uint32_t need = sectors_of(saved[i].size);
            if (need) {
                uint32_t lba = alloc_run(&sb, need, idx);
                if (!lba) { fprintf(stderr, "mkfs: disk full\n"); return 1; }
                uint8_t *padded = calloc(need, SECTOR);
                if (!padded) return 1;
                memcpy(padded, saved[i].data, saved[i].size);
                if (fseek(img, (long) lba * SECTOR, SEEK_SET) != 0 ||
                    fwrite(padded, 1, (size_t) need * SECTOR, img) != (size_t) need * SECTOR) {
                    free(padded);
                    fprintf(stderr, "mkfs: migrate write failed\n");
                    return 1;
                }
                free(padded);
                sb.entries[idx].start_lba = lba;
                sb.entries[idx].size = saved[i].size;
            }
            free(saved[i].data);
        }
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
