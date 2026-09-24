#include "elf.h"
#include "fs.h"
#include "kheap.h"
#include "task.h"
#include "../cpu/arch.h"
#include "../libc/mem.h"
#include "../drivers/screen.h"
#include "../drivers/keyboard.h"

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

/* ---- argc / argv ----------------------------------------------------------
 * The command line is copied into the top of the new process's stack (via the
 * identity-mapped physical frame, since the process's address space is not
 * active here) and laid out like this, growing downwards:
 *
 *   USER_STACK_TOP
 *     - ARGS_AREA .. TOP   argument strings, then the argv[] pointer array
 *     - ARGS_AREA - 4      argv       (user virtual address of argv[0] slot)
 *     - ARGS_AREA - 8      argc       <- initial ESP
 *
 * crt0's 'call main' then produces a normal cdecl frame: main(argc, argv). */
#define ARGS_AREA 256
#define MAX_ARGS  8

static uint32_t setup_user_args(uint32_t top_frame, const char *cmdline) {
    uint8_t *kbase = (uint8_t *) (top_frame + PAGE_SIZE - ARGS_AREA); /* kernel view */
    uint32_t vbase = USER_STACK_TOP - ARGS_AREA;                      /* user view   */

    /* Strings go at the start of the area, the argv array at its end. */
    char *strs = (char *) kbase;
    int str_space = ARGS_AREA - (MAX_ARGS + 1) * 4;
    uint32_t *argv_k = (uint32_t *) (kbase + str_space);
    uint32_t  argv_v = vbase + (uint32_t) str_space;

    int argc = 0, pos = 0, i = 0;
    while (cmdline[i] && argc < MAX_ARGS) {
        while (cmdline[i] == ' ') i++;
        if (!cmdline[i]) break;
        argv_k[argc++] = vbase + (uint32_t) pos;
        while (cmdline[i] && cmdline[i] != ' ' && pos < str_space - 1)
            strs[pos++] = cmdline[i++];
        strs[pos++] = '\0';
    }
    argv_k[argc] = 0;

    /* [argc][argv] just below the area; ESP points at argc. */
    uint32_t *sp = (uint32_t *) (kbase - 8);
    sp[0] = (uint32_t) argc;
    sp[1] = argv_v;
    return vbase - 8;
}

int elf_spawn(const char *path) {
    return elf_spawn_io(path, -1, -1);
}

int elf_spawn_io(const char *path, int stdin_pipe, int stdout_pipe) {
    /* First token of the command line is the program filename; the whole line
     * (including the name, as argv[0]) becomes the program's arguments. */
    int s = 0;
    while (path[s] == ' ') s++;
    const char *cmdline = path + s;

    char name[FS_NAME_LEN];
    int j = 0;
    while (cmdline[j] && cmdline[j] != ' ' && j < FS_NAME_LEN - 1) {
        name[j] = cmdline[j];
        j++;
    }
    name[j] = '\0';
    if (name[0] == '\0') return -105;

    /* Fail fast before touching the heap / address spaces. A missing file used
     * to still malloc a 32 KiB buffer; under memory pressure that could leave
     * the kernel in a bad state before the obvious error was reported. */
    if (!fs_exists(name)) return -101;

    /* Read the whole file into a temporary heap buffer. */
    uint8_t *buf = (uint8_t *) malloc(FS_MAX_FILESIZE);
    if (!buf) return -100;

    int n = fs_read_file(name, buf, FS_MAX_FILESIZE);
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

    /* Map the ring-3 stack at the top of the user window. Remember the frame
     * backing the topmost page: the argument block is written into it. */
    int ok = 1;
    uint32_t top_frame = 0;
    for (int i = 0; i < USER_STACK_PAGES; i++) {
        uint32_t f = frame_alloc();
        if (!f) { ok = 0; break; }
        memory_set((uint8_t *) f, 0, PAGE_SIZE);
        if (i == 0) top_frame = f;
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

    uint32_t esp = setup_user_args(top_frame, cmdline);

    int id = task_create_user(entry, pd, esp);
    if (id < 0) {
        vmm_destroy_addrspace(pd);
        return -104;
    }
    /* Runs before the child can be scheduled: callers reach here from the
     * syscall gate (IF=0) or from the kernel shell, which never redirects. */
    if (stdin_pipe >= 0 || stdout_pipe >= 0) task_set_io(id, stdin_pipe, stdout_pipe);
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

    /* Foreground program: it owns the keyboard until it exits. */
    kbd_set_focus(KBD_FOCUS_PROGRAM);

    /* Idle until the program terminates; the timer keeps preempting us so it
     * actually runs (and any other tasks keep making progress too). */
    while (task_alive(id)) asm volatile("hlt");

    kbd_set_focus(KBD_FOCUS_SHELL);
    /* Reap DEAD slot and ensure we are back on the kernel page directory. */
    task_reap();
    if (kernel_directory) {
        load_page_directory(kernel_directory);
    }
    return 0;
}
