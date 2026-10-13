#include "fs.h"
#include "kheap.h"
#include "../drivers/ata.h"
#include "../drivers/serial.h"
#include "../libc/string.h"
#include "../libc/mem.h"

/* A deliberately simple filesystem:
 *   - LBA 0..2 hold the superblock (magic + fixed directory).
 *   - Each directory slot 'i' owns a fixed data region starting at
 *     a contiguous run. Version 0 images (fixed 32 KiB slots) are packed
 *     into that layout on mount. A RAM boot keeps a small volume.
 *
 * Block device: the IDE primary master when present, otherwise a RAM disk
 * (heap chunks allocated on first write, unwritten sectors read as zero) so a
 * bare floppy / ISO boot still has a filesystem — seeded from the initrd. */

#define FS_MAGIC        0x4D594653u /* "MYFS" */
#define FS_VERSION      3
#define FS_VERSION_V2   2
#define FS_LEGACY_SLOTS 32
#define SB_SECTORS      3
#define DATA_START_LBA  4
#define LEGACY_SECTORS  64          /* old layout: 32 KiB reserved per name */
#define DISK_CAP_SECTORS (1024u * 1024u * 1024u / 512u)

/* Diskless boots keep a small heap-backed volume. The name table is not
 * this size: 10000 slots must not reserve hundreds of megabytes of RAM. */
#define RAM_VOLUME_SECTORS (DATA_START_LBA + FS_LEGACY_SLOTS * LEGACY_SECTORS)
#define RAM_CHUNK_SECTORS 64
#define RAM_CHUNKS ((RAM_VOLUME_SECTORS + RAM_CHUNK_SECTORS - 1) / RAM_CHUNK_SECTORS)

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

/* On disk the first 32 slots and the version word stay at their old offsets.
 * Version 3 keeps the real table in a free run (dir_lba), so the files that
 * already start at sector 4 do not move. */
typedef struct {
    uint32_t magic;
    uint32_t num_files;
    fs_entry_t legacy[FS_LEGACY_SLOTS];
    uint32_t version;
    uint32_t dir_lba;
    uint32_t dir_slots;
} superblock_t;

static superblock_t sb;
static fs_entry_t *ents;
static int mounted = 0;
static int legacy = 0;
static uint32_t volume_end = RAM_VOLUME_SECTORS;

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

static uint32_t sectors_of(uint32_t size) {
    if (size == 0) return 0;
    return (size + 511u) / 512u;
}

static int dir_live(void) {
    return sb.version >= FS_VERSION && ents;
}

static int slot_max(void) {
    return dir_live() ? FS_MAX_FILES : FS_LEGACY_SLOTS;
}

static fs_entry_t *slot_tab(void) {
    return dir_live() ? ents : sb.legacy;
}

static uint32_t dir_sectors(void) {
    return sectors_of(FS_MAX_FILES * (uint32_t) sizeof(fs_entry_t));
}

static int range_busy(uint32_t start, uint32_t nsec, int ignore) {
    uint32_t end = start + nsec;
    int nslots = slot_max();
    fs_entry_t *tab = slot_tab();
    for (int i = 0; i < nslots; i++) {
        if (i == ignore || !tab[i].used) continue;
        uint32_t n = sectors_of(tab[i].size);
        if (n == 0) continue;
        uint32_t a = tab[i].start_lba;
        uint32_t b = a + n;
        if (start < b && end > a) return 1;
    }
    if (sb.version >= FS_VERSION && sb.dir_lba) {
        uint32_t b = sb.dir_lba + dir_sectors();
        if (start < b && end > sb.dir_lba) return 1;
    }
    return 0;
}

static uint32_t alloc_run(uint32_t nsec, int ignore) {
    uint32_t c = DATA_START_LBA;
    while (c + nsec <= volume_end && c + nsec >= c) {
        if (!range_busy(c, nsec, ignore)) return c;
        uint32_t next = c + 1;
        int nslots = slot_max();
        fs_entry_t *tab = slot_tab();
        for (int i = 0; i < nslots; i++) {
            if (i == ignore || !tab[i].used) continue;
            uint32_t n = sectors_of(tab[i].size);
            if (n == 0) continue;
            uint32_t a = tab[i].start_lba;
            uint32_t b = a + n;
            if (c < b && c + nsec > a && b > next) next = b;
        }
        if (sb.version >= FS_VERSION && sb.dir_lba) {
            uint32_t b = sb.dir_lba + dir_sectors();
            if (c < b && c + nsec > sb.dir_lba && b > next) next = b;
        }
        if (next <= c) return 0;
        c = next;
    }
    return 0;
}

