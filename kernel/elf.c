#include "elf.h"
#include "fs.h"
#include "kheap.h"
#include "task.h"
#include "../libc/mem.h"
#include "../drivers/screen.h"

/* A statically-linked ELF executable places each PT_LOAD segment at a fixed
 * virtual address. Since we do not yet have per-process address spaces, we load
 * straight into the identity-mapped user region and only allow one program to
 * run at a time (elf_exec blocks until it finishes). */

static int seg_in_user_region(uint32_t vaddr, uint32_t memsz) {
    if (vaddr < USER_LOAD_BASE) return 0;
    /* guard against overflow, then check the upper bound */
    if (vaddr + memsz < vaddr) return 0;
    if (vaddr + memsz > USER_LOAD_LIMIT) return 0;
    return 1;
}

int elf_load(const uint8_t *data, uint32_t size, uint32_t *entry_out) {
    if (size < sizeof(Elf32_Ehdr)) return -1;

    const Elf32_Ehdr *eh = (const Elf32_Ehdr *) data;

    /* Magic: 0x7F 'E' 'L' 'F' */
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F') {
        kprint("elf: bad magic\n");
        return -2;
    }
    if (eh->e_ident[4] != ELFCLASS32 || eh->e_ident[5] != ELFDATA2LSB) {
        kprint("elf: not 32-bit little-endian\n");
        return -3;
    }
    if (eh->e_type != ET_EXEC || eh->e_machine != EM_386) {
        kprint("elf: not an i386 executable\n");
        return -4;
    }
    if (eh->e_phoff == 0 || eh->e_phnum == 0 ||
        eh->e_phoff + (uint32_t) eh->e_phnum * eh->e_phentsize > size) {
        kprint("elf: bad program headers\n");
        return -5;
    }

    /* Walk the program headers and load every PT_LOAD segment. */
    for (int i = 0; i < eh->e_phnum; i++) {
        const Elf32_Phdr *ph =
            (const Elf32_Phdr *) (data + eh->e_phoff + (uint32_t) i * eh->e_phentsize);

        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_filesz > ph->p_memsz) return -6;
        if (ph->p_offset + ph->p_filesz > size) return -7;
        if (!seg_in_user_region(ph->p_vaddr, ph->p_memsz)) {
            kprint("elf: segment outside user region\n");
            return -8;
        }

        uint8_t *dest = (uint8_t *) ph->p_vaddr;
        /* Copy the file-backed part, then zero the rest (.bss). */
        memory_copy((uint8_t *) (data + ph->p_offset), dest, (int) ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz) {
            memory_set(dest + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
        }
    }

    if (!seg_in_user_region(eh->e_entry, 1)) {
        kprint("elf: entry point outside user region\n");
        return -9;
    }

    *entry_out = eh->e_entry;
    return 0;
}

int elf_exec(const char *path) {
    /* Read the whole file into a temporary heap buffer. */
    uint8_t *buf = (uint8_t *) malloc(FS_MAX_FILESIZE);
    if (!buf) return -100;

    int n = fs_read_file(path, buf, FS_MAX_FILESIZE);
    if (n < 0) {
        kprint("exec: file not found: ");
        kprint((char *) path);
        kprint("\n");
        free(buf);
        return -101;
    }

    uint32_t entry = 0;
    int rc = elf_load(buf, (uint32_t) n, &entry);
    free(buf); /* segments are already copied to the load region */
    if (rc != 0) return rc;

    /* Spawn a ring-3 task at the ELF entry point and wait for it to finish. */
    int id = task_create_user((void (*)()) entry);
    if (id < 0) return -102;

    while (task_alive(id)) asm volatile("hlt");
    return 0;
}
