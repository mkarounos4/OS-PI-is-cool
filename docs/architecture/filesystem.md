# Filesystem Overview
- 
## List of Features

- [Proper Inode file system](#inodes)
- [Disk persistence with mount/unmount](#disk)
- [Directories with nested subdirectories](#directories)
- Symbolic links
- [Open-file table](#oppen-file-table)
- [Virtual filesystem layer, with `procfs` to demo](#virtual-filesystem)
- [Character devices](#character-devices)
- [Permission handling with chmod](#permission-handling)
- ELF-loading userspace binaries into `bin` (This section is documented in [processes.md](processes.md))
- [LRU Block cache and Inode cache](#inode-data-cache)
- [Kernel API layer](#kernel-api-layer)

## System Structure
```
+----------------------------+
|      CMDS + Syscall Layer  |
+----------------------------+
|       Kernel API layer     |
+----------------------------+
|          OFT Layer         |
+----------------------------+
|         Dirent Layer       |
+----------------------------+
|         Disk Layer         |
+----------------------------+
|        Inode Layer         |
+----------------------------+
|      Hardware Drivers      |
+----------------------------+
```

### CMDS + Syscall Layer
This is the topmost layer of the Filesystem. These are the functions in `fs/cmds.h` which are directly called by the system call wrappers. 
Most of these functions are wrappers for the KAPI level, and they take in per-process File Descriptors which are converted, using the current process's file descriptor table, into Kernel level file descriptors. These are then converted into Open File Table (OFT) Entries for the KAPI layer to use. 
Example functions here include `open`, `close`, `read`, `write`, etc.

### Kernel API Layer
This layer contains the main high-level filesystem logic. These take in file paths or OFT entries, and handle the actual core logic for the functions.
As this is still at a high level, most of these functions delegate work down to the `OFT` layer, `dirent` layer, and occasional `disk` layer depending on the desired functionality.
Example functions include `k_open`, `k_close`, `k_read`, `k_write`, etc.

### OFT Layer
This is the Kernel level Open File Table, which contains a list of OFT entries, which each contain a cached `inode` and open-file metadata such as cursor position and permissions. This is our internal implementation to mimic `FILE` structs in `Unix`.

### Dirent Layer
These are the directory entries stored directly on the SD disk inside of directories. These contain just file name and inode number, as most of the metadata is handled at the inode level. 

### Disk Layer
This primarily consists of wrappers for the `Inode` layer to allow for easy access to the raw inode disk operations without needing to worry about the specific inode implementation. This layer provides all disk operations such as `mount`, `unmount`, `get_ith_block_of_file`, etc.

### Inode Layer
This is our real `Inode` structure mimicking the linux `ext2` filesystem. This stores block pointers with singly, doubly, and triply indirect block pointers. This is where all the real file logic happens, and directly calls the hardware for writing and reading blocks to SD hardware for persistence.

### Hardware Drivers
This is the PIO drivers that expose `read_block` and `write_block` to `SD` card disk, which the `Inode` layer uses to write/read data to disk, allowing for data persistence across restarts.

# Detailed Architecture and Decisions

## Kernel API Layer

This layer contains the main high-level kernel logic that all other kernel operations will call. This is what delegates work to `dirent`, `oft`, and `disk` layers.
All functions here return an int type which corresponds to an error code from [errors.h](../../kernel/fs/errors.h), which is converted into a standard UNIX-style error code in a helper in [errors.c](../../kernel/fs/errors.c).

KAPI Specifications:
- `k_open(const char*fname, int mode)`: Opens a file given a path, and creates a new `OFT entry` for it, returning it's kernel fd. Takes in flags `O_TRUNC`, `O_CREAT`, `O_APPEND`, `O_RDONLY`, `O_WRONLY`, and `O_RDWR`, following UNIX conventions for each. If file already exists, verifies the permissions match the opening flag permissions, or else it throws `INVALID PERMISSIONS` error. Also verifies there are no invalid characters in the file name. After opening, calls the VFS level `fops->open` if exists.
- `k_close(struct oft_entry *entry)`: Calls the VFS level `fops->close` if exists, and then closes the `OFT entry`.
- `k_read(struct oft_entry *entry, char *buf, size_t n)`: Verifies read permissions, and then wraps around the VFS level `fops->read`.
- `k_write(struct oft_entry *entry, const char *buf, size_t n)`: Verifies write permissions, and then wraps around VFS level `fops->write`
- `k_update_file_time(const char *file_name)`: Updates file inode metadata time updated to curr time. Used for touch.
- `k_lseek(int fd, int offset, int whence)`: Updates cursor to specified value. `whence` accepts `F_SEEK_SET` to set to offset, `F_SEEK_CUR` to set to curr + offset, and `F_SEEK_END` to set to end + offset. If cursor set past the file size, allocates new blocks to fill that hole and writes 0s.
- `k_chmod(const char *file_name, uint8_t new_perms, int flag)`: Updates inode metadata for file permissions. `flag` accepts `0` for setting perms exactly, `1` for removing specified perms, and `2` for adding new perms ontop of existing.
- `k_mv_file(const char *src_path, const char *dest_path)`: Updates file name, and moves dirent if necessary. If file at `dest_path` already exists, removes it.
- `k_unlink(const char *fname)`: Removes one reference from the inode pointed to by that file. If refcount reaches 0, removes the inode.
- `k_ls(const char *filename, int out_fs)`: Lists files in directory at specified path to specified output file. If `filename` is `NULL`, uses current process's `cwd`.
- `k_check_if_exists(const char *f_name)`: Helper to check if the file at the specified path exists.
- `k_file_add_reference(int fd)`: Increments the refcount for the specified file by 1, used for symlinks.
- `k_make_directory(char *f_path)`: Created a new directory at path for `mkdir`.
- `k_change_directory(char *f_path)`: Update the current process's `cwd` to the specified path. If no leading `/`, searches for path starting from curr `cwd`.
- `k_check_if_executable(char *f_name)`: Helper function to check if the file has execution permissions.
- `k_stat(const char *path, struct fs_stat_st *stat)`: Gets current filesystem stats for `/proc` metadata printing.
- `k_exec` and `k_exec_process`. As the design of these is primarily process-focused, the explanation is included in [processes.md](processes.md).

This level also contains the default file operations for files and directories, which are documented in more detail in [Virtual Filesystem](#virtual-filesystem) section.

## Inodes

Our filesystem uses an Inode structure mimicking Linux's `ext2` filesystem. These inodes use a struct containing the following metadata
- `i_links_count`: refcount
- `type`: int representing file type, such as `file`, `directory`, `pipe`, etc.
- `perm`: int for permissions, with bit flag for `read`, `write`, and `execute`.
- `i_size`: storing file size in bytes.
- `i_blocks`: number of blocks in this file.
- `mtime`: storing time of last modification
- `i_rdev`: storing device major and minor number for char devices. Ignored if not device type.
- `fops`: struct containing file operation handlers such as `read`, `write`, `open`, `lookup`, etc. for VFS. More information in [Virtual Filesystem](#virtual-filesystem)
- `i_<type>`: Special Union field containing pointer to metadata for special file types. For ex., `pipe` type uses `i_pipe` with `pipe_st*` storing all pipie metadata.

### Blocks
Each inode stores a 15-length array for blocks like `ext2`. 
When there are 15 or less blocks, each index stores a direct block pointer, which is a `block_no_t` int storing the physical block number on disk this file's block data can be found.
When there are more than 15 blocks, the first 12 indexes contain direct block pointers. The 13th then stores a singly indirect block pointer, which is a physical block number to a block containing `BLOCK_SIZE/bytes_per_block_pointer` direct block pointers. The 14th index contains a doubly indirect block pointer, containing `BLOCK_SIZE/bytes_per_block_pointer` singly indirect block pointers. The 15th index then contains a triply indirect block pointer, containing `BLOCK_SIZE/bytes_per_block_pointer` doubly indirect block pointers.
By having these indirect block pointers, we are able to store significantly more block data while still keeping all disk reads requiring at most 5 disk accesses (1 for inode itself, 1 for triply indirect pointer, 1 for doubly indirect pointer, 1 for singly indirect pointer, 1 for block pointer, and one to read the block itself).

### Inode Data Cache
To prevent reading inode data constantly, but still having it be synced between different OFT_entries opening the same file, we created a cache for inodes in `inode_cache.h` and `inode_cache.c`. This is stored with a linked list and has all internals managed with static fields. We have 2 structs made here, one for the linked list nodes themselves, and one for the actual cached_inode data, so we can store its id and if its dirty together with the actual inode. 
Each cached_inode also stores a `dirty` field so, when clearing inode from the cache, we know if we have to spend time writing back to disk or can discard it.

The 3 main exposed functions are:
 - `get_inode_from_cache` which finds the inode in our cache if it exists and returns it, or it reads it in from disk and stores it here, before returning. If already exists, it increases the number of references to this cache node by 1.
 - `remove_ref_from_cache` is called everytime we are done using this inode's data. It finds the inode with the given id, decreases num_refs by 1, and if it is 0 removes it from cache. If it is dirty, also writes inode to disk.
 - `empty_inode_cache` which is called during unmounting to flux all dirty inodes to disk to prevent data loss.

 Most operation use this cached inode, including all `OFT Entries`, as when we have a file open we will often be accessing it's inode metadata. Whenever we open an inode into the cache, we make sure in the logic to call the `remove_ref_from_cache` so there are not memory leaks and, after finishing the session on unmount, our cache is `flushed`. This gives us a good tradeoff of performance while still reducing the chance of data loss.

### LRU Block Cache
To prevent reading and writing from disk constantly, we created an LRU cache in `lru_cache.h` and `lru_cache.c`. This is stored as a LinkedList (where we have a node_st struct which stors this current block's data, whether it is dirty, block_no_t on disk., and next/prev node). 

This is implemented with several helper functions to manage the linked list that are static internally. The functions that are exposed to the rest of the code to modify this are as follows:
- `lru_cache_add_to_front` which fetched the node from the cache. If it exists, it adds it to the front of the linked list, otherwise it creates a new node by reading in the actual block from disk, and then stores it in cache.
- `lru_cache_update_data` which writes the data given to the instance in the cache with this block, and adds it to the front of the cache (creating if doesn't exist)
- `lru_cache_empty` which is called on unmounting or closing the shell, which removes everything from cache to prevent data loss.

Internally, we store a `MAX_SIZE` which is defined to be 12 arbitrarily. This is how many blocks we can have in the cache at once. If we add more blocks than this, then we will remove the tail (which is our Least Recently Used block in our cache). When removing, we check if it was dirty and write to the disk if so, so we only update disk when we need to.

## Disk

### Disk Block Diagram
Here is an overview of how we formatted our metadata blocks in our disk. Each section here corresponds to multiple blocks, except for the super block.

```
|*************************************************************************************************************|
|            |                     |                     |                |                     |             |
| Superblock | Block Bitmap Blocks | Inode Bitmap Blocks | Root Dir Inode | Inode Table Blocks  | Data Blocks |
|            |                     |                     |                |                     |             |
|*************************************************************************************************************|
```

### Superblock
For mounting and unmount, we created a `Superblock` struct which is written into block 0. This is written when `mkfs`, and read in when we `mount` to allow disk-level metadata to persist. Our superblock contains the following:
- `signature`: String corresponding to this Filesystem and version. If the signature is found to be incorrect when mounting, signifies this filesystem is not on that disk.
- `bytes_per_block`
- `total_blocks`
- `block_bitmap_start`: Direct block pointer for the start of the block bitmap
- `block_bitmap_blocks`: Number of blocks in the block_bitmap. Generated during mkfs to ensure enough space for entire disk.
- `inode_bitmap_start`: Direct block pointer for the start of the inode bitmap
- `inode_bitmap_blocks`: Number of blocks in the inode_bitmap. Generated during mkfs based on the `inode_table_blocks` to ensure enough space for all inodes.
- `inode_table_start`: Direct block pointer for the start of the inode table
- `inode_table_blocks`: Number of blocks in the inode table. Set to a static number during mkfs.
- `data_start_block`: Direct block pointer for the start of actual file data blocks
- `root_inode_id`: inode number of the root directory. Generally set to `1`.

### Making new Filesystem (mkfs)

`mkfs` creates a fresh filesystem in the OS-owned portion of the block device.
It is called from `kernel_main` when `mount` returns `FS_INVALID`, which means
there is no valid superblock in the selected filesystem region or the metadata
failed validation.

The important design point is that `mkfs` does not blindly format block 0 on
real Raspberry Pi media. An SD card used to boot the board already has firmware
and boot files at the beginning of the disk. If the kernel wrote its superblock
there, it would destroy the boot partition and the board would no longer load
the OS. Instead, the disk layer first chooses a filesystem region and only then
formats block 0 relative to that region.

The mkfs path starts by validating its requested layout inputs:
- `inode_table_blocks` must be positive.
- `block_size_config` must map to one of the supported block sizes: 256, 512,
  1024, 2048, or 4096 bytes.
- The selected block size must match the underlying block driver sector size
  returned by `block_get_size()`.
- The block device must report a nonzero block count and a block size that fits
  the temporary mount buffer used while inspecting partition metadata.

After that validation, `mkfs` calls `configure_default_fs_region()`. This
function decides what part of the device belongs to our filesystem:

```
QEMU disk image:
  [ custom filesystem spans the full block device ]

Raspberry Pi SD card:
  [ firmware / boot partition ][ custom filesystem region ]
```

On QEMU, there is no Raspberry Pi firmware partition to preserve, so the region
starts at physical block 0 and spans the whole block device.

On Raspberry Pi hardware, `configure_default_fs_region()` reads physical block
0 as an MBR and verifies the `0x55AA` boot signature. It then walks the four MBR
partition entries at byte offset 446. The code treats FAT and EFI-style
partition types as boot partitions: `0x01`, `0x04`, `0x06`, `0x0B`, `0x0C`,
`0x0E`, and `0xEF`. When one of these is found, the filesystem base is computed
as:

```
fs_base_block = partition_start + partition_block_count
fs_block_count = device_block_count - fs_base_block
```

This makes our filesystem start immediately after the original partition that
the card was already using for boot files. The helper that sets this region
checks for empty partitions, integer wraparound, empty devices, and bases that
would land at or beyond the end of the block device. Those checks prevent a bad
partition table from making the filesystem overlap the boot partition or point
outside the disk.

If the MBR contains a protective GPT partition (`0xEE`), the disk layer switches
to GPT parsing. It reads the GPT header from LBA 1, verifies the `EFI PART`
signature, and uses the header fields for the partition-entry LBA, entry count,
and entry size. GPT entries are ranked so the region is anchored after the most
likely original boot/data partition:
- EFI System Partition GUID is preferred first.
- Microsoft Basic Data GUID is preferred second.
- Any other non-empty GPT entry is accepted as a last fallback.
- Unused entries are ignored.

If multiple entries have the same rank, the earliest one on disk wins. Once the
best GPT entry is selected, the filesystem starts after that entry's last LBA.
This mirrors the MBR behavior: the kernel preserves the partition that existed
before our filesystem and claims the remaining space after it.

If the disk has a valid MBR but no recognized boot partition type, the first
non-empty MBR partition is used as a fallback anchor. This is less specific than
the FAT/EFI cases, but it is still safer than formatting from block 0 because it
keeps the original partition intact.

Once the region is known, all filesystem block numbers become relative to that
region. The superblock is filesystem block 0, but physically it is written at
`fs_base_block`. All later block reads and writes go through the disk layer, so
the offset is applied consistently instead of being reimplemented in each inode
or directory function.

The on-disk layout inside the selected region is:
- Superblock at filesystem-relative block 0.
- Block bitmap starting at block 1.
- Inode bitmap after the block bitmap.
- Inode table after the inode bitmap.
- Data blocks after the inode table.

The bitmap sizes are computed from the chosen region rather than hardcoded. The
block bitmap is sized from the total filesystem block count and the number of
bits that fit in one block. The inode bitmap is sized from the number of inodes
available in the requested inode table. This keeps the same mkfs code usable
across different disk sizes and block sizes.

`fs_set_layout()` stores the computed layout in the static disk-layer fields.
Then `mkfs_inode()` writes the superblock, bitmaps, root inode, and root
directory contents. The root directory is created as inode `ROOT_INO` and
contains `.` and `..` entries pointing back to itself.

After the core filesystem exists, `seed_user_bins_for_mkfs()` writes the
embedded userspace binaries into the new filesystem image. Finally,
`lru_cache_empty()` flushes dirty cached blocks so the just-created filesystem
is durable before the kernel mounts and uses it.

### Mounting

Mounting starts before any filesystem metadata is trusted. The first step is to
choose the filesystem region on the block device with
`configure_default_fs_region()`.

The region-selection logic differs by platform:
- On QEMU, the whole block device is treated as the filesystem.
- On Raspberry Pi hardware, block 0 is read as an MBR. If it contains a GPT
  protective partition (`0xEE`), GPT logic chooses the filesystem region. If it
  contains a boot-style MBR partition, the filesystem region is placed after
  that partition. If no boot partition is found, the first non-empty partition
  is used as the fallback anchor.

This design lets the same filesystem code run in two environments: a simple
QEMU disk image and a real Raspberry Pi SD card with boot firmware files stored
outside the OS filesystem region.
For RPI hardware, this also lets us preserve the FAT32 Boot partition containing BCM_2712 firmware, while still treating the rest of the disk as our own custom Filesystem.

After that initial setup, we then read in the `superblock` and validate that it has a proper signature and all other metadata is correct. If it's not, then this returns back to `kernel_main` entrypoint and triggers us to `mkfs` and make a new filesystem. We then read in the superblock itself to load all the metadata into the static fields in `disk.c`.

Lastly for mounting, we validate the root directory was created correctly and call all our initialization functions for `oft`, `seeding user bins`, and `procfs`.

### Unmounting
To unmount, we empty our `OFT` to ensure there is no memory leaks, and then flush our `LRU Cache` and `Inode Cache` to ensure there is no data loss, and set a flag to signify there is no fs loaded.

### Inode/Block Bitmaps
In order to efficiently allocate new blocks and inodes, we implemented Bitmaps. These are special blocks that are read bit-wise, where each bit is a 1 or a 0 corresponding to if that block/inode is allocated or not.

When allocating a new block, we iterate through the block bitmap until we find a free block (a bit set to 0), toggle it to 1, and then return this new block to use. When unallocating a block, we can just clear that bit back to 0, and it will be considered unused without requiring us to spend time zeroing it out.

When allocating a new inode, we iterate through the inode bitmap until we find a free inode (a bit set to 0), toggle it to 1, and return this as the inode number to use. This corresponds to the direct index into the inode table where this inode metadata will be stored on disk. To free an inode, we can then make sure to unallocate all blocks and set the bit in the inode table back to 0.

Since each block can contain `BYTES_PER_BLOCK*8` bits corresponding to individual blocks or inodes, this results in minimal disk I/Os for each block/inode allocation.

## Directories

### Directory Structure and Dirents
The core of our directory structure is a file type called `Directory` which stores a list of `dirents` in its file data blocks. These contain the following:
- `name`: max 32 char file name
- `ino_id`: inode id of this file

Each of these directory entries references an inode/file, which allows our hierarchical directory structure to store a list of files of any type, including nested subdirectories. All metadata for these dirents is stored in the inodes themselves to allow for symlinks to sync up data among themselves.

Our directories also store, in the inode's `Union` field, an `i_dir` struct pointer to metadata. This metadata contains only an `offset`, which is the index of the next dirent we are reading. This is set to 0 when we `opendir` and cleared when we `closedir`. We can then, each time we `readdir`, read the dirent at `offset` and increment our `offset` to allow for reading directory entries one at a time.

### Dirent fops
To support our VFS implementation, we have a list of directory file operations stores in each inode's fops with default operations declared for directories. This is documented in more detail in [Virtual Filesystem](#virtual-filesystem).
Here is a brief list of what we support:
- `lookup`
- `readdir`
- `getattr`
- `opendir` (set to `fops->open`)
- `closedir` (set to `fops->close`)

### Directory API Functions
All functions here return `err_t`, which is an int type corresponding to error codes from [errors.h](../../kernel/fs/errors.h).
- `add_dirent`: Creates a new directory entry with the given name in directory `curr_dir`, with the given `ino_id` inode. This iterates through the directory until it reaches the last dirent, adds this file as a new one, and then adds a null terminating dirent at the end.
- `get_dirent_by_f_name`: Wrapper around the `fops->lookup` VFS file operation.
- `get_dirent_by_path`: Splits the path by `/` and uses `get_dirent_by_f_name` for each token in the path, and then returns the direct of the final portion.
- `list_dirents`: Verifies the given inode is a directory, and then uses VFS `fops->readdir` to read directory entries one by one and print data to `out_fd`.
- `remove_dirent_by_fname_and_type`: Iterates through dirents in the given directory until it reaches the file with the specified name and type, and then removes the dirent and shifts the rest down. Removes refcount of that inode by 1, and removes inode if it reaches 0.
- `add_dirent_by_path`: Wrapper around `get_dirent_by_path` for the parent dir and then uses `add_dirent` from that directory.

## Open-file table

### OFT Design and oft_entry Struct
Our Open File Table is the kernel level way of maintaining open files, and stores all files currently opened by any process in our OS. This is stored internally as a dynamically-resizing `Vector` of `oft_entry` structs. When creating a new file, we search through the `Vector` until we find a `NULL` entry, meaning this is an unallocated file descriptor, and return this as our new file's `fd`. If there are no `NULL` entries, we append it to the back of the `Vector`.
While this results in longer `OFT` creations, up to `O(number of files open at once)`, this allows us to retrieve `oft_entries` by `fd` in constant time with direct addressing, which is a much more common operation, so we chose to stil with this design.

For the `oft_entry`, this is our internal implementation of the `FILE` struct from linux. All internal file system functions from `KAPI` layer and lower use these `oft_entry` structs to refer to files as opposed to the file descriptor int.
They contain the following data:
- `mode`: Stores the flags we set with `open` for this file entry. Combination of `O_RDONLY`, `O_WRONLY`, `O_RDWR`, and `O_APPEND`. `O_TRUNC` and `O_CREAT` are not here as they only have an effect when initially opening the file.
- `cursor`: Current byte offset for reading/writing for this instance of this file.
- `ref_count`: Reference count of this `oft_entry`. Incremeneted when using `fork()`, as the `OFT` is duplicated to the child process.
- `ino_id`: Inode number for this file
- `inode`: Cached reference to this inode to prevent constant disk I/Os for performance

### OFT API Functions
- `oft_open_file`: Opens a new file with the given filename, inode id, parent dir block, and mode. If `ino_id` is 0, uses `add_dirent` and `add_new_file`to create a new dirent and file in this directory. This is only done when `O_CREAT` is true, or else `k_open` would fail first.
- `oft_close_file`: Decrement refcount for the specified `oft_entry`, and clear it from `Open File Table` when equal to 0.
- `get_oft_entry_by_fd`: Resolves a kernel fd to its `oft_entry` pointer for going from `cmd` level to `kapi` level and below
- `oft_add_reference`: Used when calling `fork()`
- `initialize_oft`
- `empty_oft`: clears all `OFT entries` for flushing when unmounting.

## Virtual Filesystem

### Overview

The Virtual Filesystem layer is the routing layer between path-based kernel API
calls and the concrete object that actually serves the operation. The rest of
the kernel should be able to open a path, get an `oft_entry`, and call `read`,
`write`, `lookup`, or `readdir` without caring whether the target is a normal
disk file, a directory, a character device, a pipe, or a generated virtual file.

The shape of the stack is:

```
userspace syscall wrapper
  -> fs/cmds.c process-fd wrapper
  -> kapi.c kernel filesystem API
  -> oft.c open-file table
  -> inode metadata fops
  -> disk inode, character driver, pipe, or virtual filesystem provider
```

This design keeps policy at the high level and implementation-specific behavior
at the inode level. Permission checks, path handling, file descriptor ownership,
and OFT reference counting stay shared. The operation itself is dispatched
through the inode's `struct file_operations`.

### File Operation Table

The main VFS abstraction is `struct file_operations`. It contains optional
handlers for:
- `open`: prepare a file after it has an OFT entry.
- `close`: release per-open or per-inode state.
- `read`: copy data from the file object into the caller's buffer.
- `write`: copy data from the caller into the file object.
- `lookup`: resolve a child name inside a directory.
- `readdir`: return one directory entry at a time.
- `getattr`: return file metadata when the provider cannot use the normal disk
  inode metadata directly.

The handlers are intentionally small and UNIX-like. The KAPI layer performs the
common checks first, then calls the operation stored in the inode. For example,
`k_read` verifies that the open mode allows reading before calling
`entry->inode->inode.metadata.fops->read`. `k_write` does the same for write
permissions. `k_close` calls the file-specific close operation before dropping
the OFT reference.

### Default File and Directory Operations

The KAPI layer defines two default fops tables for the normal inode filesystem:
- `default_ops` for regular files.
- `default_dir_ops` for directories.

Regular files support `open`, `read`, and `write`. The default open handler is
also where `O_TRUNC` is applied. If a file is opened with truncation, the file's
existing blocks are released, its size is reset, and the `O_TRUNC` bit is
cleared from the live open mode so later reads and writes are normal operations.

Directories use a separate fops table because directory behavior is not byte
stream behavior at the lookup layer. A directory supports:
- `open`: initialize directory iteration state.
- `close`: clear directory iteration state.
- `lookup`: map a child name and expected type to a dirent.
- `readdir`: return directory entries sequentially.

This is why `get_dirent_by_f_name` can resolve children by calling
`metadata.fops->lookup` instead of directly scanning disk blocks itself. A
disk-backed directory and a virtual directory can both present the same lookup
interface.

### Path Lookup Flow

Path lookup starts in `get_dirent_by_path`. Absolute paths begin at `ROOT_INO`;
relative paths begin at the current process's `cwd`. The path is split on `/`,
and each component is resolved by `get_dirent_by_f_name`.

`get_dirent_by_f_name` first obtains metadata for the current directory inode
through `vfs_get_metadata`. It verifies that the current inode is a directory
and that the directory has a `lookup` operation. It then calls that lookup
operation to resolve the next path component.

For normal disk directories, `dir_lookup` scans the directory's on-disk dirent
array. This keeps normal files and directories persistent. The VFS extension is
added at the root directory boundary: lookup in `ROOT_INO` checks
`vfs_lookup_root_mount` before scanning disk dirents. That gives registered
mounts overlay semantics, so virtual roots such as `/proc` and `/dev` are
visible without storing dirents on disk and are not hidden by stale disk
entries with the same names.

### Root Virtual Mounts

The current VFS supports root-level virtual mounts. This is deliberately smaller
than a full mount namespace, but it is enough for generated kernel filesystems
such as `/proc`.

`virtual_fs.c` stores a fixed-size root mount table. Each entry contains:
- `name`: the root-level path component, such as `proc`.
- `root_ino`: the synthetic inode number of the virtual filesystem root.
- `ops`: provider callbacks for that virtual filesystem.

`vfs_register_root_mount(name, root_ino, ops)` validates the name, root inode,
and operation table. If the name already exists, the existing mount is updated.
Otherwise, a new slot is allocated from the fixed table. The table currently
supports up to eight root mounts.

`vfs_lookup_root_mount(name, is_dir_type, dirent)` is called by root directory
lookup. It only returns virtual mount entries for directory lookups, because a
root mount behaves like a directory. When a match is found, it fills a normal
`dirent` with the mount name and synthetic root inode. The caller can then open,
stat, or continue walking the virtual directory using the same code path as any
other directory.

`vfs_root_mount_readdir(offset, dirent)` exposes registered virtual mounts while
listing `/`. This is what lets `ls /` show virtual mount points even though they
are not backed by persistent dirents.

### Virtual Inode Ownership

Disk inodes live in the inode table and are cached through the inode cache.
Virtual inodes do not have records on disk, so the VFS needs a second dispatch
point after path lookup: inode ownership.

Each virtual filesystem provides `struct virtual_fs_ops`:
- `is_inode(ino)`: returns whether this provider owns a synthetic inode number.
- `get_metadata(ino, metadata)`: fills an `attributes_t` for a virtual inode.
- `alloc_cached_inode(ino, node)`: creates a `cached_inode_st` shaped object for
  the OFT and KAPI layers to hold.
- `free_cached_inode(node)`: releases the virtual cached inode.
- `format_path(ino, path, size)`: formats a path for a synthetic inode.

The dispatch helpers are:
- `vfs_get_inode(ino, node)`: routes to a virtual provider if `is_inode`
  matches, otherwise falls back to `get_inode_from_cache`.
- `vfs_put_inode(node)`: routes virtual nodes to `free_cached_inode`, otherwise
  removes a reference from the disk inode cache.
- `vfs_get_metadata(ino, metadata)`: asks the virtual provider for generated
  metadata, otherwise reads disk inode metadata.
- `vfs_format_path(ino, path, size)`: lets a virtual filesystem render paths
  that do not exist as disk dirents.

The common carrier is still `struct cached_inode_st`. Virtual providers allocate
objects with the same outer shape so the OFT layer does not need separate code
for disk and virtual files. The provider is responsible for making the embedded
metadata accurate, including file type, permissions, size, and fops.

### Procfs Example

`procfs` is one virtual filesystem using this layer. During mount,
`procfs_init()` resets its internal node tables, prepares the proc root node,
and registers the root mount named `proc`.

After registration, `/proc` is resolved like this:
- Lookup starts at the disk root inode.
- Root lookup asks the VFS root mount table before scanning disk dirents.
- The VFS returns a dirent named `proc` with procfs's synthetic root inode.
- Later inode operations are routed back to procfs because its `is_inode`
  callback owns that inode range.

Procfs then generates directory and file contents from kernel state. Its root
directory can expose static files and live process directories. PID directories
are generated from the process table rather than stored on disk. Per-process
files are represented by synthetic inodes whose read fops format current
process metadata into a buffer.

This is the main reason VFS routing exists in the filesystem: tools such as
`ls`, `cat`, `stat`, and future shell code can use the same userspace API for
real files and kernel-generated information.

`/proc/mounts` is generated from the VFS root mount table. It lists the
disk-backed root filesystem plus each registered root virtual mount, currently
including `/proc` and `/dev`.

### Devfs and Character Devices

`devfs` is the `/dev` virtual filesystem. During mount, `devfs_init()` clears
its in-memory node table and registers the root mount named `dev`. Character
drivers register later in boot; calls to `devfs_create_char_device(rdev)` then
populate virtual entries such as `/dev/uart0`, `/dev/tty0`, and
`/dev/ttygui0`.

Device nodes are not disk-visible dirents anymore. Each active devfs node is a
synthetic inode with `CHAR_DRIVER_TYPE`, an `i_rdev` major/minor number, and fops
from the currently registered character driver. Opening `/dev/tty0`,
`/dev/uart0`, or `/dev/ttygui0` still goes through path lookup, KAPI, and the
OFT. Once the inode is open, its fops dispatch to the registered character
driver instead of normal file block I/O.

This reuse is intentional. The VFS layer is both the mechanism for generated
filesystems like `/proc` and the mechanism for special inode types that need to
override ordinary file behavior while preserving one user-facing file
descriptor API.

### VFS Invariants and Limits

The VFS is intentionally simple:
- Virtual mounts currently exist only at the filesystem root.
- The root mount table is fixed-size rather than dynamically allocated.
- Virtual providers must use stable synthetic inode ranges and implement
  `is_inode` correctly so inode ownership is unambiguous.
- Virtual inode metadata must provide the correct file type and fops table.
- Disk-backed files remain the default path; VFS dispatch only takes over when
  a provider explicitly owns an inode.

These limits keep the implementation small while still creating the important
abstraction boundary. The kernel gets a single file descriptor and path API, but
new file-like systems can be added without rewriting `open`, `read`, `write`,
`stat`, `ls`, or the open-file table.

## Character Devices

### Overview
`Character devices` correspond to a special file type `CHAR_DRIVER_TYPE`. This type defines special `fops` file operations at the VFS level to give special behavior for reading and writing to files.

When using a `CHAR_DRIVER_TYPE`, we also use the `i_dev` field in `inode metadata` which stores the char device `major` and `minor` number.
The `major` number here is an identifier corresponding to the specific type of `char driver` (ex. a `tty` driver), and the `minor` number corresponds to the instance of that driver this inode is associated with (ex. 0 for `tty0`).

Inside our [devices.c](../../kernel/devices/devices.c) file, we store an array `char_device_registry` which stores a list of `char_driver` structs. These each store:
- `name`: character driver name
- `major`: The char driver major number
- `fops`: The special fops for this file.
- `data`: a void* for the device-specific global data.

### Registering Char Devices
After the filesystem mount registers devfs, boot initializes each `char_driver` and registers it with `register_char_driver`. This is done to allow for dynamic creation of char devices so, as a future extension, we can create a package manager which can dynamically add and register new drivers.
This takes in a `char_driver` struct and registers it in the array at the specified `major` number.

### Creating new instances of Char Devices
When creating a new char device (ex. new tty instance), we have a `devfs_create_char_device` which creates a new devfs entry with the specified `rdev` major and minor number.
This fetches the `char_driver` from the `char_device_registry` and creates a virtual `CHAR_DRIVER_TYPE` node with the specified `name` + `minor` number under `/dev`.
The generated virtual inode stores the `major` and `minor` number and uses the registered `char_driver` fops.

For any `char device` instance specific metadata, this can be stored in the `char_device_registry`'s `data` field, with it using a struct containing an array of per-instance data.

### More Info
You can find more details about specific device implementations, like `pipes` and `ttys` in [device-drivers.md](device-drivers.md).

## Permission Handling
For permission handling, each inode has a `permissions` metadata field which stores bitwise indicators for `read`, `write`, and `exec`. When opening a file with a specified mode, we check that the inode has the specified permissions enabled, or else the call fails.

When opening these files with a specific mode (`O_RDONLY`, `O_WRONLY`, `O_RDWR`), we store this mode in the `oft_entry`, and verify against this anytime we try to `read` or `write`, and if we don't have the correct permissions, the call fails.

## Error handling
To ensure proper error handling, every call returns a type `err_t`, which is an int corresponding to a specific error in [errors.h](errors.h). These errors can then be parsed into an error message in (errors.c)[errors.c] or converted from a filesystem internal error code (which is more specific and handles more filesystem-specific errors), into a standard UNIX-style error code for our userspace functions to return back to the user and the rest of the kernel.
