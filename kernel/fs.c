#include "fs.h"
#include "kheap.h"
#include "../drivers/ata.h"
#include "../libc/string.h"
#include "../libc/mem.h"

/* A deliberately simple filesystem:
 *   - LBA 0..2 hold the superblock (magic + fixed directory).
 *   - Each directory slot 'i' owns a fixed data region starting at
 *     DATA_START_LBA + i * SECTORS_PER_FILE. This avoids any allocation or
 *     fragmentation logic at the cost of a fixed max file size.
 *
 * Block device: the IDE primary master when present, otherwise a RAM disk
 * (heap chunks allocated on first write, unwritten sectors read as zero) so a
 * bare floppy / ISO boot still has a filesystem — seeded from the initrd. */

#define FS_MAGIC        0x4D594653u /* "MYFS" */
#define SB_SECTORS      3
#define DATA_START_LBA  4
#define SECTORS_PER_FILE 64         /* 32 KiB, matches FS_MAX_FILESIZE */

#define RAM_CHUNK_SECTORS 64
#define RAM_CHUNKS ((DATA_START_LBA + FS_MAX_FILES * SECTORS_PER_FILE + RAM_CHUNK_SECTORS - 1) / RAM_CHUNK_SECTORS)

static int ram_backed = 0;
static uint8_t *ram_chunk[RAM_CHUNKS];

static int dev_read(uint32_t lba, uint8_t count, uint8_t *buf) {
    if (!ram_backed) return ata_read_sectors(lba, count, buf);
    for (uint8_t i = 0; i < count; i++, lba++, buf += 512) {
        uint32_t c = lba / RAM_CHUNK_SECTORS;
        if (c >= RAM_CHUNKS) return -1;
        if (ram_chunk[c])
            memory_copy(ram_chunk[c] + (lba % RAM_CHUNK_SECTORS) * 512, buf, 512);
        else
            memory_set(buf, 0, 512);
    }
    return 0;
}

static int dev_write(uint32_t lba, uint8_t count, const uint8_t *buf) {
    if (!ram_backed) return ata_write_sectors(lba, count, buf);
    for (uint8_t i = 0; i < count; i++, lba++, buf += 512) {
        uint32_t c = lba / RAM_CHUNK_SECTORS;
        if (c >= RAM_CHUNKS) return -1;
        if (!ram_chunk[c]) {
            ram_chunk[c] = (uint8_t *) malloc(RAM_CHUNK_SECTORS * 512);
            if (!ram_chunk[c]) return -1;
            memory_set(ram_chunk[c], 0, RAM_CHUNK_SECTORS * 512);
        }
        memory_copy((uint8_t *) buf, ram_chunk[c] + (lba % RAM_CHUNK_SECTORS) * 512, 512);
    }
    return 0;
}

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

static int fs_read_sb() {
    uint8_t buf[SB_SECTORS * 512];
    memory_set(buf, 0, sizeof(buf));
    if (dev_read(0, SB_SECTORS, buf) != 0) return -1;
    memory_copy(buf, (uint8_t *) &sb, sizeof(superblock_t));
    return 0;
}

static int fs_write_sb() {
    uint8_t buf[SB_SECTORS * 512];
    memory_set(buf, 0, sizeof(buf));
    memory_copy((uint8_t *) &sb, buf, sizeof(superblock_t));
    return dev_write(0, SB_SECTORS, buf);
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
    if (fs_write_sb() != 0) {
        mounted = 0;
        return;
    }
    mounted = 1;
}

int fs_mount() {
    if (!init_ata()) {
        /* No disk: run on a RAM disk. The initrd fills it right after. */
        ram_backed = 1;
        for (int i = 0; i < RAM_CHUNKS; i++) ram_chunk[i] = 0;
        fs_format();
        return mounted;
    }

    if (fs_read_sb() != 0) {
        /* First contact with a blank/VirtualBox disk can fail the first
         * multi-sector-sized read; try once more after a format. */
        fs_format();
        if (!mounted) return 0;
        return 1;
    }
    if (sb.magic != FS_MAGIC) {
        fs_format();
        if (!mounted) return 0;
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
        if (dev_write(sb.entries[idx].start_lba + s, 1, secbuf) != 0)
            return -1;
    }

    sb.entries[idx].size = size;
    if (fs_write_sb() != 0) return -1;
    return 0;
}

int fs_read_file(const char *name, uint8_t *buf, uint32_t maxlen) {
    if (!mounted) return -1;
    int idx = fs_find(name);
    if (idx < 0) return -1;

    uint32_t size = sb.entries[idx].size;
    /* A corrupt superblock (e.g. after a failed VirtualBox multi-sector write)
     * can claim an absurd size; refuse anything past the real limit. */
    if (size > FS_MAX_FILESIZE) return -1;
    if (size > maxlen) size = maxlen;

    uint32_t sectors = (size + 511) / 512;
    for (uint32_t s = 0; s < sectors; s++) {
        uint8_t secbuf[512];
        if (dev_read(sb.entries[idx].start_lba + s, 1, secbuf) != 0)
            return -1;
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
    if (fs_write_sb() != 0) return -1;
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

int fs_exists(const char *name) {
    return mounted && fs_find(name) >= 0;
}

int fs_is_ram(void) {
    return mounted && ram_backed;
}

int fs_size(const char *name) {
    if (!mounted) return -1;
    int idx = fs_find(name);
    return idx < 0 ? -1 : (int) sb.entries[idx].size;
}
