#include "isobase.h"
#include "fs.h"
#include "../drivers/atapi.h"
#include "../drivers/serial.h"
#include "../libc/mem.h"

/* The ISO is the install medium. chaos.flp is the El Torito boot image.
 * /BASE/INDEX.TXT lists "install-name iso-name" pairs. The files sit in
 * that directory. ISO9660 stores the names in uppercase with an optional
 * ";1" version. */

static int eq_fold(const char *a, int an, const char *b) {
    int i = 0;
    while (b[i]) {
        if (i >= an) return 0;
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'a' && ca <= 'z') ca = (char) (ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char) (cb - 32);
        if (ca != cb) return 0;
        i++;
    }
    return i == an;
}

static int id_len(const uint8_t *id, int n) {
    int cut = n;
    for (int i = 0; i < n; i++) {
        if (id[i] == ';') { cut = i; break; }
    }
    while (cut > 0 && id[cut - 1] == '.') cut--;
    return cut;
}

/* Walk one ISO directory. On match, set extent and byte length. */
static int find_in_dir(uint32_t lba, uint32_t bytes, const char *want,
                       uint32_t *out_lba, uint32_t *out_len) {
    uint8_t sec[2048];
    uint32_t secs = (bytes + 2047u) / 2048u;
    for (uint32_t s = 0; s < secs; s++) {
        if (atapi_read(lba + s, sec) != 0) return -1;
        uint32_t off = 0;
        while (off + 33 < 2048) {
            uint8_t rec = sec[off];
            if (rec == 0) break;
            if (off + rec > 2048) break;
            uint32_t ext = (uint32_t) sec[off + 2] | ((uint32_t) sec[off + 3] << 8) |
                           ((uint32_t) sec[off + 4] << 16) | ((uint32_t) sec[off + 5] << 24);
            uint32_t len = (uint32_t) sec[off + 10] | ((uint32_t) sec[off + 11] << 8) |
                           ((uint32_t) sec[off + 12] << 16) | ((uint32_t) sec[off + 13] << 24);
            uint8_t flags = sec[off + 25];
            int nlen = sec[off + 32];
            if (nlen > 0 && off + 33 + (uint32_t) nlen <= 2048) {
                int cut = id_len(sec + off + 33, nlen);
                if (eq_fold((char *) (sec + off + 33), cut, want)) {
                    *out_lba = ext;
                    *out_len = len;
                    return (flags & 0x02) ? 2 : 1;
                }
            }
            off += rec;
        }
    }
    return 0;
}

static int copy_file(uint32_t lba, uint32_t size, const char *install_as) {
    int prep = fs_prepare(install_as, size);
    if (prep == 1) return 1;
    if (prep != 0) return -1;
    uint8_t sec[2048];
    uint32_t off = 0;
    while (off < size) {
        if (atapi_read(lba + off / 2048u, sec) != 0) return -1;
        uint32_t n = size - off;
        if (n > 2048u) n = 2048u;
        if (fs_write_at(install_as, off, sec, n) != 0) return -1;
        off += n;
    }
    return 0;
}

static int install_index(uint32_t lba, uint32_t size, uint32_t dir_lba, uint32_t dir_len) {
    if (size > 4096) size = 4096;
    uint8_t text[4096];
    memory_set(text, 0, sizeof(text));
    uint32_t got = 0;
    while (got < size) {
        uint8_t sec[2048];
        if (atapi_read(lba + got / 2048u, sec) != 0) return -1;
        uint32_t n = size - got;
        if (n > 2048) n = 2048;
        memory_copy(sec, text + got, (int) n);
        got += n;
    }
    uint32_t i = 0;
    while (i < got) {
        while (i < got && (text[i] == '\n' || text[i] == '\r')) i++;
        if (i >= got) break;
        char dest[24];
        char src[32];
        int d = 0, s = 0;
        while (i < got && text[i] != ' ' && text[i] != '\n' && d < 23)
            dest[d++] = (char) text[i++];
        dest[d] = 0;
        while (i < got && text[i] == ' ') i++;
        while (i < got && text[i] != '\n' && text[i] != '\r' && s < 31)
            src[s++] = (char) text[i++];
        src[s] = 0;
        while (i < got && text[i] != '\n') i++;
        if (!d || !s) continue;
        uint32_t flba = 0, flen = 0;
        int kind = find_in_dir(dir_lba, dir_len, src, &flba, &flen);
        if (kind != 1) {
            serial_write("[base] missing ");
            serial_write(src);
            serial_write("\n");
            continue;
        }
        int rc = copy_file(flba, flen, dest);
        if (rc < 0) {
            serial_write("[base] failed ");
            serial_write(dest);
            serial_write("\n");
        } else if (rc == 0) {
            serial_write("[base] installed ");
            serial_write(dest);
            serial_write("\n");
        }
    }
    return 0;
}

void base_install(void) {
    if (!atapi_init()) return;
    uint8_t sec[2048];
    if (atapi_read(16, sec) != 0) {
        serial_write("[base] cannot read volume descriptor\n");
        return;
    }
    if (sec[0] != 1 || sec[1] != 'C' || sec[2] != 'D' || sec[3] != '0' ||
        sec[4] != '0' || sec[5] != '1') {
        serial_write("[base] not an ISO9660 disc\n");
        return;
    }
    uint32_t root_lba = (uint32_t) sec[156 + 2] | ((uint32_t) sec[156 + 3] << 8) |
                        ((uint32_t) sec[156 + 4] << 16) | ((uint32_t) sec[156 + 5] << 24);
    uint32_t root_len = (uint32_t) sec[156 + 10] | ((uint32_t) sec[156 + 11] << 8) |
                        ((uint32_t) sec[156 + 12] << 16) | ((uint32_t) sec[156 + 13] << 24);
    uint32_t base_lba = 0, base_len = 0;
    if (find_in_dir(root_lba, root_len, "BASE", &base_lba, &base_len) != 2) {
        serial_write("[base] no BASE directory\n");
        return;
    }
    uint32_t idx_lba = 0, idx_len = 0;
    if (find_in_dir(base_lba, base_len, "INDEX.TXT", &idx_lba, &idx_len) != 1) {
        serial_write("[base] no index\n");
        return;
    }
    serial_write("[base] installing from disc\n");
    install_index(idx_lba, idx_len, base_lba, base_len);
}
