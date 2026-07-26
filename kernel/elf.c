#include "elf.h"
#include "fs.h"
#include "kheap.h"
#include "task.h"
#include "../cpu/vmm.h"
#include "../cpu/paging.h"
#include "../libc/mem.h"
#include "../drivers/screen.h"

/* A statically-linked ELF executable places each PT_LOAD segment at a fixed
 * virtual address (USER_BASE). Every program is loaded into its own address
 * space, so many can run at once at the same virtual address, fully isolated
 * from each other and from the kernel. */

static int seg_in_user_region(uint32_t vaddr, uint32_t memsz) {
    if (vaddr < USER_BASE) return 0;
    if (vaddr + memsz < vaddr) return 0;          /* overflow */
    if (vaddr + memsz > USER_PROG_LIMIT) return 0; /* must stay below the stack */
    return 1;
}

int elf_load_into(uint32_t pd, const uint8_t *data, uint32_t size, uint32_t *entry_out) {
    if (size < sizeof(Elf32_Ehdr)) return -1;

    const Elf32_Ehdr *eh = (const Elf32_Ehdr *) data;

    /* Magic: 0x7F 'E' 'L' 'F' */
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F') {
        return -2;
    }
    if (eh->e_ident[4] != ELFCLASS32 || eh->e_ident[5] != ELFDATA2LSB) return -3;
    if (eh->e_type != ET_EXEC || eh->e_machine != EM_386) return -4;
    if (eh->e_phoff == 0 || eh->e_phnum == 0 ||
        eh->e_phoff + (uint32_t) eh->e_phnum * eh->e_phentsize > size) {
        return -5;
    }

    /* Walk the program headers and load every PT_LOAD segment. */
    for (int i = 0; i < eh->e_phnum; i++) {
        const Elf32_Phdr *ph =
            (const Elf32_Phdr *) (data + eh->e_phoff + (uint32_t) i * eh->e_phentsize);

        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_filesz > ph->p_memsz) return -6;
        if (ph->p_offset + ph->p_filesz > size) return -7;
        if (ph->p_vaddr & 0xFFFu) return -8;                 /* page-aligned only */
        if (!seg_in_user_region(ph->p_vaddr, ph->p_memsz)) return -9;

        /* Back the segment with fresh frames, one page at a time: zero the page,
         * copy in whatever part of it is file-backed (the rest stays zero for
         * .bss), then map it into the target address space as user r/w. */
        for (uint32_t off = 0; off < ph->p_memsz; off += PAGE_SIZE) {
            uint32_t frame = frame_alloc();
            if (!frame) return -20;
            memory_set((uint8_t *) frame, 0, PAGE_SIZE);

            if (off < ph->p_filesz) {
                uint32_t tocopy = ph->p_filesz - off;
                if (tocopy > PAGE_SIZE) tocopy = PAGE_SIZE;
                memory_copy((uint8_t *) (data + ph->p_offset + off),
                            (uint8_t *) frame, (int) tocopy);
            }

            if (vmm_map(pd, ph->p_vaddr + off, frame,
                        PAGE_PRESENT | PAGE_RW | PAGE_USER) != 0) {
                return -21;
            }
        }
    }

    if (!seg_in_user_region(eh->e_entry, 1)) return -10;

    *entry_out = eh->e_entry;
    return 0;
}

int elf_spawn(const char *path) {
    /* Read the whole file into a temporary heap buffer. */
    uint8_t *buf = (uint8_t *) malloc(FS_MAX_FILESIZE);
    if (!buf) return -100;

    int n = fs_read_file(path, buf, FS_MAX_FILESIZE);
    if (n < 0) {
        free(buf);
        return -101;
    }

    /* Fresh address space: shares the kernel mappings, empty user region. */
    uint32_t pd = vmm_create_addrspace();
    if (!pd) {
        free(buf);
        return -102;
    }

    /* Map the ring-3 stack at the top of the user window. */
    int ok = 1;
    for (int i = 0; i < USER_STACK_PAGES; i++) {
        uint32_t f = frame_alloc();
        if (!f) { ok = 0; break; }
        memory_set((uint8_t *) f, 0, PAGE_SIZE);
        if (vmm_map(pd, USER_STACK_TOP - (uint32_t) (i + 1) * PAGE_SIZE, f,
                    PAGE_PRESENT | PAGE_RW | PAGE_USER) != 0) {
            ok = 0;
            break;
        }
    }

    uint32_t entry = 0;
    int rc = ok ? elf_load_into(pd, buf, (uint32_t) n, &entry) : -103;
    free(buf);
    if (rc != 0) {
        vmm_destroy_addrspace(pd);
        return rc;
    }

    int id = task_create_user(entry, pd, USER_STACK_TOP);
    if (id < 0) {
        vmm_destroy_addrspace(pd);
        return -104;
    }
    return id;
}

int elf_exec(const char *path) {
    int id = elf_spawn(path);
    if (id < 0) {
        kprint("exec: could not load ");
        kprint((char *) path);
        kprint("\n");
        return id;
    }

    /* Idle until the program terminates; the timer keeps preempting us so it
     * actually runs (and any other tasks keep making progress too). */
    while (task_alive(id)) asm volatile("hlt");
    return 0;
}
