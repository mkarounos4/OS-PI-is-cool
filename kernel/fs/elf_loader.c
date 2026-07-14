#include "elf_loader.h"

#include <stddef.h>

#include "errors.h"
#include "kapi.h"
#include "memory/kmalloc.h"
#include "memory/page_table/page_table.h"
#include "oft.h"
#include "signals/signals.h"
#include "string.h"
#include "threading/thread.h"

#define ELF_NIDENT 16
#define ELF_CLASS_64 2
#define ELF_DATA_LSB 1
#define ELF_VERSION_CURRENT 1
#define ELF_TYPE_EXEC 2
#define ELF_MACHINE_AARCH64 183
#define ELF_PH_TYPE_LOAD 1
#define EXEC_MAX_ARGS 32
#define EXEC_MAX_ARG_LEN 256
#define PAGE_OFFSET_MASK (PAGE_SIZE - 1)

typedef struct elf64_ehdr_st {
    unsigned char e_ident[ELF_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_ehdr_t;

typedef struct elf64_phdr_st {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

static uint64_t min_u64(uint64_t a, uint64_t b) {
    return a < b ? a : b;
}

static void copy_bytes(void *dst, const void *src, size_t size) {
    uint8_t *dst_bytes = dst;
    const uint8_t *src_bytes = src;
    for (size_t i = 0; i < size; i++) {
        dst_bytes[i] = src_bytes[i];
    }
}

static void install_ttbr0(uint64_t ttbr0_el1) {
    asm volatile(
        "dsb ishst\n"
        "msr ttbr0_el1, %0\n"
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "isb\n"
        :
        : "r"(ttbr0_el1)
        : "memory");
}

static int elf_load_user_segment(uint64_t *table, ino_id_t ino_id,
                                 uint64_t file_offset, uint64_t file_size,
                                 uint64_t va, uint64_t pa, uint64_t mem_size,
                                 uint32_t flags) {
    return load_memory_segment(table, ino_id, file_offset, file_size, va, pa,
                               mem_size, flags);
}

static void free_exec_args(char **args, int argc) {
    for (int i = 0; i < argc; i++) {
        kfree(args[i]);
    }
}

static int copy_exec_arg(const char *src, char **dst) {
    size_t len = 0;
    while (len < EXEC_MAX_ARG_LEN && src[len] != '\0') {
        len++;
    }
    if (len == EXEC_MAX_ARG_LEN) {
        return INVALID_ARGS;
    }

    char *copy = kmalloc(len + 1);
    if (copy == NULL) {
        return INVALID_ARGS;
    }

    copy_bytes(copy, src, len + 1);
    *dst = copy;
    return SUCCESS;
}

static int copy_exec_args(char *const argv[], char **args, int *argc_out) {
    int argc = 0;
    if (argv != NULL) {
        while (argv[argc] != NULL) {
            if (argc >= EXEC_MAX_ARGS) {
                free_exec_args(args, argc);
                return INVALID_ARGS;
            }

            int err = copy_exec_arg(argv[argc], &args[argc]);
            if (err != SUCCESS) {
                free_exec_args(args, argc);
                return err;
            }
            argc++;
        }
    }

    *argc_out = argc;
    return SUCCESS;
}

static void copy_exec_name(char *dst, size_t dst_size, const char *path) {
    if (dst_size == 0) {
        return;
    }

    const char *name = path;
    for (size_t i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/' && path[i + 1] != '\0') {
            name = path + i + 1;
        }
    }

    size_t i = 0;
    while (i + 1 < dst_size && name[i] != '\0') {
        dst[i] = name[i];
        i++;
    }
    dst[i] = '\0';
}

static int stack_write(uint64_t *table, uint64_t va, const void *src,
                       size_t size) {
    uint64_t stack_base = USER_STACK_TOP - USER_STACK_SIZE;
    if (va < stack_base || va > USER_STACK_TOP ||
        size > USER_STACK_TOP - va) {
        return INVALID_ARGS;
    }

    const uint8_t *src_bytes = src;
    while (size > 0) {
        uint64_t page_va = va & ~PAGE_OFFSET_MASK;
        uint8_t *page = pt_get_mapped_page(table, page_va);
        if (page == NULL) {
            page = pt_seed_user_page(table, page_va);
            if (page == NULL) {
                return INVALID_ARGS;
            }
        }

        uint64_t page_offset = va & PAGE_OFFSET_MASK;
        uint64_t chunk = min_u64(size, PAGE_SIZE - page_offset);
        copy_bytes(page + page_offset, src_bytes, (size_t)chunk);

        va += chunk;
        src_bytes += chunk;
        size -= chunk;
    }

    return SUCCESS;
}

static int setup_exec_stack(uint64_t *table, char **args, int argc,
                            uint64_t *sp_out, uint64_t *argv_out) {
    uint64_t stack_base = USER_STACK_TOP - USER_STACK_SIZE;
    uint64_t sp = USER_STACK_TOP;
    uint64_t argv_ptrs[EXEC_MAX_ARGS + 1];

    for (int i = argc - 1; i >= 0; i--) {
        size_t len = strlen(args[i]) + 1;
        if (sp < stack_base + len) {
            return INVALID_ARGS;
        }
        sp -= len;
        int err = stack_write(table, sp, args[i], len);
        if (err != SUCCESS) {
            return err;
        }
        argv_ptrs[i] = sp;
    }
    argv_ptrs[argc] = 0;

    sp &= ~UINT64_C(0xf);
    size_t argv_bytes = (size_t)(argc + 1) * sizeof(uint64_t);
    if (sp < stack_base + argv_bytes) {
        return INVALID_ARGS;
    }
    sp -= argv_bytes;
    int err = stack_write(table, sp, argv_ptrs, argv_bytes);
    if (err != SUCCESS) {
        return err;
    }

    *sp_out = sp;
    *argv_out = sp;
    return SUCCESS;
}

static void reset_exec_signal_dispositions(pcb_t *pcb) {
    for (int signum = 0; signum < 32; signum++) {
        if (signum != SIGKILL && signum != SIGSTOP &&
            pcb->sigactions[signum].sa_handler == SIG_IGN) {
            continue;
        }

        pcb->sigactions[signum].sa_handler = SIG_DFL;
        pcb->sigactions[signum].sa_mask = 0;
        pcb->sigactions[signum].sa_flags = 0;
    }
}

static int read_exact_at(int fd, struct oft_entry *entry, uint64_t offset,
                         void *buf, int size) {
    int err = k_lseek(fd, (int)offset, F_SEEK_SET);
    if (err < 0) {
        return err;
    }

    int total_read = 0;
    while (total_read < size) {
        int bytes_read = k_read(entry, (char *)buf + total_read,
                                (size_t)(size - total_read));
        if (bytes_read < 0) {
            return bytes_read;
        }
        if (bytes_read == 0) {
            return FILE_READ_ERROR;
        }
        total_read += bytes_read;
    }
    return SUCCESS;
}

static int validate_elf_header(const elf64_ehdr_t *header, int file_size) {
    if (file_size < (int)sizeof(elf64_ehdr_t)) {
        return INVALID_ARGS;
    }

    if (header->e_ident[0] != 0x7f || header->e_ident[1] != 'E' ||
        header->e_ident[2] != 'L' || header->e_ident[3] != 'F') {
        return INVALID_ARGS;
    }
    if (header->e_ident[4] != ELF_CLASS_64 ||
        header->e_ident[5] != ELF_DATA_LSB ||
        header->e_ident[6] != ELF_VERSION_CURRENT) {
        return INVALID_ARGS;
    }
    if (header->e_type != ELF_TYPE_EXEC ||
        header->e_machine != ELF_MACHINE_AARCH64 ||
        header->e_version != ELF_VERSION_CURRENT) {
        return INVALID_ARGS;
    }
    if (header->e_ehsize < sizeof(elf64_ehdr_t) ||
        header->e_phentsize != sizeof(elf64_phdr_t)) {
        return INVALID_ARGS;
    }
    if (header->e_phnum == 0) {
        return INVALID_ARGS;
    }
    if (header->e_phoff > (uint64_t)file_size ||
        header->e_phnum > ((uint64_t)file_size - header->e_phoff) /
                           header->e_phentsize) {
        return INVALID_ARGS;
    }

    return SUCCESS;
}

int elf_exec_process(pcb_t *pcb, const char *path, char *const argv[],
                     struct trap_frame *frame, uint64_t frame_va,
                     struct trap_frame **next_frame, int install_table) {
    if (pcb == NULL || path == NULL || frame == NULL) {
        return INVALID_ARGS;
    }

    // kill all threads other than myself
    for (size_t i = 0; i < vec_len(&pcb->tids); i++) {
        tcb_t *tcb = thread_get_by_tid((tid_t)(uintptr_t)vec_get(&pcb->tids, i));
        if (tcb == get_curr_thread())  continue;

        thread_detach(tcb->tid);
        terminate_thread(tcb);
    }

    char *args[EXEC_MAX_ARGS];
    int argc = 0;
    int err = copy_exec_args(argv, args, &argc);
    if (err != SUCCESS) {
        return err;
    }

    char exec_name[32];
    copy_exec_name(exec_name, sizeof(exec_name), path);

    int fd = k_open(path, O_RDONLY);
    if (fd < 0) {
        free_exec_args(args, argc);
        return fd;
    }

    struct oft_entry *entry;
    err = get_oft_entry_by_fd(fd, &entry);
    if (err != SUCCESS) {
        free_exec_args(args, argc);
        return err;
    }

    int file_size = get_file_size(entry);
    if (file_size <= 0) {
        free_exec_args(args, argc);
        k_close(entry);
        return FILE_READ_ERROR;
    }

    elf64_ehdr_t header;
    err = read_exact_at(fd, entry, 0, &header, sizeof(header));
    if (err != SUCCESS) {
        free_exec_args(args, argc);
        k_close(entry);
        return err;
    }

    err = validate_elf_header(&header, file_size);
    if (err != SUCCESS) {
        free_exec_args(args, argc);
        k_close(entry);
        return err;
    }

    uint64_t entry_pc = header.e_entry;
    uint64_t *new_table = initialize_user_page_table();
    if (new_table == NULL) {
        free_exec_args(args, argc);
        k_close(entry);
        return INVALID_ARGS;
    }

    uint64_t kernel_stack_base = PROC_KERNEL_STACK_TOP - PROC_KERNEL_STACK_SIZE;
    for (uint64_t va = kernel_stack_base; va < PROC_KERNEL_STACK_TOP;
         va += PAGE_SIZE) {
        if (pt_seed_kernel_page(new_table, va) == NULL) {
            free_exec_args(args, argc);
            destroy_page_table(new_table);
            k_close(entry);
            return INVALID_ARGS;
        }
    }

    uint64_t user_sp = USER_STACK_TOP;
    uint64_t user_argv = 0;
    err = setup_exec_stack(new_table, args, argc, &user_sp, &user_argv);
    if (err != SUCCESS) {
        free_exec_args(args, argc);
        destroy_page_table(new_table);
        k_close(entry);
        return err;
    }

    for (uint16_t i = 0; i < header.e_phnum; i++) {
        elf64_phdr_t phdr;
        uint64_t ph_offset = header.e_phoff + ((uint64_t)i *
                                               header.e_phentsize);
        err = read_exact_at(fd, entry, ph_offset, &phdr, sizeof(phdr));
        if (err != SUCCESS) {
            free_exec_args(args, argc);
            destroy_page_table(new_table);
            k_close(entry);
            return err;
        }

        if (phdr.p_type != ELF_PH_TYPE_LOAD) {
            continue;
        }

        if (phdr.p_filesz > phdr.p_memsz ||
            phdr.p_offset > (uint64_t)file_size ||
            phdr.p_filesz > (uint64_t)file_size - phdr.p_offset) {
            free_exec_args(args, argc);
            destroy_page_table(new_table);
            k_close(entry);
            return INVALID_ARGS;
        }

        err = elf_load_user_segment(new_table, entry->ino_id, phdr.p_offset,
                                    phdr.p_filesz, phdr.p_vaddr,
                                    phdr.p_paddr, phdr.p_memsz,
                                    phdr.p_flags);
        if (err != SUCCESS) {
            free_exec_args(args, argc);
            destroy_page_table(new_table);
            k_close(entry);
            return err;
        }

        err = k_lseek(fd, (int)(phdr.p_offset + phdr.p_filesz), F_SEEK_SET);
        if (err < 0) {
            free_exec_args(args, argc);
            destroy_page_table(new_table);
            k_close(entry);
            return err;
        }
    }

    err = k_close(entry);
    if (err != SUCCESS) {
        free_exec_args(args, argc);
        destroy_page_table(new_table);
        return err;
    }

    uint64_t *old_table = (uint64_t *)(uintptr_t)pcb->ttbr0_el1_va;
    uint64_t frame_page_va = frame_va & ~((uint64_t)PAGE_OFFSET_MASK);
    uint64_t frame_offset = frame_va - frame_page_va;
    if (frame_va < kernel_stack_base || frame_va >= PROC_KERNEL_STACK_TOP ||
        frame_offset >= PAGE_SIZE) {
        free_exec_args(args, argc);
        destroy_page_table(new_table);
        return INVALID_ARGS;
    }

    for (uint64_t va = kernel_stack_base; va < PROC_KERNEL_STACK_TOP;
         va += PAGE_SIZE) {
        void *old_kernel_stack_page = pt_get_mapped_page(old_table, va);
        void *new_kernel_stack_page = pt_get_mapped_page(new_table, va);
        if (old_kernel_stack_page == NULL || new_kernel_stack_page == NULL) {
            free_exec_args(args, argc);
            destroy_page_table(new_table);
            return INVALID_ARGS;
        }

        copy_bytes(new_kernel_stack_page, old_kernel_stack_page, PAGE_SIZE);
    }

    uint8_t *new_frame_page = pt_get_mapped_page(new_table, frame_page_va);
    if (new_frame_page == NULL) {
        free_exec_args(args, argc);
        destroy_page_table(new_table);
        return INVALID_ARGS;
    }

    struct trap_frame *new_frame =
        (struct trap_frame *)(uintptr_t)(new_frame_page + frame_offset);
    uint64_t new_frame_va = frame_page_va + frame_offset;

    new_frame->elr = entry_pc;
    new_frame->sp = user_sp;
    new_frame->esr = 0;
    new_frame->far = 0;
    new_frame->type = 0;
    new_frame->intid = 0;
    for (unsigned i = 0; i < 31; i++) {
        new_frame->regs[i] = 0;
    }
    new_frame->regs[0] = (uint64_t)argc;
    new_frame->regs[1] = user_argv;

    tcb_t *tcb = get_curr_thread();
    tcb->ctx.x19 = new_frame_va;
    tcb->ctx.sp = new_frame_va;
    tcb->ctx.ttbr0_el1 = kernel_phys_addr((uint64_t)(uintptr_t)new_table);
    tcb->ctx.ttbr0_el1_va = (uint64_t)(uintptr_t)new_table;
    pcb->ttbr0_el1 = kernel_phys_addr((uint64_t)(uintptr_t)new_table);
    pcb->ttbr0_el1_va = (uint64_t)(uintptr_t)new_table;

    if (install_table) {
        install_ttbr0(pcb->ttbr0_el1);
    }
    if (next_frame != NULL) {
        *next_frame = (struct trap_frame *)(uintptr_t)new_frame_va;
    }
    destroy_page_table(old_table);
    set_process_name_for_pid(pcb->pid, exec_name);
    reset_exec_signal_dispositions(pcb);
    free_exec_args(args, argc);

    return SUCCESS;
}
