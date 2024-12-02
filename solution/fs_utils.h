#ifndef FS_UTILS_H
#define FS_UTILS_H

#include "wfs.h"
#include <stddef.h>

size_t calc_size(size_t num_inodes, size_t num_data_blocks);
int initialize_disk(const char* disk, size_t num_inodes, size_t num_data_blocks,
                    size_t required_size, int raid_mode, int disk_index, int num_disks);

#endif // FS_UTILS_H