static int ensure_ents(void) {
    if (ents) return 0;
    ents = (fs_entry_t *) malloc(FS_MAX_FILES * (uint32_t) sizeof(fs_entry_t));
    if (!ents) return -1;
    memory_set((uint8_t *) ents, 0, (int) (FS_MAX_FILES * sizeof(fs_entry_t)));
    return 0;
}

static int dir_write_span(uint32_t byte_off, uint32_t nbytes) {
    uint32_t total = FS_MAX_FILES * (uint32_t) sizeof(fs_entry_t);
    uint32_t end = byte_off + nbytes;
    uint32_t s0 = byte_off / 512u;
    uint32_t s1 = (end - 1u) / 512u;
    for (uint32_t s = s0; s <= s1; s++) {
        uint8_t sec[512];
        memory_set(sec, 0, 512);
        uint32_t src = s * 512u;
        if (src < total) {
            uint32_t n = total - src;
            if (n > 512) n = 512;
            memory_copy((uint8_t *) ents + src, sec, (int) n);
        }
        if (dev_write(sb.dir_lba + s, 1, sec) != 0) return -1;
    }
    return 0;
}

static int dir_write_all(void) {
    uint32_t total = FS_MAX_FILES * (uint32_t) sizeof(fs_entry_t);
    uint32_t nsec = sectors_of(total);
    for (uint32_t s = 0; s < nsec; s++) {
        if (dir_write_span(s * 512u, 512) != 0) return -1;
    }
    return 0;
}

static int dir_load(void) {
    if (ensure_ents() != 0) return -1;
    uint32_t total = FS_MAX_FILES * (uint32_t) sizeof(fs_entry_t);
    uint32_t nsec = sectors_of(total);
    for (uint32_t s = 0; s < nsec; s++) {
        uint8_t sec[512];
        if (dev_read(sb.dir_lba + s, 1, sec) != 0) return -1;
        uint32_t src = s * 512u;
        if (src >= total) break;
        uint32_t n = total - src;
        if (n > 512) n = 512;
        memory_copy(sec, (uint8_t *) ents + src, (int) n);
    }
    return 0;
}

/* Version 2 stores 32 slots in the superblock. Copy them into a free run. */
static int upgrade_v2(void) {
    if (ensure_ents() != 0) return -1;
    for (int i = 0; i < FS_LEGACY_SLOTS; i++) ents[i] = sb.legacy[i];
    uint32_t lba = alloc_run(dir_sectors(), -1);
    if (!lba) return -1;
    sb.dir_lba = lba;
    sb.dir_slots = FS_MAX_FILES;
    sb.version = FS_VERSION;
    if (dir_write_all() != 0) return -1;
    return fs_write_sb();
}

void fs_format() {
    memory_set((uint8_t *) &sb, 0, sizeof(superblock_t));
    sb.magic = FS_MAGIC;
    sb.num_files = 0;
    legacy = 0;
    if (ensure_ents() != 0) {
        mounted = 0;
        return;
    }
    memory_set((uint8_t *) ents, 0, (int) (FS_MAX_FILES * sizeof(fs_entry_t)));
    uint32_t lba = alloc_run(dir_sectors(), -1);
    if (!lba) {
        mounted = 0;
        return;
    }
    sb.dir_lba = lba;
    sb.dir_slots = FS_MAX_FILES;
    sb.version = FS_VERSION;
    if (dir_write_all() != 0 || fs_write_sb() != 0) {
        mounted = 0;
        return;
    }
    mounted = 1;
}

/* Read every legacy file, then pack them into the contiguous layout.
 * Returns 0 on success. On failure the old slot map is left in place. */
static int fs_migrate(void) {
    typedef struct { char name[FS_NAME_LEN]; uint32_t size; uint8_t *data; } saved_t;
    saved_t saved[FS_LEGACY_SLOTS];
    int ns = 0;
    memory_set((uint8_t *) saved, 0, sizeof(saved));

    for (int i = 0; i < FS_LEGACY_SLOTS; i++) {
        if (!sb.legacy[i].used) continue;
        uint32_t size = sb.legacy[i].size;
        if (size > LEGACY_SECTORS * 512u) size = LEGACY_SECTORS * 512u;
        uint8_t *data = 0;
        if (size) {
            data = (uint8_t *) malloc(size);
            if (!data) {
                for (int k = 0; k < ns; k++) free(saved[k].data);
                return -1;
            }
            uint32_t lba = DATA_START_LBA + (uint32_t) i * LEGACY_SECTORS;
            for (uint32_t s = 0; s < sectors_of(size); s++) {
                uint8_t sec[512];
                if (dev_read(lba + s, 1, sec) != 0) {
                    free(data);
                    for (int k = 0; k < ns; k++) free(saved[k].data);
                    return -1;
                }
                uint32_t chunk = size - s * 512u;
                if (chunk > 512) chunk = 512;
                memory_copy(sec, data + s * 512u, (int) chunk);
            }
        }
        for (int k = 0; k < FS_NAME_LEN; k++) saved[ns].name[k] = sb.legacy[i].name[k];
        saved[ns].size = size;
        saved[ns].data = data;
        ns++;
    }

    uint32_t end = volume_end;
    fs_format();
    volume_end = end;
    for (int i = 0; i < ns; i++) {
        if (fs_write_file(saved[i].name, saved[i].data, saved[i].size) != 0) {
            for (int k = i; k < ns; k++) free(saved[k].data);
            return -1;
        }
        free(saved[i].data);
    }
    return 0;
}

