#include "disk.h"

#include "devfs.h"
#include "disk/block.h"
#include "dirs.h"
#include "fs_test.h"
#include "inodes.h"
#include "fs/caches/inode_cache.h"
#include "kapi.h"
#include "procfs.h"
#include "scheduler.h"
#include "sync/spinlock.h"
#include "string.h"
#include "user_bins.h"

#define FS_MOUNT_BLOCK_BUFFER_SIZE 4096

static int is_mounted = 0;
static int bytes_per_block = 0;
static int num_table_blocks = 0;
static uint64_t fs_base_block = 0;
static uint64_t fs_block_count = 0;
static uint32_t total_fs_blocks = 0;
static uint32_t block_bitmap_start = 1;
static uint32_t block_bitmap_blocks = 1;
static uint32_t inode_bitmap_start = 2;
static uint32_t inode_bitmap_blocks = 1;
static uint32_t inode_table_start = 3;
static uint32_t data_start_block = 4;
static unsigned char mount_block_buffer[FS_MOUNT_BLOCK_BUFFER_SIZE] __attribute__((aligned(16)));
static spinlock_t fs_block_cache_lock = SPINLOCK_INIT;
static spinlock_t fs_bitmap_lock = SPINLOCK_INIT;

typedef struct fs_seed_file_st {
    const char *path;
    const char *contents;
    uint8_t executable;
} fs_seed_file_t;

