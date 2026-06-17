#pragma once

int fs_test_mkfs_metadata(int inode_table_blocks, int block_size_config);
int fs_test_bitmap_boundaries(void);
void fs_test_run_mkfs_smoke(void);
