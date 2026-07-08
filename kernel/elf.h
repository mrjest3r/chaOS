#ifndef ELF_H
#define ELF_H

#include <stdint.h>

/* Region where user programs are loaded. Sits above the kernel heap (which
 * ends at 4 MiB) and below the identity-map limit (16 MiB). User ELF programs
 * must be linked to run inside this window (see user/linker.ld). */
#define USER_LOAD_BASE  0x00400000u
#define USER_LOAD_LIMIT 0x01000000u

/* ---- ELF32 on-disk structures (little-endian, i386) --------------------- */

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) Elf32_Phdr;

#define ET_EXEC     2
#define EM_386      3
#define PT_LOAD     1
#define ELFCLASS32  1
#define ELFDATA2LSB 1

/* Validates an ELF32 image held in 'data' (of 'size' bytes) and copies its
 * PT_LOAD segments into the user load region. On success returns 0 and stores
 * the entry point in *entry_out. Returns a negative value on any error. */
int elf_load(const uint8_t *data, uint32_t size, uint32_t *entry_out);

/* Reads 'path' from the filesystem, loads it, spawns a ring-3 task at its entry
 * point and blocks until that task exits. Returns 0 on success, negative on
 * error (file missing, not a valid ELF, out of memory, ...). */
int elf_exec(const char *path);

#endif