static const fs_seed_file_t test_scripts[] = {
    {
        "/tests/README.sh",
        "#!/bin/sh\n"
        "# OS-PI smoke test index. Run one script at a time from /tests.\n"
        "echo OS-PI test scripts\n"
        "echo Each script prints what it is about to do and what success looks like\n"
        "sleep 1000\n"
        "echo Run /tests/all.sh for a full smoke pass\n"
        "echo Run /tests/multicore.sh to show four CPU-bound jobs sharing CPUs\n"
        "echo Run /tests/stress.sh to leave cpubusy jobs running until killed\n"
        "echo Run /tests/vfs.sh to exercise file directory link and procfs paths\n"
        "echo Run /tests/vm.sh to exercise user heap and process VM maps\n"
        "sleep 1000\n"
        "echo Available tests are:\n"
        "ls /tests\n",
        1,
    },
    {
        "/tests/all.sh",
        "#!/bin/sh\n"
        "# Runs the main scripted smoke tests.\n"
        "echo BEGIN all-tests\n"
        "echo This runs proc vfs vm and multicore tests in order\n"
        "echo Success means each section reaches its END marker without fatal exceptions\n"
        "sleep 1500\n"
        "/tests/proc.sh\n"
        "sleep 1500\n"
        "/tests/vfs.sh\n"
        "sleep 1500\n"
        "/tests/vm.sh\n"
        "sleep 1500\n"
        "/tests/multicore.sh\n"
        "sleep 1500\n"
        "echo END all-tests\n",
        1,
    },
    {
        "/tests/proc.sh",
        "#!/bin/sh\n"
        "# Shows kernel state exported by procfs.\n"
        "echo BEGIN procfs-test\n"
        "echo Step 1 list /proc\n"
        "echo Success looks like entries such as cpuinfo processes meminfo timers interrupts\n"
        "sleep 1000\n"
        "ls /proc\n"
        "sleep 1500\n"
        "echo Step 2 print /proc/cpuinfo\n"
        "echo Success looks like online_processors and per_cpu rows for each CPU\n"
        "sleep 1000\n"
        "cat /proc/cpuinfo\n"
        "sleep 1500\n"
        "echo Step 3 print process table\n"
        "echo Success looks like init shell and this script runner in the table\n"
        "sleep 1000\n"
        "cat /proc/processes\n"
        "sleep 1500\n"
        "echo Step 4 print timer and interrupt state\n"
        "echo Success looks like readable counters without errors\n"
        "sleep 1000\n"
        "cat /proc/timers\n"
        "sleep 1000\n"
        "cat /proc/interrupts\n"
        "sleep 1000\n"
        "echo END procfs-test\n",
        1,
    },
    {
        "/tests/vfs.sh",
        "#!/bin/sh\n"
        "# Exercises VFS, regular files, stat, copy, rename, hard links, and symlinks.\n"
        "echo BEGIN vfs-test\n"
        "echo Step 1 create test directories under /tmp\n"
        "echo It is OK if mkdir says the directory already exists from an earlier run\n"
        "sleep 1000\n"
        "mkdir /tmp\n"
        "mkdir /tmp/vfs-test\n"
        "sleep 1000\n"
        "echo Step 2 create a regular file through shell redirection\n"
        "echo Success looks like cat printing alpha beta gamma\n"
        "sleep 1000\n"
        "echo alpha beta gamma > /tmp/vfs-test/input.txt\n"
        "cat /tmp/vfs-test/input.txt\n"
        "sleep 1500\n"
        "echo Step 3 count and search the file\n"
        "echo Success looks like wc counts and grep printing the same line\n"
        "sleep 1000\n"
        "wc /tmp/vfs-test/input.txt\n"
        "grep beta /tmp/vfs-test/input.txt\n"
        "sleep 1500\n"
        "echo Step 4 copy rename hardlink and symlink the file\n"
        "sleep 1000\n"
        "cp /tmp/vfs-test/input.txt /tmp/vfs-test/copy.txt\n"
        "mv /tmp/vfs-test/copy.txt /tmp/vfs-test/moved.txt\n"
        "ln /tmp/vfs-test/input.txt /tmp/vfs-test/hardlink.txt\n"
        "ln -s /tmp/vfs-test/input.txt /tmp/vfs-test/symlink.txt\n"
        "sleep 1500\n"
        "echo Step 5 inspect link and inode metadata\n"
        "echo Success looks like readlink target and stat output for both files\n"
        "sleep 1000\n"
        "readlink /tmp/vfs-test/symlink.txt\n"
        "stat /tmp/vfs-test/input.txt\n"
        "stat /tmp/vfs-test/hardlink.txt\n"
        "sleep 1500\n"
        "echo Step 6 list directory and verify moved copy contents\n"
        "sleep 1000\n"
        "ls /tmp/vfs-test\n"
        "cat /tmp/vfs-test/moved.txt\n"
        "sleep 1500\n"
        "echo Step 7 remove the moved copy and list again\n"
        "echo Success looks like moved.txt is gone while input and links remain\n"
        "sleep 1000\n"
        "rm /tmp/vfs-test/moved.txt\n"
        "ls /tmp/vfs-test\n"
        "sleep 1000\n"
        "echo END vfs-test\n",
        1,
    },
    {
        "/tests/vm.sh",
        "#!/bin/sh\n"
        "# Exercises process VM visibility, heap growth, and scheduler process views.\n"
        "echo BEGIN vm-test\n"
        "echo Step 1 show user heap accounting before a workload\n"
        "echo Success looks like heap total used and free values\n"
        "sleep 1000\n"
        "free\n"
        "sleep 1500\n"
        "echo Step 2 run cpubusy for a short timed workload\n"
        "echo Success looks like cpubusy start and cpubusy done with checksum\n"
        "sleep 1000\n"
        "cpubusy 300 vm\n"
        "sleep 1500\n"
        "echo Step 3 print kernel memory and VM counters\n"
        "echo Success looks like readable meminfo and vmstat output\n"
        "sleep 1000\n"
        "cat /proc/meminfo\n"
        "sleep 1000\n"
        "cat /proc/vmstat\n"
        "sleep 1500\n"
        "echo Step 4 print init process maps and process table\n"
        "echo Success looks like virtual address ranges followed by ps output\n"
        "sleep 1000\n"
        "cat /proc/0/maps\n"
        "sleep 1000\n"
        "ps\n"
        "cat /proc/processes\n"
        "sleep 1000\n"
        "echo END vm-test\n",
        1,
    },
    {
        "/tests/multicore.sh",
        "#!/bin/sh\n"
        "# Starts one busy loop per expected CPU and displays CPU/process/timer state.\n"
        "echo BEGIN multicore-test\n"
        "echo Step 1 show CPU state before load\n"
        "echo Success looks like online_processors is 4 on QEMU -smp 4 or Pi 5\n"
        "sleep 1000\n"
        "cat /proc/cpuinfo\n"
        "sleep 1500\n"
        "echo Step 2 start four CPU-bound workers with staggered launches\n"
        "echo Success looks like four cpubusy start lines and no SDHCI errors\n"
        "sleep 1000\n"
        "echo Launching worker cpu0\n"
        "cpubusy 2000 cpu0 &\n"
        "sleep 1000\n"
        "echo Launching worker cpu1\n"
        "cpubusy 2000 cpu1 &\n"
        "sleep 1000\n"
        "echo Launching worker cpu2\n"
        "cpubusy 2000 cpu2 &\n"
        "sleep 1000\n"
        "echo Launching worker cpu3\n"
        "cpubusy 2000 cpu3 &\n"
        "sleep 1500\n"
        "echo Step 3 inspect process table while workers are active\n"
        "echo Success looks like several cpubusy processes running\n"
        "sleep 1000\n"
        "ps\n"
        "cat /proc/processes\n"
        "sleep 1500\n"
        "echo Step 4 inspect per-CPU scheduler ticks during load\n"
        "echo Success looks like scheduler_ticks values have increased on online CPUs\n"
        "sleep 1000\n"
        "cat /proc/cpuinfo\n"
        "sleep 1000\n"
        "cat /proc/timers\n"
        "echo Step 5 wait for timed workers to finish\n"
        "echo Success looks like cpubusy done lines then no fatal exceptions\n"
        "sleep 10000\n"
        "sleep 1000\n"
        "ps\n"
        "cat /proc/cpuinfo\n"
        "sleep 1000\n"
        "echo END multicore-test\n",
        1,
    },
    {
        "/tests/stress.sh",
        "#!/bin/sh\n"
        "# Leaves background burners running until killed. Use ps, then kill PIDs.\n"
        "echo BEGIN stress-test\n"
        "echo Step 1 show CPU state before infinite workers\n"
        "echo Success looks like all expected CPUs online\n"
        "sleep 1000\n"
        "cat /proc/cpuinfo\n"
        "sleep 1500\n"
        "echo Step 2 start four infinite CPU burners with staggered launches\n"
        "echo Success looks like four cpubusy start lines and a responsive shell afterward\n"
        "sleep 1000\n"
        "echo Launching stress0\n"
        "cpubusy 0 stress0 &\n"
        "sleep 1000\n"
        "echo Launching stress1\n"
        "cpubusy 0 stress1 &\n"
        "sleep 1000\n"
        "echo Launching stress2\n"
        "cpubusy 0 stress2 &\n"
        "sleep 1000\n"
        "echo Launching stress3\n"
        "cpubusy 0 stress3 &\n"
        "sleep 1500\n"
        "echo Step 3 print process and CPU state while burners continue running\n"
        "echo Success looks like cpubusy PIDs in ps and scheduler_ticks increasing\n"
        "sleep 1000\n"
        "ps\n"
        "cat /proc/cpuinfo\n"
        "sleep 1000\n"
        "echo Step 4 leave workers running for manual inspection\n"
        "echo Use kill PID to stop each cpubusy worker shown by ps\n"
        "echo Example kill 7\n"
        "echo END stress-test\n",
        1,
    },
};