int fs_mount() {
    if (mounted) return 1;
    if (!init_ata()) {
        /* No disk: a small RAM volume. The initrd fills it right after. */
        ram_backed = 1;
        volume_end = RAM_VOLUME_SECTORS;
        for (int i = 0; i < RAM_CHUNKS; i++) ram_chunk[i] = 0;
        fs_format();
        return mounted;
    }

    uint32_t sectors = ata_sector_count();
    if (sectors > DISK_CAP_SECTORS) sectors = DISK_CAP_SECTORS;
    if (sectors < RAM_VOLUME_SECTORS) sectors = RAM_VOLUME_SECTORS;
    volume_end = sectors;

    if (fs_read_sb() != 0 || sb.magic != FS_MAGIC) {
        fs_format();
        return mounted;
    }
    mounted = 1;
    if (sb.version == 0) {
        if (fs_migrate() != 0) {
            legacy = 1;
            serial_write("[fs] legacy layout kept\n");
        }
    } else if (sb.version == FS_VERSION_V2) {
        if (upgrade_v2() != 0) {
            mounted = 0;
            serial_write("[fs] directory upgrade failed\n");
        }
    } else if (sb.version == FS_VERSION) {
        if (sb.dir_lba < DATA_START_LBA || dir_load() != 0) {
            mounted = 0;
            serial_write("[fs] directory unreadable\n");
        }
    } else {
        mounted = 0;
        serial_write("[fs] unknown layout\n");
    }
    return 1;
}

static int fs_find(const char *name) {
    int nslots = slot_max();
    fs_entry_t *tab = slot_tab();
    for (int i = 0; i < nslots; i++) {
        if (tab[i].used && strcmp((char *) tab[i].name, (char *) name) == 0)
            return i;
    }
    return -1;
}

static int fs_alloc_slot() {
    int nslots = slot_max();
    fs_entry_t *tab = slot_tab();
    for (int i = 0; i < nslots; i++) {
        if (!tab[i].used) return i;
    }
    return -1;
}

static int commit_slot(int idx) {
    if (!dir_live()) return fs_write_sb();
    if (dir_write_span((uint32_t) idx * (uint32_t) sizeof(fs_entry_t),
                       (uint32_t) sizeof(fs_entry_t)) != 0)
        return -1;
    return fs_write_sb();
}

int fs_write_file(const char *name, const uint8_t *data, uint32_t size) {
    if (!mounted || size > FS_MAX_FILESIZE) return -1;

    fs_entry_t *tab = slot_tab();
    int idx = fs_find(name);
    int fresh = 0;
    if (idx < 0) {
        idx = fs_alloc_slot();
        if (idx < 0) return -1; /* directory full */
        for (int k = 0; k < FS_NAME_LEN; k++) tab[idx].name[k] = 0;
        for (int k = 0; name[k] && k < FS_NAME_LEN - 1; k++) tab[idx].name[k] = name[k];
        tab[idx].used = 1;
        tab[idx].size = 0;
        tab[idx].start_lba = 0;
        sb.num_files++;
        fresh = 1;
    }

    uint32_t sectors = sectors_of(size);
    if (legacy) {
        tab[idx].start_lba = DATA_START_LBA + (uint32_t) idx * LEGACY_SECTORS;
        if (sectors > LEGACY_SECTORS) {
            if (fresh) { tab[idx].used = 0; sb.num_files--; }
            return -1;
        }
    } else if (sectors > sectors_of(tab[idx].size)) {
        uint32_t lba = alloc_run(sectors, fresh ? -1 : idx);
        if (!lba) {
            if (fresh) { tab[idx].used = 0; sb.num_files--; }
            return -1;
        }
        tab[idx].start_lba = lba;
    }

    if (sectors == 0) {
        tab[idx].size = 0;
        if (commit_slot(idx) != 0) return -1;
        return 0;
    }

    for (uint32_t s = 0; s < sectors; s++) {
        uint8_t secbuf[512];
        memory_set(secbuf, 0, 512);
        uint32_t chunk = size - s * 512;
        if (chunk > 512) chunk = 512;
        memory_copy((uint8_t *) (data + s * 512), secbuf, (int) chunk);
        if (dev_write(tab[idx].start_lba + s, 1, secbuf) != 0)
            return -1;
    }

    tab[idx].size = size;
    if (commit_slot(idx) != 0) return -1;
    return 0;
}

