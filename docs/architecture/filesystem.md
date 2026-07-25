# Filesystem Overview

## List of Features

- Inode-based filesystem with ext2-inspired block layout
- Disk persistence with mount/unmount support
- Directories, nested paths, hard links, symbolic links, and permissions
- Open-file table and file-descriptor integration
- Virtual filesystem layer with `procfs` and `devfs` root mounts
- Character devices and VFS file-operation dispatch
- LRU block cache and inode cache
- Filesystem-backed ELF loading into `/bin`
- [Kernel API layer](#kernel-api-layer)
- [Design tradeoffs and limits](#design-tradeoffs-and-limits)

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
The command/syscall layer is the user-facing filesystem boundary. Functions in
`fs/cmds.h` are called by syscall wrappers and translate per-process file
descriptors through the current process's descriptor table into kernel open-file
table entries. Example operations include `open`, `close`, `read`, and `write`.

### Kernel API Layer
The kernel API layer contains the high-level filesystem logic. These functions
take paths or OFT entries, perform validation and policy checks, and delegate to
the OFT, directory-entry, inode, VFS, or disk layers as needed. Example
functions include `k_open`, `k_close`, `k_read`, and `k_write`.

### OFT Layer
The open-file table is the kernel-level table of open file descriptions. Each
OFT entry stores a cached inode reference plus open-file metadata such as cursor
position, open mode, and reference count.

### Dirent Layer
Directory entries are stored inside directory data blocks. Each dirent contains
a file name and inode number; most metadata remains in the inode layer.

### Disk Layer
The disk layer owns persistent filesystem layout and block allocation state. It
provides operations such as `mount`, `unmount`, `mkfs`, and
`get_ith_block_of_file` while hiding physical block offsets from higher layers.

### Inode Layer
The inode layer stores persistent file metadata and block pointers using an
ext2-inspired layout. It supports direct, singly indirect, doubly indirect, and
triply indirect block pointers for file data.

This split is important because it keeps directory entries small and makes the
inode the single owner of file metadata. Directories only need names and inode
numbers, while permissions, sizes, link counts, device ids, and file-operation
dispatch stay with the object being referenced.

### Hardware Drivers
The filesystem reaches storage through the block-device API. The current
persistent backend is SDHCI-backed SD storage on Raspberry Pi hardware and an SD
image in QEMU.

# Detailed Architecture and Decisions

## Kernel API Layer

This layer contains the high-level kernel logic used by filesystem syscalls and
other kernel subsystems. It delegates work to the directory-entry, OFT, VFS,
inode, and disk layers.

Most functions return `err_t` values from
[`errors.h`](../../kernel/fs/errors.h). Syscall-facing paths convert these
internal filesystem errors into Unix-style negative `SYS_E*` values through
helpers in [`errors.c`](../../kernel/fs/errors.c).

KAPI specifications:

- `k_open(const char *fname, int mode)`: Opens a path, creates an OFT entry, and returns a kernel file descriptor. Supported flags include `O_TRUNC`, `O_CREAT`, `O_APPEND`, `O_RDONLY`, `O_WRONLY`, and `O_RDWR`. The path, file name, and requested permissions are validated before the VFS `open` handler runs.
- `k_close(struct oft_entry *entry)`: Calls the VFS `close` handler when present, then closes the OFT entry.
- `k_read(struct oft_entry *entry, char *buf, size_t n)`: Verifies read permissions, then dispatches to the VFS `read` handler.
- `k_write(struct oft_entry *entry, const char *buf, size_t n)`: Verifies write permissions, then dispatches to the VFS `write` handler.
- `k_update_file_time(const char *file_name)`: Updates inode modification time for `touch`.
- `k_lseek(int fd, int offset, int whence)`: Updates the file cursor. `whence` accepts `F_SEEK_SET`, `F_SEEK_CUR`, and `F_SEEK_END`. Seeking past end-of-file allocates blocks for the hole and writes zeroes.
- `k_chmod(const char *file_name, uint8_t new_perms, int flag)`: Updates inode permission bits. `flag` sets, removes, or adds the provided bits.
- `k_mv_file(const char *src_path, const char *dest_path)`: Renames or moves a path. If the destination exists, it is removed first.
- `k_unlink(const char *fname)`: Drops one link from the target inode and removes the inode when the link count reaches zero.
- `k_ls(const char *filename, int out_fs)`: Lists a directory to an output file descriptor. A `NULL` path uses the current process's `cwd`.
- `k_check_if_exists(const char *f_name)`: Checks whether a path exists.
- `k_file_add_reference(int fd)`: Adds a reference to the selected file.
- `k_make_directory(char *f_path)`: Creates a directory path for `mkdir`.
- `k_change_directory(char *f_path)`: Updates the current process's `cwd`; relative paths are resolved from the current `cwd`.
- `k_check_if_executable(char *f_name)`: Checks whether a file has execute permission.
- `k_stat(const char *path, struct fs_stat_st *stat)`: Reads filesystem metadata for `stat` and `/proc` reporting.
- `createlink(const char *create_path, const char *orig_path, int is_soft)`: Creates hard links or symbolic links. Hard links add another dirent to the target inode; symbolic links allocate a symlink inode and store the absolute target path in its file data.
- `k_readlink(const char *path, char *buffer, size_t count)`: Copies a symbolic link's stored target path into a userspace buffer without following the link.
- `k_exec` and `k_exec_process`: Bridge filesystem paths into the runtime
  ELF loader; the full design is documented in [elf-loading.md](elf-loading.md).

This level also contains the default file operations for files and directories,
which are documented in [Virtual Filesystem](#virtual-filesystem).

## Inodes

The inode layer uses an ext2-inspired metadata structure. Each inode stores:

- `i_links_count`: link count.
- `type`: file type, such as regular file, directory, pipe, or character device.
- `perm`: read, write, and execute permission bits.
- `i_size`: file size in bytes.
- `i_blocks`: number of blocks owned by the file.
- `mtime`: last modification time.
- `i_rdev`: character-device major/minor number for device nodes.
- `fops`: VFS file-operation handlers such as `read`, `write`, `open`, `lookup`, and `readdir`.
- `i_<type>`: type-specific metadata, such as `i_pipe` for pipe state.

Regular files, directories, devices, pipes, virtual files, and symbolic links
all use the same inode metadata shape. The important difference is their fops
table. A symbolic-link inode stores its target path as ordinary file data, but
its fops resolve that path and forward normal operations to the target inode.

### Blocks
Each inode stores a 15-entry block pointer array:

- The first 12 entries are direct block pointers.
- Entry 13 is a singly indirect block pointer.
- Entry 14 is a doubly indirect block pointer.
- Entry 15 is a triply indirect block pointer.

This layout supports larger files without making small files expensive. Looking
up a block through the deepest path requires reading the inode, triply indirect
block, doubly indirect block, singly indirect block, direct block pointer, and
target data block.

The design mirrors the educational value of ext2 without trying to be fully
ext2-compatible. Small files stay compact because their direct blocks are
immediate, while larger files can grow without adding extents, B-trees, or a
more complex allocator.

### Inode Data Cache
The inode cache in [`inode_cache.h`](../../kernel/fs/caches/inode_cache.h) and
[`inode_cache.c`](../../kernel/fs/caches/inode_cache.c) avoids repeated disk
reads for inode metadata while keeping shared inode state synchronized across
open-file table entries. Cache nodes track the inode id, reference count, cached
inode data, and whether the inode is dirty.

The main exposed functions are:

- `get_inode_from_cache`: returns an existing cached inode or reads it from disk and inserts it into the cache.
- `remove_ref_from_cache`: drops one reference and writes dirty inode data back when the final reference is removed.
- `empty_inode_cache`: flushes all dirty inode cache entries during unmount.

OFT entries hold cached inode references while files are open. This keeps common
metadata access fast while still flushing dirty data on close or unmount.

The cache exists because the same inode is often touched repeatedly through
multiple layers: path lookup, permission checks, `stat`, reads, writes, and
inherited descriptors after `fork`. Keeping one cached copy reduces disk traffic
without forcing the higher-level KAPI and OFT code to know how inode blocks are
laid out on disk.

### LRU Block Cache
The LRU block cache in [`lru_cache.h`](../../kernel/fs/caches/lru_cache.h) and
[`lru_cache.c`](../../kernel/fs/caches/lru_cache.c) reduces repeated block I/O.
It stores cache nodes in a linked list, tracking each block's data, block number,
dirty state, and previous/next pointers.

The public cache operations are:

- `lru_cache_add_to_front`: fetches a block from cache or disk and marks it most recently used.
- `lru_cache_update_data`: updates cached data for a block, creating the cache node if needed.
- `lru_cache_empty`: flushes and removes cached blocks during unmount.

The cache has a fixed capacity of 12 blocks. When capacity is exceeded, the
least recently used tail node is evicted; dirty evictions are written back to
disk.

An LRU cache is a useful middle ground for this codebase. It demonstrates the
same locality principle used by larger filesystems, but it avoids adding a
large buffer-cache subsystem, writeback daemon, or adaptive policy that would
make the rest of the filesystem harder to inspect.

## Disk

### Disk Block Diagram
The on-disk filesystem region is organized into metadata and data blocks. Each
section may span multiple blocks except the superblock.

```
|*************************************************************************************************************|
|            |                     |                     |                |                     |             |
| Superblock | Block Bitmap Blocks | Inode Bitmap Blocks | Root Dir Inode | Inode Table Blocks  | Data Blocks |
|            |                     |                     |                |                     |             |
|*************************************************************************************************************|
```

### Superblock
The `Superblock` is written at filesystem-relative block 0 during `mkfs` and
read during `mount`. It stores persistent layout metadata:

- `signature`: filesystem/version signature used to validate the disk region.
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

Keeping layout data in a superblock lets the kernel validate a disk region
before trusting inode or bitmap contents. It also means the same filesystem
logic can operate on different block counts without baking one disk image size
into the code.

### Making New Filesystem (mkfs)

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

This is one of the places where supporting real Raspberry Pi 5 hardware changes
the filesystem design. In QEMU, a whole raw disk image can belong to the OS. On
hardware, the same SD card also has to carry firmware-visible boot files, so the
filesystem must be careful about where its "block 0" actually starts.

The mkfs path starts by validating its requested layout inputs:
- `inode_table_blocks` must be positive.
- `block_size_config` must map to one of the supported block sizes: 256, 512,
  1024, 2048, or 4096 bytes.
- The selected block size must match the underlying block driver sector size
  returned by `block_get_size()`.
- The block device must report a nonzero block count and a block size that fits
  the temporary mount buffer used while inspecting partition metadata.

After that validation, `mkfs` calls `configure_default_fs_region()`. This
function decides what part of the device belongs to the custom filesystem:

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

This makes the filesystem start immediately after the original partition that
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
before the custom filesystem and claims the remaining space after it.

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

On Raspberry Pi 5 hardware, this preserves the FAT boot partition containing the
board firmware while allowing the remaining disk region to be used by the
custom filesystem.

After region selection, `mount` reads the superblock and validates its signature
and metadata. Invalid metadata returns `FS_INVALID` to `kernel_main`, which can
then call `mkfs` to create a fresh filesystem. A successful mount loads the
superblock into `disk.c` static layout fields, validates the root directory,
initializes the OFT, seeds `/bin`, and registers virtual filesystems such as
`procfs` and `devfs`.

### Unmounting

Unmount closes open-file table state, flushes the LRU block cache and inode
cache, and marks the filesystem as not loaded.

### Inode/Block Bitmaps

Block and inode allocation use bitmaps. Each bit represents whether a block or
inode table slot is allocated.

Allocating a block scans for a zero bit, sets it, and returns the corresponding
block number. Freeing a block clears the bit. Allocating an inode follows the
same pattern against the inode bitmap and returns an inode number that indexes
the inode table. Freeing an inode releases its data blocks and clears its inode
bitmap bit.

Since each bitmap block contains `BYTES_PER_BLOCK * 8` allocation bits, the
scheme keeps allocation metadata compact.

Bitmaps are not the most sophisticated allocation strategy, but they are a good
fit here: allocation state is easy to validate, easy to flush, and easy to
inspect while debugging filesystem corruption.

## Directories

### Directory Structure and Dirents

Directories store `dirent` records in file data blocks. Each dirent contains:

- `name`: file name, capped at 32 characters.
- `ino_id`: inode id for the referenced file.

Dirents let a directory reference regular files, subdirectories, symbolic links,
devices, pipes, or virtual entries. File metadata stays in the inode, so
different path entries can refer to the same underlying inode state.

Directory iteration uses type-specific inode metadata. The `i_dir` state stores
the current read offset, which is initialized by `opendir`, cleared by
`closedir`, and advanced by each `readdir` operation.

This gives directory reads the same open-file shape as regular file reads:
state belongs to the open object, not to a global iterator. That matters once
multiple processes or descriptors refer to the same directory path.

### Dirent fops

Directory inodes use directory-specific VFS file operations. The default
directory operations include:

- `lookup`
- `readdir`
- `getattr`
- `opendir` (set to `fops->open`)
- `closedir` (set to `fops->close`)

### Directory API Functions

Directory helpers return `err_t` values from
[`errors.h`](../../kernel/fs/errors.h).

- `add_dirent`: Creates a new directory entry with a name and inode id in `curr_dir`.
- `get_dirent_by_f_name`: Wrapper around the `fops->lookup` VFS file operation.
- `get_dirent_by_path`: Splits a path by `/`, resolves each component, and returns the final directory entry.
- `list_dirents`: Opens the inode through its fops, reads entries through VFS `readdir`, and prints them to `out_fd`.
- `remove_dirent_by_f_name`: Removes a matching dirent, shifts later entries down, and drops one inode reference.
- `add_dirent_by_path`: Resolves the parent directory and adds a dirent there.

## Links

Hard links are additional directory entries pointing at an existing inode. The
inode link count records how many dirents reference it, and storage is released
when the final link is removed.

Symbolic links are their own inode type. Creating one stores the target as an
absolute path in the symlink inode's data blocks and assigns the symlink fops
table to the inode. The symlink fops read that stored path, resolve the target,
and forward file operations such as `open`, `read`, `write`, `lookup`, and
`readdir` to the target's fops. This lets soft links work for regular files,
directories, and executable paths without adding symlink branches throughout
the rest of the filesystem.

Opening a symlink replaces the open-file entry's inode with the resolved target
inode. That means `exec` sees the real executable inode and file size, and `cd`
to a symlinked directory stores the target directory inode as the process cwd.
`readlink` is the exception: it reads the symlink inode's stored path directly
and does not follow the target.

## Open-file table

### OFT Design and oft_entry Struct

The open-file table stores file descriptions that may be referenced by multiple
process file descriptors. It is implemented as a dynamically resizing `Vector`
of `oft_entry` pointers. Allocation scans for an unused slot and appends when no
free slot exists. That makes allocation linear in the number of open files, but
descriptor lookup remains direct.

This design is intentionally close to the Unix distinction between a process
file descriptor and an open file description. It is what makes `fork` descriptor
inheritance, `dup2`, redirection, pipes, and device files work through one
resource model instead of special-casing each feature in the process layer.

An `oft_entry` contains:

- `mode`: live open mode, including `O_RDONLY`, `O_WRONLY`, `O_RDWR`, and `O_APPEND`.
- `cursor`: current byte offset for reads and writes.
- `ref_count`: number of references to the open file description, including inherited references after `fork`.
- `ino_id`: inode number for the opened object.
- `inode`: cached inode reference used to avoid repeated disk I/O.

### OFT API Functions

- `oft_open_file`: Opens an inode with a name, parent directory, and mode. When `O_CREAT` is active and no inode exists, it creates the dirent and file.
- `oft_close_file`: Decrements the OFT reference count and clears the table slot when the count reaches zero.
- `get_oft_entry_by_fd`: Resolves a kernel file descriptor to its OFT entry.
- `oft_add_reference`: Adds an inherited OFT reference during `fork`.
- `initialize_oft`
- `empty_oft`: clears OFT entries during unmount.

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

The handlers are intentionally small and Unix-style. The KAPI layer performs the
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
- `lookup`: map a child name to a dirent.
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
and verifies that the inode has a `lookup` operation. It then calls that lookup
operation to resolve the next path component. Regular files do not provide
`lookup`, so trying to walk through one returns `NOT_A_DIRECTORY`. Directories,
virtual directories, and symlinks to directories can all participate in lookup
through their fops.

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

`vfs_lookup_root_mount(name, dirent)` is called by root directory lookup. When a
match is found, it fills a normal `dirent` with the mount name and synthetic
root inode. The caller can then open, stat, or continue walking the virtual
directory using the same code path as any other directory.

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

The reason to put `procfs`, `devfs`, pipes, directories, and disk files behind
the same fops shape is not abstraction for its own sake. It lets ordinary shell
tools exercise kernel subsystems through the same `open`, `read`, `write`,
`stat`, and `ls` paths that regular files use. That makes the OS easier to
debug and makes userspace behavior more Unix-style without claiming full POSIX
coverage.

### VFS Invariants and Limits

The VFS is intentionally simple:

- Virtual mounts currently exist only at the filesystem root.
- The root mount table is fixed-size rather than dynamically allocated.
- Virtual providers must use stable synthetic inode ranges and implement
  `is_inode` correctly so inode ownership is unambiguous.
- Virtual inode metadata must provide the correct file type and fops table.
- Symlink inodes use persistent symlink fops and resolve the target from their
  stored path when an operation follows the link.
- Disk-backed files remain the default path; VFS dispatch only takes over when
  a provider explicitly owns an inode.

These limits keep the implementation small while still creating the important
abstraction boundary. The kernel gets a single file descriptor and path API, but
new file-like systems can be added without rewriting `open`, `read`, `write`,
`stat`, `ls`, or the open-file table.

## Character Devices

### Overview

Character devices use the special inode type `CHAR_DRIVER_TYPE`. Their inode
metadata stores an `i_rdev` major/minor pair:

- The major number selects the character driver, such as the TTY driver.
- The minor number selects a driver instance, such as `tty0`.

[`devices.c`](../../kernel/devices/devices.c) stores a `char_device_registry`
array of `char_driver` structs. Each driver stores:

- `name`: character driver name
- `major`: character-driver major number
- `fops`: file operations for this driver
- `data`: driver-specific global state

### Registering Char Devices

After filesystem mount registers devfs, boot initializes each character driver
and registers it with `register_char_driver`. Registration places the driver in
the fixed major-number table. Device nodes can then be created dynamically under
`/dev` without creating persistent disk dirents.

### Creating new instances of Char Devices

`devfs_create_char_device` creates a devfs entry for a given `rdev` major/minor
pair. It fetches the registered driver, creates a virtual `CHAR_DRIVER_TYPE`
node such as `/dev/tty0`, and assigns the registered fops to the generated
inode.

Per-instance metadata can live in the driver's `data` field, typically as a
driver-owned structure containing an array indexed by minor number.

### More Info

Specific TTY, UART, framebuffer-terminal, pipe, and fan behavior is documented
in [device-drivers.md](device-drivers.md).

## Permission Handling

Each inode stores permission bits for read, write, and execute. `k_open`
validates requested access against the inode permissions before creating the OFT
entry. The live open mode is then stored in the OFT entry, so later `read` and
`write` calls can be checked against descriptor permissions without repeating
path lookup.

This two-level check mirrors the useful Unix distinction between file
permissions and open-file mode: the inode says what access is allowed in
general, while the OFT entry records what this specific descriptor was opened
to do.

## Error Handling

Filesystem internals return `err_t` values for precise filesystem failures.
Syscall-facing wrappers convert these internal errors into Unix-style negative
`SYS_E*` values before returning to userspace. This keeps low-level filesystem
diagnostics specific while preserving a simple errno-style API for user
programs.

Userspace commands report those failures through `print_errno`, which writes a
human-readable message to file descriptor 2. For example, creating an existing
path reports `file exists`, trying to read a directory reports `is a directory`,
and trying to use a regular file as a directory reports `not a directory`.

See [`errors.h`](../../kernel/fs/errors.h) and
[`errors.c`](../../kernel/fs/errors.c).

## Design Tradeoffs and Limits

- The filesystem is ext2-inspired rather than fully ext2-compatible. This is done
  as this is a tool for learning and exploring how a filesystem is made from the
  ground up with our own design optimizations and simplification decisions.
- There is no journaling, so crash recovery is limited. We optimized for software
  efficiency with caching, but not recovery or driver optimization.
- The VFS supports root virtual mounts such as `/proc` and `/dev`, but not a
  full Unix mount namespace.
- Cache sizes and replacement policies favor simplicity and readability over
  adaptive production tuning.
- Permissions model Unix-style access bits, but not full users/groups/ACL
  behavior.

These tradeoffs are deliberate. The filesystem is large enough to support real
Unix-style workflows such as `/bin` execution, descriptor inheritance, pipes,
devices, `/proc`, and persistent files, while staying small enough to trace from
a shell command down to the block device.
