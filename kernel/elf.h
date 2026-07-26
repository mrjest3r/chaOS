#ifndef ELF_H
#define ELF_H

#include <stdint.h>

/* User programs are linked to run at USER_BASE (see cpu/vmm.h and
 * user/linker.ld) and are loaded into a private per-process address space. */

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

/* Validates an ELF32 image held in 'data' (of 'size' bytes) and loads its
 * PT_LOAD segments into the address space 'pd' (a page directory created with
 * vmm_create_addrspace), allocating and mapping user frames as it goes. On
 * success returns 0 and stores the entry point in *entry_out. Returns a
 * negative value on any error. Must run with the kernel identity map reachable
 * (the caller is normally the kernel/shell task). */
int elf_load_into(uint32_t pd, const uint8_t *data, uint32_t size, uint32_t *entry_out);

/* Reads 'path' from disk, builds a fresh isolated address space, loads the
 * program into it and creates a ring-3 task. Returns the new task id, or a
 * negative value on error. Does NOT wait for the task to finish. */
int elf_spawn(const char *path);

/* Like elf_spawn, but blocks until the spawned task exits. Returns 0 on success
 * (program ran and exited), negative on error. */
int elf_exec(const char *path);

#endif
