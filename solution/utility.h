#ifndef FS_UTILS_H
#define FS_UTILS_H

#include "wfs.h"
#include <stddef.h>

size_t calc_size(size_t num_inodes, size_t num_data_blocks);
int disk_initialize(const char *disk_file, size_t inode_count, size_t data_block_count, size_t required_size,int raid_mode, int disk_index, int total_disks);
int split_path(const char *path, char *parent_path, char *dir_name);

#endif
