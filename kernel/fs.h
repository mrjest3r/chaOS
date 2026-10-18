#ifndef FS_H
#define FS_H

#include <stdint.h>

#define FS_NAME_LEN     24
#define FS_MAX_FILES    10000 /* directory block, not the 32-slot superblock */
#define FS_MAX_FILESIZE (32u * 1024u * 1024u) /* 32 MiB per file; the volume holds ~1 GiB */

typedef struct {
    char name[FS_NAME_LEN];
    uint32_t size;
} fs_fileinfo_t;

/* Reads the superblock; formats the disk automatically if unformatted.
 * Returns 1 if a filesystem is available, 0 if there's no disk. */
int fs_mount();

/* Wipe and initialize an empty filesystem. */
void fs_format();

/* Create or overwrite a file. Returns 0 on success, -1 on error. */
int fs_write_file(const char *name, const uint8_t *data, uint32_t size);

/* Read a file into 'buf' (up to maxlen bytes).
 * Returns bytes read, or -1 if not found. */
int fs_read_file(const char *name, uint8_t *buf, uint32_t maxlen);

/* Delete a file. Returns 0 on success, -1 if not found. */
int fs_delete(const char *name);

/* Change a directory entry's name. File bytes stay where they are.
 * Returns 0 on success, -1 if missing, the new name is taken, or it
 * does not fit in FS_NAME_LEN. */
int fs_rename(const char *from, const char *to);

/* Create bin/, sys/, and home/ and move the flat names that belong there.
 * Safe to call on every mount: a name already in place is left alone. */
void fs_organize(void);

/* Fill 'out' with info for up to 'max' files. Returns the number written. */
int fs_list(fs_fileinfo_t *out, int max);

/* Returns 1 if a file with this name exists, 0 otherwise. */
int fs_exists(const char *name);

/* Size in bytes of an existing file, or -1. */
int fs_size(const char *name);

/* Reserve 'size' bytes for 'name'. 1 = already that size, 0 = ready to
 * fill with fs_write_at, -1 = error. */
int fs_prepare(const char *name, uint32_t size);
int fs_write_at(const char *name, uint32_t offset, const uint8_t *data, uint32_t len);

/* 1 when the filesystem lives in RAM (no IDE disk found; nothing persists). */
int fs_is_ram(void);

#endif
