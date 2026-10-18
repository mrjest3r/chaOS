/* Stage the base-OS directory that chaos.iso carries next to the boot floppy.
 *
 * The boot floppy (chaos.flp) is still what the BIOS loads. BASE/ is what the
 * kernel copies onto the machine after boot: every system program, plus Doom.
 * Personal files are not included.
 *
 *   stagebase <disk.img> <outdir> <elf> [elf ...]
 *
 * doom.elf and freedoom1.wad are read out of disk.img (they are not in the
 * git tree). Each elf argument is copied in under its basename and recorded
 * as bin/<basename>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define NAME_LEN 24
#define ENT 36
#define SECTOR 512

static int extract_named(FILE *img, const char *want, const char *dest) {
    uint8_t sb[2048];
    if (fseek(img, 0, SEEK_SET) != 0 || fread(sb, 1, sizeof(sb), img) != sizeof(sb))
        return -1;
    uint32_t magic;
    memcpy(&magic, sb, 4);
    if (magic != 0x4D594653u) {
        fprintf(stderr, "stagebase: %s is not a chaOS disk\n", "disk");
        return -1;
    }
    uint32_t ver, dir_lba, slots;
    memcpy(&ver, sb + 1160, 4);
    memcpy(&dir_lba, sb + 1164, 4);
    memcpy(&slots, sb + 1168, 4);
    if (ver < 3 || !dir_lba || !slots) {
        fprintf(stderr, "stagebase: disk has no version-3 directory\n");
        return -1;
    }
    if (fseek(img, (long) dir_lba * SECTOR, SEEK_SET) != 0) return -1;
    for (uint32_t i = 0; i < slots; i++) {
        uint8_t ent[ENT];
        if (fread(ent, 1, ENT, img) != ENT) return -1;
        uint32_t used;
        memcpy(&used, ent + 32, 4);
        if (!used) continue;
        char name[NAME_LEN + 1];
        memcpy(name, ent, NAME_LEN);
        name[NAME_LEN] = 0;
        if (strcmp(name, want) != 0) continue;
        uint32_t size, lba;
        memcpy(&size, ent + 24, 4);
        memcpy(&lba, ent + 28, 4);
        FILE *out = fopen(dest, "wb");
        if (!out) { perror(dest); return -1; }
        if (fseek(img, (long) lba * SECTOR, SEEK_SET) != 0) { fclose(out); return -1; }
        uint32_t left = size;
        while (left) {
            uint8_t sec[SECTOR];
            if (fread(sec, 1, SECTOR, img) != SECTOR) { fclose(out); return -1; }
            uint32_t n = left > SECTOR ? SECTOR : left;
            if (fwrite(sec, 1, n, out) != n) { fclose(out); return -1; }
            left -= n;
        }
        fclose(out);
        /* The directory read position is lost; caller reopens the scan. */
        printf("stagebase: %s (%u bytes)\n", want, size);
        return 0;
    }
    fprintf(stderr, "stagebase: %s is not on the disk\n", want);
    return -1;
}

static const char *basename_of(const char *p) {
    const char *b = p;
    for (const char *s = p; *s; s++)
        if (*s == '/') b = s + 1;
    return b;
}

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) { perror(src); return -1; }
    FILE *out = fopen(dst, "wb");
    if (!out) { perror(dst); fclose(in); return -1; }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { fclose(in); fclose(out); return -1; }
    }
    fclose(in);
    fclose(out);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <disk.img> <outdir> <elf> [elf ...]\n", argv[0]);
        return 1;
    }
    const char *disk = argv[1];
    const char *dir = argv[2];
    FILE *img = fopen(disk, "rb");
    if (!img) { perror(disk); return 1; }

    char path[512];
    snprintf(path, sizeof(path), "%s/doom.elf", dir);
    if (extract_named(img, "bin/doom.elf", path) != 0) { fclose(img); return 1; }
    snprintf(path, sizeof(path), "%s/freedoom1.wad", dir);
    if (extract_named(img, "freedoom1.wad", path) != 0) { fclose(img); return 1; }
    fclose(img);

    snprintf(path, sizeof(path), "%s/index.txt", dir);
    FILE *idx = fopen(path, "wb");
    if (!idx) { perror(path); return 1; }
    fputs("bin/doom.elf doom.elf\n", idx);
    fputs("freedoom1.wad freedoom1.wad\n", idx);
    for (int i = 3; i < argc; i++) {
        const char *base = basename_of(argv[i]);
        snprintf(path, sizeof(path), "%s/%s", dir, base);
        if (copy_file(argv[i], path) != 0) { fclose(idx); return 1; }
        fprintf(idx, "bin/%s %s\n", base, base);
    }
    fclose(idx);
    printf("stagebase: index written\n");
    return 0;
}
