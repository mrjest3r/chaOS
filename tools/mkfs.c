/* Host-side tool that injects files into a chaOS disk image.
 *
 * The on-disk layout mirrors kernel/fs.c exactly:
 *   - LBA 0..2 : superblock. Version 2 keeps 32 slots here. Version 3
 *     points dir_lba at a 10000-slot table in free space.
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
#define FS_LEGACY_SLOTS  32
#define FS_MAX_FILES     10000
#define FS_MAGIC         0x4D594653u
#define FS_VERSION       3
#define FS_VERSION_V2    2
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
    fs_entry_t legacy[FS_LEGACY_SLOTS];
    uint32_t   version;
    uint32_t   dir_lba;
    uint32_t   dir_slots;
} superblock_t;

static uint32_t volume_end = DISK_CAP_SECTORS;
static fs_entry_t *ents;

static uint32_t dir_sectors(void) {
    return (FS_MAX_FILES * sizeof(fs_entry_t) + SECTOR - 1) / SECTOR;
}

static fs_entry_t *tab_of(const superblock_t *sb) {
    if (sb->version >= FS_VERSION && ents) return ents;
    return (fs_entry_t *) sb->legacy;
}

static int nslots_of(const superblock_t *sb) {
    return (sb->version >= FS_VERSION && ents) ? FS_MAX_FILES : FS_LEGACY_SLOTS;
}

static uint32_t sectors_of(uint32_t size) {
    if (size == 0) return 0;
    return (size + SECTOR - 1) / SECTOR;
}

static int range_busy(superblock_t *sb, uint32_t start, uint32_t nsec, int ignore) {
    uint32_t end = start + nsec;
    fs_entry_t *tab = tab_of(sb);
    int nslots = nslots_of(sb);
    for (int i = 0; i < nslots; i++) {
        if (i == ignore || !tab[i].used) continue;
        uint32_t n = sectors_of(tab[i].size);
        if (n == 0) continue;
        uint32_t a = tab[i].start_lba;
        if (start < a + n && end > a) return 1;
    }
    if (sb->version >= FS_VERSION && sb->dir_lba) {
        uint32_t b = sb->dir_lba + dir_sectors();
        if (start < b && end > sb->dir_lba) return 1;
    }
    return 0;
}

static uint32_t alloc_run(superblock_t *sb, uint32_t nsec, int ignore) {
    uint32_t c = DATA_START_LBA;
    while (c + nsec <= volume_end) {
        if (!range_busy(sb, c, nsec, ignore)) return c;
        uint32_t next = c + 1;
        fs_entry_t *tab = tab_of(sb);
        int nslots = nslots_of(sb);
        for (int i = 0; i < nslots; i++) {
            if (i == ignore || !tab[i].used) continue;
            uint32_t n = sectors_of(tab[i].size);
            if (n == 0) continue;
            uint32_t b = tab[i].start_lba + n;
            if (c < b && c + nsec > tab[i].start_lba && b > next) next = b;
        }
        if (sb->version >= FS_VERSION && sb->dir_lba) {
            uint32_t b = sb->dir_lba + dir_sectors();
            if (c < b && c + nsec > sb->dir_lba && b > next) next = b;
        }
        if (next <= c) return 0;
        c = next;
    }
    return 0;
}

static int ensure_ents(void) {
    if (ents) return 0;
    ents = calloc(FS_MAX_FILES, sizeof(fs_entry_t));
    if (!ents) { fprintf(stderr, "mkfs: out of memory\n"); return -1; }
    return 0;
}

static int write_dir(FILE *img, const superblock_t *sb) {
    uint32_t bytes = FS_MAX_FILES * sizeof(fs_entry_t);
    uint32_t nsec = dir_sectors();
    uint8_t *pad = calloc(nsec, SECTOR);
    if (!pad) return -1;
    memcpy(pad, ents, bytes);
    if (fseek(img, (long) sb->dir_lba * SECTOR, SEEK_SET) != 0 ||
        fwrite(pad, 1, (size_t) nsec * SECTOR, img) != (size_t) nsec * SECTOR) {
        free(pad);
        fprintf(stderr, "mkfs: directory write failed\n");
        return -1;
    }
    free(pad);
    return 0;
}

static int load_dir(FILE *img, const superblock_t *sb) {
    if (ensure_ents() != 0) return -1;
    uint32_t bytes = FS_MAX_FILES * sizeof(fs_entry_t);
    uint32_t nsec = dir_sectors();
    uint8_t *pad = calloc(nsec, SECTOR);
    if (!pad) return -1;
    if (fseek(img, (long) sb->dir_lba * SECTOR, SEEK_SET) != 0 ||
        fread(pad, 1, (size_t) nsec * SECTOR, img) != (size_t) nsec * SECTOR) {
        free(pad);
        fprintf(stderr, "mkfs: directory read failed\n");
        return -1;
    }
    memcpy(ents, pad, bytes);
    free(pad);
    return 0;
}

static int place_dir(FILE *img, superblock_t *sb) {
    if (ensure_ents() != 0) return -1;
    uint32_t lba = alloc_run(sb, dir_sectors(), -1);
    if (!lba) { fprintf(stderr, "mkfs: no room for the directory\n"); return -1; }
    sb->dir_lba = lba;
    sb->dir_slots = FS_MAX_FILES;
    sb->version = FS_VERSION;
    return write_dir(img, sb);
}

static int format_sb(FILE *img, superblock_t *sb) {
    memset(sb, 0, sizeof(*sb));
    sb->magic = FS_MAGIC;
    sb->num_files = 0;
    if (ents) memset(ents, 0, FS_MAX_FILES * sizeof(fs_entry_t));
    return place_dir(img, sb);
}

static const char *basename_of(const char *path) {
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '/') b = p + 1;
    return b;
}

/* A zero-byte name ending in '/' is a directory marker. */
static void ensure_marker(superblock_t *sb, const char *mark) {
    fs_entry_t *tab = tab_of(sb);
    int nslots = nslots_of(sb);
    for (int i = 0; i < nslots; i++)
        if (tab[i].used && strcmp(tab[i].name, mark) == 0) return;
    for (int i = 0; i < nslots; i++) {
        if (tab[i].used) continue;
        memset(tab[i].name, 0, FS_NAME_LEN);
        memcpy(tab[i].name, mark, strlen(mark));
        tab[i].used = 1;
        tab[i].size = 0;
        tab[i].start_lba = 0;
        sb->num_files++;
        return;
    }
}

