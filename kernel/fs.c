#include "fs.h"
#include "../drivers/ata.h"
#include "../libc/string.h"
#include "../libc/mem.h"

/* A deliberately simple filesystem:
 *   - LBA 0..2 hold the superblock (magic + fixed directory).
 *   - Each directory slot 'i' owns a fixed data region starting at
 *     DATA_START_LBA + i * SECTORS_PER_FILE. This avoids any allocation or
 *     fragmentation logic at the cost of a fixed max file size. */

#define FS_MAGIC        0x4D594653u /* "MYFS" */
#define SB_SECTORS      3
#define DATA_START_LBA  4
#define SECTORS_PER_FILE 64         /* 32 KiB, matches FS_MAX_FILESIZE */

typedef struct {
    char name[FS_NAME_LEN];
    uint32_t size;      /* bytes; 0 and used=0 => empty slot */
    uint32_t start_lba;
    uint32_t used;
} fs_entry_t;

typedef struct {
    uint32_t magic;
    uint32_t num_files;
    fs_entry_t entries[FS_MAX_FILES];
} superblock_t;

static superblock_t sb;
static int mounted = 0;

static void fs_read_sb() {
    uint8_t buf[SB_SECTORS * 512];
    ata_read_sectors(0, SB_SECTORS, buf);
    memory_copy(buf, (uint8_t *) &sb, sizeof(superblock_t));
}

static void fs_write_sb() {
    uint8_t buf[SB_SECTORS * 512];
    memory_set(buf, 0, sizeof(buf));
    memory_copy((uint8_t *) &sb, buf, sizeof(superblock_t));
    ata_write_sectors(0, SB_SECTORS, buf);
}

void fs_format() {
    memory_set((uint8_t *) &sb, 0, sizeof(superblock_t));
    sb.magic = FS_MAGIC;
    sb.num_files = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        sb.entries[i].used = 0;
        sb.entries[i].size = 0;
        sb.entries[i].start_lba = DATA_START_LBA + i * SECTORS_PER_FILE;
        sb.entries[i].name[0] = '\0';
    }
    fs_write_sb();
    mounted = 1;
}

int fs_mount() {
    if (!init_ata()) return 0; /* no disk */

    fs_read_sb();
    if (sb.magic != FS_MAGIC) {
        fs_format();
    }
    mounted = 1;
    return 1;
}

static int fs_find(const char *name) {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (sb.entries[i].used && strcmp((char *) sb.entries[i].name, (char *) name) == 0) {
            return i;
        }
    }
    return -1;
}

static int fs_alloc_slot() {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (!sb.entries[i].used) return i;
    }
    return -1;
}

int fs_write_file(const char *name, const uint8_t *data, uint32_t size) {
    if (!mounted || size > FS_MAX_FILESIZE) return -1;

    int idx = fs_find(name);
    if (idx < 0) {
        idx = fs_alloc_slot();
        if (idx < 0) return -1; /* directory full */
        strncpy(sb.entries[idx].name, name, FS_NAME_LEN);
        sb.entries[idx].used = 1;
        sb.entries[idx].start_lba = DATA_START_LBA + idx * SECTORS_PER_FILE;
        sb.num_files++;
    }

    uint32_t sectors = (size + 511) / 512;
    for (uint32_t s = 0; s < sectors; s++) {
        uint8_t secbuf[512];
        memory_set(secbuf, 0, 512);
        uint32_t chunk = size - s * 512;
        if (chunk > 512) chunk = 512;
        memory_copy((uint8_t *) (data + s * 512), secbuf, (int) chunk);
        ata_write_sectors(sb.entries[idx].start_lba + s, 1, secbuf);
    }

    sb.entries[idx].size = size;
    fs_write_sb();
    return 0;
}

int fs_read_file(const char *name, uint8_t *buf, uint32_t maxlen) {
    if (!mounted) return -1;
    int idx = fs_find(name);
    if (idx < 0) return -1;

    uint32_t size = sb.entries[idx].size;
    if (size > maxlen) size = maxlen;

    uint32_t sectors = (size + 511) / 512;
    for (uint32_t s = 0; s < sectors; s++) {
        uint8_t secbuf[512];
        ata_read_sectors(sb.entries[idx].start_lba + s, 1, secbuf);
        uint32_t chunk = size - s * 512;
        if (chunk > 512) chunk = 512;
        memory_copy(secbuf, buf + s * 512, (int) chunk);
    }
    return (int) size;
}

int fs_delete(const char *name) {
    if (!mounted) return -1;
    int idx = fs_find(name);
    if (idx < 0) return -1;

    sb.entries[idx].used = 0;
    sb.entries[idx].size = 0;
    sb.entries[idx].name[0] = '\0';
    if (sb.num_files > 0) sb.num_files--;
    fs_write_sb();
    return 0;
}

int fs_list(fs_fileinfo_t *out, int max) {
    int n = 0;
    for (int i = 0; i < FS_MAX_FILES && n < max; i++) {
        if (sb.entries[i].used) {
            strncpy(out[n].name, sb.entries[i].name, FS_NAME_LEN);
            out[n].size = sb.entries[i].size;
            n++;
        }
    }
    return n;
}