static err_t mark_user_bin_executable(const char *path) {
    struct fs_dirent dirent;
    err_t err = get_dirent_by_path(path, &dirent, NULL, NULL);
    if (err != SUCCESS) {
        return err;
    }

    return update_inode_metadata(dirent.ino_id, INODE_EDIT_PERM, 0, 0x1, 0);
}

static err_t seed_write_file(const char *path, const char *contents, uint8_t executable) {
    if (path == NULL || contents == NULL) {
        return INVALID_ARGS;
    }

    if (k_check_if_exists(path)) {
        err_t err = k_unlink(path);
        if (err != SUCCESS) {
            return err;
        }
    }

    int fd = k_open(path, O_CREAT | O_WRONLY);
    if (fd < 0) {
        return fd;
    }

    struct oft_entry *entry;
    err_t err = get_oft_entry_by_fd(fd, &entry);
    if (err != SUCCESS) {
        return err;
    }

    size_t len = strlen(contents);
    size_t written = 0;
    while (written < len) {
        int chunk = k_write(entry, contents + written, len - written);
        if (chunk < 0) {
            k_close(entry);
            return chunk;
        }
        if (chunk == 0) {
            k_close(entry);
            return FILE_WRITE_ERROR;
        }
        written += (size_t)chunk;
    }

    err = k_close(entry);
    if (err != SUCCESS) {
        return err;
    }

    return executable ? mark_user_bin_executable(path) : SUCCESS;
}

static err_t seed_tests(void) {
    struct fs_dirent dir;
    err_t err = get_dirent_by_path("/tests", &dir, NULL, NULL);
    if (err == SUCCESS) {
        attributes_t metadata;
        err = get_inode_metadata(dir.ino_id, &metadata);
        if (err != SUCCESS) {
            return err;
        }
        if (metadata.type != DIRECTORY_TYPE) {
            err = k_unlink("/tests");
            if (err != SUCCESS) {
                return err;
            }
            err = FILE_NOT_CREATED;
        }
    }

    if (err == FILE_NOT_CREATED || err == FILE_NOT_FOUND) {
        err = k_make_directory("/tests");
    }
    if (err != SUCCESS) {
        return err;
    }

    for (size_t i = 0; i < sizeof(test_scripts) / sizeof(test_scripts[0]); i++) {
        err = seed_write_file(test_scripts[i].path, test_scripts[i].contents,
                              test_scripts[i].executable);
        if (err != SUCCESS) {
            return err;
        }
    }

    return SUCCESS;
}

static err_t seed_user_bin_file(const user_bin_t *bin) {
    if (bin == NULL || bin->path == NULL || bin->start == NULL ||
        bin->end < bin->start) {
        return INVALID_ARGS;
    }

    if (k_check_if_exists(bin->path)) {
        return mark_user_bin_executable(bin->path);
    }

    size_t size = (size_t)(bin->end - bin->start);
    int fd = k_open(bin->path, O_CREAT | O_WRONLY);
    if (fd < 0) {
        return fd;
    }

    struct oft_entry *entry;
    err_t err = get_oft_entry_by_fd(fd, &entry);
    if (err != SUCCESS) {
        return err;
    }

    size_t written = 0;
    while (written < size) {
        int chunk = k_write(entry, (const char *)(bin->start + written),
                            size - written);
        if (chunk < 0) {
            k_close(entry);
            return chunk;
        }
        if (chunk == 0) {
            k_close(entry);
            return FILE_WRITE_ERROR;
        }
        written += (size_t)chunk;
    }

    err = k_close(entry);
    if (err != SUCCESS) {
        return err;
    }

    return mark_user_bin_executable(bin->path);
}

static err_t seed_user_bins(void) {
    struct fs_dirent dir;
    err_t err = get_dirent_by_path("/bin", &dir, NULL, NULL);
    
    if (err == SUCCESS) {
        attributes_t metadata;
        err = get_inode_metadata(dir.ino_id, &metadata);
        if (err) {
            return err;
        }
        if (metadata.type != DIRECTORY_TYPE) {
            err = k_unlink("/bin");
            if (err) {
                return err;
            }
            err = FILE_NOT_CREATED;
        }
    }
    
    if (err == FILE_NOT_CREATED || err == FILE_NOT_FOUND) {
        err = k_make_directory("/bin");
    }
    if (err != SUCCESS) {
        return err;
    }

    for (size_t i = 0; i < user_bins_count; i++) {
        err = seed_user_bin_file(&user_bins[i]);
        if (err != SUCCESS) {
            return err;
        }
    }

    return SUCCESS;
}

static err_t seed_user_bins_for_mkfs(void) {
    is_mounted = 1;

    err_t err = initialize_oft();
    if (err == SUCCESS) {
        err = seed_user_bins();
    }
    if (err == SUCCESS) {
        err = seed_tests();
    }

    err_t cleanup_err = empty_oft();
    is_mounted = 0;

    err_t inode_err = unmount_inode();
    err_t cache_err = lru_cache_empty();

    if (err != SUCCESS) {
        return err;
    }
    if (cleanup_err != SUCCESS) {
        return cleanup_err;
    }
    if (inode_err != SUCCESS) {
        return inode_err;
    }
    return cache_err;
}

static uint32_t read_le32(const unsigned char *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint64_t read_le64(const unsigned char *bytes) {
    return (uint64_t)read_le32(bytes) |
           ((uint64_t)read_le32(bytes + 4) << 32);
}

static int bytes_equal(const unsigned char *lhs,
                       const unsigned char *rhs,
                       uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (lhs[i] != rhs[i]) {
            return 0;
        }
    }
    return 1;
}

static int signature_matches(const struct superblock_st *superblock) {
    for (int i = 0; i < FS_SIGNATURE_SIZE; i++) {
        if (superblock->signature[i] != FS_SIGNATURE[i]) {
            return 0;
        }
    }
    return 1;
}