int fs_read_file(const char *name, uint8_t *buf, uint32_t maxlen) {
    if (!mounted) return -1;
    int idx = fs_find(name);
    if (idx < 0) return -1;

    fs_entry_t *tab = slot_tab();
    uint32_t size = tab[idx].size;
    if (size > FS_MAX_FILESIZE) return -1;
    if (legacy && size > LEGACY_SECTORS * 512u) return -1;
    if (size > maxlen) size = maxlen;

    uint32_t sectors = (size + 511) / 512;
    for (uint32_t s = 0; s < sectors; s++) {
        uint8_t secbuf[512];
        if (dev_read(tab[idx].start_lba + s, 1, secbuf) != 0)
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

    fs_entry_t *tab = slot_tab();
    tab[idx].used = 0;
    tab[idx].size = 0;
    tab[idx].name[0] = '\0';
    if (sb.num_files > 0) sb.num_files--;
    if (commit_slot(idx) != 0) return -1;
    return 0;
}

int fs_list(fs_fileinfo_t *out, int max) {
    int n = 0;
    int nslots = slot_max();
    fs_entry_t *tab = slot_tab();
    for (int i = 0; i < nslots && n < max; i++) {
        if (tab[i].used) {
            for (int k = 0; k < FS_NAME_LEN; k++) out[n].name[k] = tab[i].name[k];
            out[n].size = tab[i].size;
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
    return idx < 0 ? -1 : (int) slot_tab()[idx].size;
}

int fs_rename(const char *from, const char *to) {
    if (!mounted || !from || !to || !to[0]) return -1;
    int nlen = 0;
    while (to[nlen]) nlen++;
    if (nlen >= FS_NAME_LEN) return -1;
    int idx = fs_find(from);
    if (idx < 0 || fs_find(to) >= 0) return -1;
    fs_entry_t *tab = slot_tab();
    for (int k = 0; k < FS_NAME_LEN; k++) tab[idx].name[k] = 0;
    for (int k = 0; k < nlen; k++) tab[idx].name[k] = to[k];
    return commit_slot(idx);
}

static void ensure_dir(const char *mark) {
    if (!fs_exists(mark)) fs_write_file(mark, 0, 0);
}

static int ends_elf(const char *name) {
    int n = 0;
    while (name[n]) n++;
    if (n < 4) return 0;
    return name[n - 4] == '.' && name[n - 3] == 'e' &&
           name[n - 2] == 'l' && name[n - 1] == 'f';
}

static int has_slash(const char *name) {
    for (int i = 0; name[i]; i++)
        if (name[i] == '/') return 1;
    return 0;
}

static void move_into(const char *dir, const char *leaf) {
    char dest[FS_NAME_LEN];
    int d = 0;
    while (dir[d]) { dest[d] = dir[d]; d++; }
    int i = 0;
    while (leaf[i] && d < FS_NAME_LEN - 1) dest[d++] = leaf[i++];
    dest[d] = '\0';
    if (d >= FS_NAME_LEN) return;
    fs_rename(leaf, dest);
}

void fs_organize(void) {
    if (!mounted) return;
    ensure_dir("bin/");
    ensure_dir("sys/");
    ensure_dir("home/");

    /* Programs the desk and the boot path launch live in bin/. */
    int nslots = slot_max();
    fs_entry_t *tab = slot_tab();
    for (int i = 0; i < nslots; i++) {
        if (!tab[i].used) continue;
        char name[FS_NAME_LEN];
        for (int k = 0; k < FS_NAME_LEN; k++) name[k] = tab[i].name[k];
        name[FS_NAME_LEN - 1] = '\0';
        if (!name[0] || has_slash(name) || name[0] == '.') continue;
        if (!ends_elf(name)) continue;
        move_into("bin/", name);
    }

    move_into("sys/", "accounts");
    move_into("sys/", "session");
    move_into("sys/", "attune");
    move_into("sys/", "bootcount");

    move_into("home/", "ideas.txt");
    move_into("home/", "mahyar.txt");
    move_into("home/", ".note.txt");
}