static int add_file(FILE *img, superblock_t *sb, const char *path) {
    const char *base = basename_of(path);
    if (strlen(base) >= FS_NAME_LEN) {
        fprintf(stderr, "mkfs: name too long: %s\n", base);
        return -1;
    }
    /* Programs live in bin/. An older flat name is renamed in place so the
     * file bytes are not copied. */
    char namebuf[FS_NAME_LEN];
    memset(namebuf, 0, sizeof(namebuf));
    size_t bl = strlen(base);
    int is_elf = bl >= 4 && strcmp(base + bl - 4, ".elf") == 0;
    if (is_elf) {
        if (bl + 4 >= FS_NAME_LEN) {
            fprintf(stderr, "mkfs: name too long: bin/%s\n", base);
            return -1;
        }
        memcpy(namebuf, "bin/", 4);
        memcpy(namebuf + 4, base, bl);
        ensure_marker(sb, "bin/");
    } else {
        memcpy(namebuf, base, bl);
    }
    const char *name = namebuf;

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

    /* Find an existing entry with this name, or allocate a free slot.
     * A flat *.elf left from before bin/ is the same file: rename it. */
    fs_entry_t *tab = tab_of(sb);
    int nslots = nslots_of(sb);
    int idx = -1;
    for (int i = 0; i < nslots; i++)
        if (tab[i].used && strcmp(tab[i].name, name) == 0) { idx = i; break; }
    if (idx < 0 && is_elf) {
        for (int i = 0; i < nslots; i++)
            if (tab[i].used && strcmp(tab[i].name, base) == 0) {
                memset(tab[i].name, 0, FS_NAME_LEN);
                memcpy(tab[i].name, name, strlen(name));
                idx = i;
                break;
            }
    }
    if (idx < 0) {
        for (int i = 0; i < nslots; i++)
            if (!tab[i].used) { idx = i; break; }
        if (idx < 0) { fprintf(stderr, "mkfs: directory full\n"); return -1; }
        memset(tab[idx].name, 0, FS_NAME_LEN);
        memcpy(tab[idx].name, name, strlen(name));
        tab[idx].used = 1;
        tab[idx].size = 0;
        tab[idx].start_lba = 0;
        sb->num_files++;
    }
    uint32_t need = sectors_of((uint32_t) sz);
    if (need > sectors_of(tab[idx].size)) {
        uint32_t lba = alloc_run(sb, need, idx);
        if (!lba) { fprintf(stderr, "mkfs: disk full\n"); return -1; }
        tab[idx].start_lba = lba;
    }
    tab[idx].size = (uint32_t) sz;
    if (need == 0) return 0;

    if (fseek(img, (long) tab[idx].start_lba * SECTOR, SEEK_SET) != 0) {
        fprintf(stderr, "mkfs: seek failed\n");
        return -1;
    }
    if (fwrite(data, 1, (size_t) need * SECTOR, img) != (size_t) need * SECTOR) {
        fprintf(stderr, "mkfs: write failed\n");
        return -1;
    }

    printf("mkfs: added %-16s %6ld bytes -> slot %d (lba %u)\n",
           name, sz, idx, tab[idx].start_lba);
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
        if (format_sb(img, &sb) != 0) { fclose(img); return 1; }
    } else if (sb.version == 0) {
        /* Pack the old fixed slots into the contiguous layout before adding. */
        typedef struct { char name[FS_NAME_LEN]; uint32_t size; uint8_t *data; } saved_t;
        saved_t saved[FS_LEGACY_SLOTS];
        int ns = 0;
        memset(saved, 0, sizeof(saved));
        for (int i = 0; i < FS_LEGACY_SLOTS; i++) {
            if (!sb.legacy[i].used) continue;
            uint32_t size = sb.legacy[i].size;
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
            memcpy(saved[ns].name, sb.legacy[i].name, FS_NAME_LEN);
            saved[ns].name[FS_NAME_LEN - 1] = '\0';
            saved[ns].size = size;
            saved[ns].data = data;
            ns++;
        }
        if (format_sb(img, &sb) != 0) { fclose(img); return 1; }
        printf("mkfs: migrated %d files to the 1 GiB layout\n", ns);
        for (int i = 0; i < ns; i++) {
            int idx = -1;
            for (int s = 0; s < FS_MAX_FILES; s++)
                if (!ents[s].used) { idx = s; break; }
            if (idx < 0) { fprintf(stderr, "mkfs: directory full\n"); return 1; }
            memset(ents[idx].name, 0, FS_NAME_LEN);
            for (int k = 0; k < FS_NAME_LEN - 1 && saved[i].name[k]; k++)
                ents[idx].name[k] = saved[i].name[k];
            ents[idx].used = 1;
            ents[idx].size = 0;
            ents[idx].start_lba = 0;
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
                ents[idx].start_lba = lba;
                ents[idx].size = saved[i].size;
            }
            free(saved[i].data);
        }
    } else if (sb.version == FS_VERSION_V2) {
        if (ensure_ents() != 0) { fclose(img); return 1; }
        memcpy(ents, sb.legacy, FS_LEGACY_SLOTS * sizeof(fs_entry_t));
        printf("mkfs: widening directory to %d names\n", FS_MAX_FILES);
        if (place_dir(img, &sb) != 0) { fclose(img); return 1; }
    } else if (sb.version == FS_VERSION) {
        if (load_dir(img, &sb) != 0) { fclose(img); return 1; }
    } else {
        fprintf(stderr, "mkfs: unknown filesystem version %u\n", sb.version);
        fclose(img);
        return 1;
    }

    for (int i = 2; i < argc; i++) {
        if (add_file(img, &sb, argv[i]) != 0) { fclose(img); return 1; }
    }

    /* Directory write seeks away from sector 0, so the superblock
     * is written after it. */
    memset(sbbuf, 0, sizeof(sbbuf));
    memcpy(sbbuf, &sb, sizeof(sb));
    if (sb.version >= FS_VERSION && write_dir(img, &sb) != 0) {
        fclose(img);
        return 1;
    }
    if (fseek(img, 0, SEEK_SET) != 0 ||
        fwrite(sbbuf, 1, sizeof(sbbuf), img) != sizeof(sbbuf)) {
        fprintf(stderr, "mkfs: superblock write failed\n");
        fclose(img);
        return 1;
    }

    fclose(img);
    return 0;
}