static int range_is_valid(uint32_t start, uint32_t count, uint32_t total) {
    return count != 0 && start < total && count <= total - start;
}

static int mbr_type_is_boot_partition(unsigned char type) {
    return type == 0x01 || type == 0x04 || type == 0x06 ||
           type == 0x0B || type == 0x0C || type == 0x0E ||
           type == 0xEF;
}

static err_t set_fs_region_after_partition(uint64_t partition_start,
                                           uint64_t partition_blocks,
                                           uint64_t device_blocks) {
    uint64_t fs_base = partition_start + partition_blocks;
    if (partition_blocks == 0 || fs_base <= partition_start ||
        device_blocks == 0 || fs_base >= device_blocks) {
        return INVALID_ARGS;
    }

    return fs_set_block_region(fs_base, device_blocks - fs_base);
}

static int gpt_guid_is_unused(const unsigned char *guid) {
    for (int i = 0; i < 16; i++) {
        if (guid[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static int gpt_guid_rank(const unsigned char *guid) {
    static const unsigned char efi_system_guid[16] = {
        0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
        0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
    };
    static const unsigned char microsoft_basic_guid[16] = {
        0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
        0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
    };

    if (bytes_equal(guid, efi_system_guid, sizeof(efi_system_guid))) {
        return 3;
    }
    if (bytes_equal(guid, microsoft_basic_guid, sizeof(microsoft_basic_guid))) {
        return 2;
    }
    if (!gpt_guid_is_unused(guid)) {
        return 1;
    }
    return 0;
}

static err_t configure_gpt_fs_region(uint64_t device_blocks,
                                     uint32_t block_size) {
    unsigned char data[FS_MOUNT_BLOCK_BUFFER_SIZE] __attribute__((aligned(16)));
    if (block_size == 0 || block_size > FS_MOUNT_BLOCK_BUFFER_SIZE) {
        return INVALID_ARGS;
    }

    if (block_read(1, 1, data) != 0) {
        return FILE_READ_ERROR;
    }

    static const unsigned char gpt_signature[8] = {
        'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'
    };
    if (!bytes_equal(data, gpt_signature, sizeof(gpt_signature))) {
        return INVALID_ARGS;
    }

    uint64_t entries_lba = read_le64(&data[72]);
    uint32_t entry_count = read_le32(&data[80]);
    uint32_t entry_size = read_le32(&data[84]);
    if (entries_lba == 0 || entry_count == 0 ||
        entry_size < 128 || entry_size > block_size ||
        block_size % entry_size != 0) {
        return INVALID_ARGS;
    }

    uint32_t entries_per_block = block_size / entry_size;
    uint64_t loaded_block = UINT64_MAX;
    int best_rank = 0;
    uint64_t best_start = 0;
    uint64_t best_blocks = 0;

    for (uint32_t i = 0; i < entry_count; i++) {
        uint64_t entry_block = entries_lba + (i / entries_per_block);
        if (entry_block >= device_blocks) {
            break;
        }
        if (entry_block != loaded_block) {
            if (block_read(entry_block, 1, data) != 0) {
                return FILE_READ_ERROR;
            }
            loaded_block = entry_block;
        }

        const unsigned char *entry =
            &data[(i % entries_per_block) * entry_size];
        int rank = gpt_guid_rank(entry);
        if (rank == 0) {
            continue;
        }

        uint64_t first_lba = read_le64(&entry[32]);
        uint64_t last_lba = read_le64(&entry[40]);
        if (first_lba > last_lba || last_lba >= device_blocks) {
            continue;
        }

        if (rank > best_rank ||
            (rank == best_rank && first_lba < best_start)) {
            best_rank = rank;
            best_start = first_lba;
            best_blocks = last_lba - first_lba + 1;
        }
    }

    if (best_rank == 0) {
        return INVALID_ARGS;
    }

    return set_fs_region_after_partition(best_start, best_blocks,
                                         device_blocks);
}

static err_t configure_default_fs_region(void) {
    unsigned char mbr[FS_MOUNT_BLOCK_BUFFER_SIZE] __attribute__((aligned(16)));
    uint64_t device_blocks = block_get_count();
    uint32_t block_size = block_get_size();
    if (device_blocks == 0 || block_size == 0 ||
        block_size > FS_MOUNT_BLOCK_BUFFER_SIZE) {
        return INVALID_ARGS;
    }

#if defined(PLATFORM_QEMU)
    return fs_set_block_region(0, device_blocks);
#endif

    if (block_read(0, 1, mbr) != 0) {
        return FILE_READ_ERROR;
    }
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        return INVALID_ARGS;
    }

    const unsigned char *fallback_partition = NULL;
    for (int i = 0; i < 4; i++) {
        const unsigned char *partition = &mbr[446 + i * 16];
        unsigned char type = partition[4];
        uint32_t partition_start = read_le32(&partition[8]);
        uint32_t partition_blocks = read_le32(&partition[12]);
        if (partition_blocks == 0) {
            continue;
        }

        if (type == 0xEE) {
            return configure_gpt_fs_region(device_blocks, block_size);
        }

        if (fallback_partition == NULL) {
            fallback_partition = partition;
        }

        if (mbr_type_is_boot_partition(type)) {
            return set_fs_region_after_partition(partition_start,
                                                 partition_blocks,
                                                 device_blocks);
        }
    }

    if (fallback_partition != NULL) {
        return set_fs_region_after_partition(read_le32(&fallback_partition[8]),
                                             read_le32(&fallback_partition[12]),
                                             device_blocks);
    }

    return INVALID_ARGS;
}

static err_t validate_superblock(const struct superblock_st *superblock) {
    if (!signature_matches(superblock)) {
        return FS_INVALID;
    }

    if (superblock->bytes_per_block != block_get_size() ||
        superblock->total_blocks == 0 ||
        superblock->total_blocks != fs_get_block_count() ||
        superblock->root_inode_id != ROOT_INO) {
        return FS_INVALID;
    }

    if (superblock->block_bitmap_start != 1 ||
        !range_is_valid(superblock->block_bitmap_start,
                        superblock->block_bitmap_blocks,
                        superblock->total_blocks) ||
        !range_is_valid(superblock->inode_bitmap_start,
                        superblock->inode_bitmap_blocks,
                        superblock->total_blocks) ||
        !range_is_valid(superblock->inode_table_start,
                        superblock->inode_table_blocks,
                        superblock->total_blocks) ||
        superblock->inode_bitmap_start <
            superblock->block_bitmap_start + superblock->block_bitmap_blocks ||
        superblock->inode_table_start <
            superblock->inode_bitmap_start + superblock->inode_bitmap_blocks ||
        superblock->data_start_block <
            superblock->inode_table_start + superblock->inode_table_blocks ||
        superblock->data_start_block >= superblock->total_blocks) {
        return FS_INVALID;
    }

    return SUCCESS;
}

static err_t validate_root_directory(void) {
    struct inode_st root_inode;
    err_t err = get_inode_raw(&root_inode, ROOT_INO);
    if (err != SUCCESS) {
        return FS_INVALID;
    }

    uint32_t total_blocks = get_total_fs_blocks();
    if (root_inode.metadata.i_blocks == 0 ||
        root_inode.metadata.i_blocks > 12 ||
        root_inode.blocks[0] != get_data_start_block() ||
        root_inode.blocks[0] >= total_blocks) {
        return FS_INVALID;
    }

    struct fs_dirent *dir = kmalloc(get_bytes_per_block());
    if (dir == NULL) {
        return FILE_READ_ERROR;
    }

    int dirents_per_block = get_bytes_per_block() / (int)sizeof(struct fs_dirent);
    int found_end = 0;
    for (uint32_t block_idx = 0;
         block_idx < root_inode.metadata.i_blocks;
         block_idx++) {
        block_no_t block = root_inode.blocks[block_idx];
        if (block == 0 || block >= total_blocks) {
            kfree(dir);
            return FS_INVALID;
        }

        err = read_block(dir, block);
        if (err != SUCCESS) {
            kfree(dir);
            return FS_INVALID;
        }

        int start_idx = 0;
        if (block_idx == 0) {
            attributes_t first_metadata;
            attributes_t second_metadata;
            err = get_inode_metadata(dir[0].ino_id, &first_metadata);
            if (err != SUCCESS) {
                kfree(dir);
                return err;
            }
            err = get_inode_metadata(dir[1].ino_id, &second_metadata);
            if (err != SUCCESS) {
                kfree(dir);
                return err;
            }
            if (strcmp(dir[0].name, ".") != 0 ||
                strcmp(dir[1].name, "..") != 0 ||
                dir[0].ino_id != ROOT_INO ||
                dir[1].ino_id != ROOT_INO ||
                first_metadata.type != DIRECTORY_TYPE ||
                second_metadata.type != DIRECTORY_TYPE ) {
                kfree(dir);
                return FS_INVALID;
            }
            start_idx = 2;
        }

        for (int i = start_idx; i < dirents_per_block; i++) {
            if (strcmp(dir[i].name, "\0") == 0) {
                found_end = 1;
                break;
            }
        }

        if (found_end) {
            break;
        }
    }

    kfree(dir);
    return found_end ? SUCCESS : FS_INVALID;
}

err_t fs_set_block_region(uint64_t base_block, uint64_t block_count) {
    if (block_count == 0) {
        return INVALID_ARGS;
    }

    if (base_block + block_count < base_block) {
        return INVALID_ARGS;
    }

    fs_base_block = base_block;
    fs_block_count = block_count;
    return SUCCESS;
}

uint64_t fs_get_base_block(void) {
    return fs_base_block;
}

uint64_t fs_get_block_count(void) {
    return fs_block_count;
}

err_t fs_set_layout(uint32_t bytes_per_block_value, uint32_t total_blocks,
                    uint32_t new_block_bitmap_start, uint32_t new_block_bitmap_blocks,
                    uint32_t new_inode_bitmap_start, uint32_t new_inode_bitmap_blocks,
                    uint32_t new_inode_table_start, uint32_t inode_table_blocks,
                    uint32_t new_data_start_block) {
    if (bytes_per_block_value == 0 || total_blocks == 0 ||
        new_block_bitmap_blocks == 0 || new_inode_bitmap_blocks == 0 ||
        inode_table_blocks == 0 || new_data_start_block >= total_blocks) {
        return INVALID_ARGS;
    }

    bytes_per_block = (int)bytes_per_block_value;
    total_fs_blocks = total_blocks;
    block_bitmap_start = new_block_bitmap_start;
    block_bitmap_blocks = new_block_bitmap_blocks;
    inode_bitmap_start = new_inode_bitmap_start;
    inode_bitmap_blocks = new_inode_bitmap_blocks;
    inode_table_start = new_inode_table_start;
    num_table_blocks = (int)inode_table_blocks;
    data_start_block = new_data_start_block;
    return SUCCESS;
}

err_t mkfs(int inode_table_blocks, int block_size_config) {
    if (inode_table_blocks <= 0) {
        return INVALID_ARGS;
    }

    int new_bytes_per_block;

    // Get bytes per block
    if (block_size_config == 0) {
        new_bytes_per_block = 256;
    } else if (block_size_config == 1) {
        new_bytes_per_block = 512;
    } else if (block_size_config == 2) {
        new_bytes_per_block = 1024;
    } else if (block_size_config == 3) {
        new_bytes_per_block = 2048;
    } else if (block_size_config == 4) {
        new_bytes_per_block = 4096;
    } else {
        return INVALID_ARGS;
    }

    if ((uint32_t)new_bytes_per_block != block_get_size()) {
        return INVALID_ARGS;
    }

    err_t err = configure_default_fs_region();
    if (err != SUCCESS) {
        return err;
    }

    uint64_t usable_blocks = fs_get_block_count();
    if (usable_blocks > UINT32_MAX) {
        return INVALID_ARGS;
    }

    err = lru_cache_empty();
    if (err != SUCCESS) {
        return err;
    }

    uint32_t total_blocks = (uint32_t)usable_blocks;
    uint32_t bits_per_block = (uint32_t)new_bytes_per_block * 8u;
    uint32_t new_block_bitmap_blocks = (total_blocks + bits_per_block - 1u) / bits_per_block;
    uint32_t inodes_per_block = (uint32_t)(new_bytes_per_block / sizeof(struct inode_st));
    uint64_t inode_count = (uint64_t)(uint32_t)inode_table_blocks * inodes_per_block;
    if (inode_count == 0 || inode_count > UINT32_MAX) {
        return INVALID_ARGS;
    }

    uint32_t new_inode_bitmap_blocks =
        ((uint32_t)inode_count + bits_per_block - 1u) / bits_per_block;
    uint32_t new_block_bitmap_start = 1;
    uint32_t new_inode_bitmap_start = new_block_bitmap_start + new_block_bitmap_blocks;
    uint32_t new_inode_table_start = new_inode_bitmap_start + new_inode_bitmap_blocks;
    uint32_t new_data_start_block = new_inode_table_start + (uint32_t)inode_table_blocks;
    if (new_data_start_block >= total_blocks) {
        return INVALID_ARGS;
    }

    err = fs_set_layout((uint32_t)new_bytes_per_block, total_blocks,
                        new_block_bitmap_start, new_block_bitmap_blocks,
                        new_inode_bitmap_start, new_inode_bitmap_blocks,
                        new_inode_table_start, (uint32_t)inode_table_blocks,
                        new_data_start_block);
    if (err != SUCCESS) {
        return err;
    }

    err = mkfs_inode(inode_table_blocks, new_bytes_per_block);
    if (err != SUCCESS) {
        return err;
    }

    err = seed_user_bins_for_mkfs();
    if (err != SUCCESS) {
        return err;
    }

    return lru_cache_empty();
}

err_t mount(void) {
    if (is_mounted) {
        return FS_ALREADY_MOUNTED;
    }

    uint32_t driver_block_size = block_get_size();
    if (driver_block_size == 0 || driver_block_size > FS_MOUNT_BLOCK_BUFFER_SIZE) {
        return INVALID_ARGS;
    }

    err_t err_code = configure_default_fs_region();
    if (err_code != SUCCESS) {
        return err_code;
    }

    if (block_read(fs_get_base_block(), 1, mount_block_buffer) != 0) {
        return FILE_READ_ERROR;
    }

    const struct superblock_st *superblock =
        (const struct superblock_st *)mount_block_buffer;
    err_code = validate_superblock(superblock);
    if (err_code != SUCCESS) {
        return err_code;
    }

    err_code = mount_inode(superblock, &bytes_per_block, &num_table_blocks);
    if (err_code != SUCCESS) {
        return err_code;
    }

    err_code = validate_root_directory();
    if (err_code != SUCCESS) {
        lru_cache_empty();
        return err_code;
    }

    is_mounted = 1;
    err_code = initialize_oft();
    if (err_code != SUCCESS) {
        is_mounted = 0;
        unmount_inode();
        return err_code;
    }

    err_code = seed_user_bins();
    if (err_code != SUCCESS) {
        is_mounted = 0;
        empty_oft();
        unmount_inode();
        return err_code;
    }

    err_code = procfs_init();
    if (err_code != SUCCESS) {
        is_mounted = 0;
        empty_oft();
        unmount_inode();
        return err_code;
    }

    err_code = devfs_init();
    if (err_code != SUCCESS) {
        is_mounted = 0;
        empty_oft();
        unmount_inode();
        return err_code;
    }

    return SUCCESS;
}

err_t unmount() {
    if (!is_mounted) {
        return FS_NOT_MOUNTED;
    }

    err_t err_code = lru_cache_empty();
    if (err_code) {
        return err_code;
    }
    
    err_code = empty_oft();
    if (err_code != SUCCESS) {
        return err_code;
    }

    err_code = unmount_inode();
    if (err_code != SUCCESS) {
        return err_code;
    }

    is_mounted = 0;

    return SUCCESS;
}

err_t read_block(void *data, block_no_t num) {
    unsigned char *bytes = (unsigned char*) data;
    struct node_st *block_node;
    uint64_t flags = spin_lock_irqsave(&fs_block_cache_lock);
    err_t err_code = lru_cache_add_to_front(&block_node, num);
    if (err_code) {
        spin_unlock_irqrestore(&fs_block_cache_lock, flags);
        return err_code;
    }

    memcpy(bytes, block_node->data, get_bytes_per_block());
    spin_unlock_irqrestore(&fs_block_cache_lock, flags);

    return SUCCESS;
}

err_t write_block(void *data, block_no_t num) {
    uint64_t flags = spin_lock_irqsave(&fs_block_cache_lock);
    err_t err = lru_cache_update_data(data, num);
    if (err) {
        spin_unlock_irqrestore(&fs_block_cache_lock, flags);
        return err;
    }
    spin_unlock_irqrestore(&fs_block_cache_lock, flags);
    return SUCCESS;
}

// Returns first 0 in bitmap of block at block_with_bitmap
err_t find_free_from_bitmap(uint32_t *free_block, block_no_t block_with_bitmap, int update_taken) {
    return find_free_from_bitmap_range(free_block, block_with_bitmap, 1,
                                       (uint32_t)get_bytes_per_block() * 8u,
                                       update_taken);
}

err_t set_bit_in_bitmap_range(block_no_t bitmap_start, uint32_t bitmap_blocks,
                              uint32_t valid_bits, uint32_t bit_idx,
                              int set_bit) {
    if (bitmap_blocks == 0 || bit_idx >= valid_bits) {
        return INVALID_ARGS;
    }

    uint32_t bits_per_block = (uint32_t)get_bytes_per_block() * 8u;
    uint32_t bitmap_block_idx = bit_idx / bits_per_block;
    if (bitmap_block_idx >= bitmap_blocks) {
        return INVALID_ARGS;
    }

    uint64_t flags = spin_lock_irqsave(&fs_bitmap_lock);
    unsigned char *data = kmalloc(get_bytes_per_block());
    if (data == NULL) {
        spin_unlock_irqrestore(&fs_bitmap_lock, flags);
        return FILE_READ_ERROR;
    }

    err_t err = read_block(data, bitmap_start + bitmap_block_idx);
    if (err != SUCCESS) {
        kfree(data);
        spin_unlock_irqrestore(&fs_bitmap_lock, flags);
        return err;
    }

    uint32_t bit_in_block = bit_idx % bits_per_block;
    unsigned char mask = (unsigned char)(1u << (7u - (bit_in_block % 8u)));
    if (set_bit) {
        data[bit_in_block / 8u] |= mask;
    } else {
        data[bit_in_block / 8u] &= (unsigned char)~mask;
    }

    err = write_block(data, bitmap_start + bitmap_block_idx);
    kfree(data);
    spin_unlock_irqrestore(&fs_bitmap_lock, flags);
    return err;
}

err_t find_free_from_bitmap_range(uint32_t *free_block, block_no_t bitmap_start,
                                  uint32_t bitmap_blocks, uint32_t valid_bits,
                                  int update_taken) {
    if (free_block == NULL || bitmap_blocks == 0 || valid_bits == 0) {
        return INVALID_ARGS;
    }

    uint64_t flags = spin_lock_irqsave(&fs_bitmap_lock);
    void *data = kmalloc(get_bytes_per_block());
    if (data == NULL) {
        spin_unlock_irqrestore(&fs_bitmap_lock, flags);
        return FILE_READ_ERROR;
    }

    uint32_t bits_per_block = (uint32_t)get_bytes_per_block() * 8u;
    for (uint32_t bitmap_block_idx = 0; bitmap_block_idx < bitmap_blocks; bitmap_block_idx++) {
        err_t err_read = read_block(data, bitmap_start + bitmap_block_idx);
        if (err_read != SUCCESS) {
            kfree(data);
            spin_unlock_irqrestore(&fs_bitmap_lock, flags);
            return err_read;
        }

        unsigned char *bitmap = (unsigned char *)data;
        uint32_t first_bit = bitmap_block_idx * bits_per_block;
        uint32_t valid_bits_in_block =
            valid_bits > first_bit ? valid_bits - first_bit : 0;
        if (valid_bits_in_block > bits_per_block) {
            valid_bits_in_block = bits_per_block;
        }

        uint32_t valid_bytes = (valid_bits_in_block + 7u) / 8u;
        for (uint32_t index_byte = 0; index_byte < valid_bytes; index_byte++) {
            if (bitmap[index_byte] == 0xFF) {
                continue;
            }

            for (uint32_t index = 0; index < 8u; index++) {
                uint32_t bit_in_block = index_byte * 8u + index;
                if (bit_in_block >= valid_bits_in_block) {
                    break;
                }

                unsigned char mask = (unsigned char)(1u << (7u - index));
                if ((bitmap[index_byte] & mask) == 0) {
                    if (update_taken) {
                        bitmap[index_byte] |= mask;
                        err_t err_code = write_block(data, bitmap_start + bitmap_block_idx);
                        if (err_code != SUCCESS) {
                            kfree(data);
                            spin_unlock_irqrestore(&fs_bitmap_lock, flags);
                            return err_code;
                        }
                    }
                    *free_block = first_bit + bit_in_block;
                    kfree(data);
                    spin_unlock_irqrestore(&fs_bitmap_lock, flags);
                    return SUCCESS;
                }
            }
        }
    }

    kfree(data);
    spin_unlock_irqrestore(&fs_bitmap_lock, flags);
    return NO_FREE_BLOCKS;
}

block_no_t get_ith_block_of_file_by_id(ino_id_t id, unsigned int block_num) {
    return get_block_num_from_inode_with_id(id, block_num);
}

block_no_t get_ith_block_of_file(struct oft_entry *entry, unsigned int block_num) {
    uint64_t flags = cached_inode_lock(entry->inode);
    struct inode_st snapshot = entry->inode->inode;
    cached_inode_unlock(entry->inode, flags);
    return get_block_num_from_inode(&snapshot, block_num);
}

block_no_t get_next_block_from_file(struct oft_entry *entry, int next_block_index) {
    return get_ith_block_of_file(entry, next_block_index);
}

err_t allocate_new_block_for_file(struct oft_entry *entry, block_no_t *block_num) {
    uint64_t flags = cached_inode_lock(entry->inode);
    entry->inode->dirty = 1;
    err_t err = allocate_block_for_file_inode(&entry->inode->inode, block_num);
    cached_inode_unlock(entry->inode, flags);
    return err;
}

err_t allocate_new_block_for_file_from_id(ino_id_t id_in_fs, block_no_t* allocated_block) {
    block_no_t new_block;
    err_t err;
    if ((err = allocate_block_for_file_inode_from_id(id_in_fs, &new_block)) != SUCCESS) {
        return err;
    }
    *allocated_block = new_block;
    return SUCCESS;
}

err_t free_file(const char* f_name) {
    struct fs_dirent dirent;
    char *actual_name;
    ino_id_t parent_dir;
    err_t error = get_dirent_by_path(f_name, &dirent, &parent_dir, &actual_name);
    if (error == FILE_NOT_FOUND || error == FILE_NOT_CREATED) {
        return FILE_NOT_FOUND;
    } else if (error) {
        return error;
    }

    attributes_t metadata;
    error = get_inode_metadata(dirent.ino_id, &metadata);
    if (error != SUCCESS) {
        kfree(actual_name);
        return error;
    }
    if (metadata.type == DIRECTORY_TYPE) {
        kfree(actual_name);
        return IS_A_DIRECTORY;
    }

    if (dirent.ino_id != 0) {

       error = free_file_inode(get_inode_from_cache(dirent.ino_id));
       if (error != SUCCESS) {
           kfree(actual_name);
           return error;
       }
    }
    remove_ref_from_cache(dirent.ino_id);

    error = remove_dirent_by_f_name(actual_name, parent_dir);
    kfree(actual_name);
    return error;
}

err_t clear_blocks_of_file(struct oft_entry *entry) {
    uint64_t flags = cached_inode_lock(entry->inode);
    entry->inode->dirty = 1;
    err_t err = clear_blocks_of_inode(&entry->inode->inode, 1);
    cached_inode_unlock(entry->inode, flags);
    return err;
}

err_t clear_blocks_of_file_by_id(ino_id_t id) {
    struct cached_inode_st *inode_cached = get_inode_from_cache(id);
    if (inode_cached == 0) {
        return -1;
    }

    uint64_t flags = cached_inode_lock(inode_cached);
    inode_cached->dirty = 1;
    int to_ret = clear_blocks_of_inode(&inode_cached->inode, 1);
    cached_inode_unlock(inode_cached, flags);
    err_t error = remove_ref_from_cache(id);
    if (error) return error;
    return to_ret;
}

int add_new_file(struct oft_entry **entry, int file_type, uint8_t perm, struct file_operations *fops) {
    ino_id_t ino_id;
    err_t error = add_new_file_inode(&ino_id, file_type, perm, fops);
    if (error != SUCCESS) {
        return error;
    }

    if (entry != NULL) {
        (*entry)->ino_id = ino_id;
        (*entry)->inode = get_inode_from_cache(ino_id);
    }

    return ino_id;
}

err_t add_new_file_with_id(block_no_t* new_block, int file_type, uint8_t perm, struct file_operations *fops) {
    return add_new_file_inode(new_block, file_type, perm, fops);
}

err_t remove_last_block(ino_id_t id_in_fs) {
    return remove_last_block_inode(id_in_fs);
}

block_no_t get_first_block(ino_id_t file_id) {
        return get_block_num_from_inode_with_id(file_id, 0);
}

int get_file_size(struct oft_entry *entry) {
    if (entry->inode == NULL) {
        return 0;
    }

    uint64_t flags = cached_inode_lock(entry->inode);
    int size = entry->inode->inode.metadata.i_size;
    cached_inode_unlock(entry->inode, flags);
    return size;
}

int get_file_size_by_id(ino_id_t ino_id) {
    struct cached_inode_st *inode_cached = get_inode_from_cache(ino_id);
    if (inode_cached == 0) {
        return -1;
    }

    uint64_t flags = cached_inode_lock(inode_cached);
    int to_ret = inode_cached->inode.metadata.i_size;
    cached_inode_unlock(inode_cached, flags);
    err_t error = remove_ref_from_cache(ino_id);
    if (error) return error;
    return to_ret;
}

int update_file_size(struct oft_entry *entry, int new_size) {
    if (entry == NULL || entry->inode == NULL) {
        return -1;
    }

    uint64_t flags = cached_inode_lock(entry->inode);
    entry->inode->dirty = 1;
    entry->inode->inode.metadata.i_size = new_size;
    cached_inode_unlock(entry->inode, flags);
    return SUCCESS;
}

void toggle_in_bitmap_data(unsigned char *data, unsigned int idx) {
    data[idx / 8] ^= (1 << (7 - (idx % 8)));
}

// Getters and Setters
int get_num_table_blocks() {
    return num_table_blocks;
}

int get_bytes_per_block() {
    return bytes_per_block;
}

uint32_t get_total_fs_blocks() {
    return total_fs_blocks;
}

uint32_t get_block_bitmap_start() {
    return block_bitmap_start;
}

uint32_t get_block_bitmap_blocks() {
    return block_bitmap_blocks;
}

uint32_t get_inode_bitmap_start() {
    return inode_bitmap_start;
}

uint32_t get_inode_bitmap_blocks() {
    return inode_bitmap_blocks;
}

uint32_t get_inode_table_start() {
    return inode_table_start;
}

uint32_t get_data_start_block() {
    return data_start_block;
}

int get_is_mounted() {
    return is_mounted;
}

ino_id_t get_curr_dir() {
    pcb_t *pcb = get_curr_process();
    if (pcb == NULL) {
        return 0;
    }
    uint64_t flags = spin_lock_irqsave(&pcb->lock);
    ino_id_t cwd = pcb->cwd;
    spin_unlock_irqrestore(&pcb->lock, flags);
    return cwd;
}

void set_curr_dir(ino_id_t id) {
    pcb_t *pcb = get_curr_process();
    if (pcb == NULL) {
        return;
    }
    uint64_t flags = spin_lock_irqsave(&pcb->lock);
    pcb->cwd = id;
    spin_unlock_irqrestore(&pcb->lock, flags);
}
