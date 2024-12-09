#include "wfs.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BLOCK_ALIGN(offset) (((offset) + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE)

size_t calc_size(size_t num_inodes, size_t num_data_blocks){
    size_t sb_size = sizeof(struct wfs_sb);
    size_t i_bitmap_size = (num_inodes + 7) / 8;
    size_t d_bitmap_size = (num_data_blocks + 7) / 8;
    size_t inodes_size = num_inodes * BLOCK_SIZE;
    size_t data_blocks_size = num_data_blocks * BLOCK_SIZE;

    size_t size = sb_size;

    size += i_bitmap_size;

    size += d_bitmap_size;

    size = ROUNDBLOCK(size);
    size += inodes_size;

    size = ROUNDBLOCK(size);
    size += data_blocks_size;

    return size;
}

struct wfs_sb write_superblock(int fd, size_t num_inodes, size_t num_data_blocks, int raid_mode, int disk_index, int num_disks) {
    size_t i_bitmap_size = (num_inodes + 7) / 8;
    size_t d_bitmap_size = (num_data_blocks + 7) / 8;
    size_t inodes_size = num_inodes * BLOCK_SIZE;
    __uint64_t disk_id = (__uint64_t)time(NULL) ^ (disk_index + 1) ^ rand();

    struct wfs_sb sb = {
        .num_inodes = num_inodes,
        .num_data_blocks = num_data_blocks,
        .i_bitmap_ptr = sizeof(struct wfs_sb),
        .d_bitmap_ptr = (sizeof(struct wfs_sb) + i_bitmap_size),
        .i_blocks_ptr = BLOCK_ALIGN(sizeof(struct wfs_sb) + i_bitmap_size + d_bitmap_size),
        .d_blocks_ptr = BLOCK_ALIGN(sizeof(struct wfs_sb) + i_bitmap_size + d_bitmap_size + inodes_size),
        .raid_mode = raid_mode,
        .total_disks = num_disks,
        .disk_index = disk_index,
        .disk_id = disk_id
    };
    lseek(fd, 0 , SEEK_SET);
    ssize_t bytes_written = write(fd, &sb, sizeof(struct wfs_sb));

    if(bytes_written != sizeof(struct wfs_sb)){
        perror("Failed to write superblock");
        exit(EXIT_FAILURE);
    }
    return sb;
}

void write_bitmap(int fd, size_t num_inodes, size_t num_data_blocks, struct wfs_sb *sb) {
    size_t i_bitmap_size = (num_inodes + 7) / 8;
    size_t d_bitmap_size = (num_data_blocks + 7) / 8;

    char *bitmap = calloc(1, i_bitmap_size);
    bitmap[0] |= 1;

    lseek(fd, sb->i_bitmap_ptr, SEEK_SET);
    ssize_t bytes = write(fd, bitmap, i_bitmap_size);
    if(bytes != (ssize_t)i_bitmap_size){
        perror("Failed to write inode bitmap");
        free(bitmap);
        exit(EXIT_FAILURE);
    }
    free(bitmap);

    bitmap = calloc(1, d_bitmap_size);
    lseek(fd, sb->d_bitmap_ptr, SEEK_SET);
    bytes = write(fd, bitmap, d_bitmap_size);
    if(bytes != (ssize_t)d_bitmap_size){
        perror("Failed to write data block bitmap");
        free(bitmap);
        exit(EXIT_FAILURE);
    }
    free(bitmap);

}

void write_inode_to_disk(int fd, struct wfs_inode *inode, size_t inode_index, struct wfs_sb *sb) {
  off_t inode_offset = sb->i_blocks_ptr + inode_index * BLOCK_SIZE;

  lseek(fd, inode_offset, SEEK_SET);
  write(fd, inode, sizeof(struct wfs_inode));
}

void write_rootinode(int fd, struct wfs_sb *sb) {
  struct wfs_inode root = {
      .num = 0,
      .mode = __S_IFDIR | 0755,
      .uid = getuid(),
      .gid = getgid(),
      .size = 0,
      .nlinks = 2,
      .atim = time(NULL),
      .mtim = time(NULL),
      .ctim = time(NULL),
  };

  //Initialise everything to -1 so that we know if this is used or not
  for (int i = 0; i < N_BLOCKS; i++) {
    root.blocks[i] = -1;
  }

  write_inode_to_disk(fd, &root, 0, sb);
}

int disk_initialize(const char* disk, size_t num_inodes, size_t num_data_blocks,
                    size_t required_size, int raid_mode, int disk_index, int num_disks) {

        int fd = open(disk, O_RDWR, 0644);
        if(fd<0){
            perror("Error opening disk file");
            return -1;
        }
        off_t disk_size = lseek(fd, 0, SEEK_END);
        if( disk_size < required_size ){
            close(fd);
            return -1;
        }

        lseek(fd, 0, SEEK_SET);
        struct wfs_sb sb = write_superblock(fd, num_inodes, num_data_blocks, raid_mode, disk_index, num_disks);
        write_bitmap(fd, num_inodes, num_data_blocks, &sb);
        write_rootinode(fd, &sb);
        

        close(fd);
        return 0;
}

int split_path(const char *path, char *parent_path, char *dir_name) {
  //Split the path and store in array
  const char *last_slash = strrchr(path, '/');
  if (last_slash == NULL || last_slash == path) {
    parent_path[0] = '/';
    parent_path[1] = '\0';
    strncpy(dir_name, last_slash + 1, MAX_NAME);
    return 0;
  }

  size_t parent_len = last_slash - path;
  strncpy(parent_path, path, parent_len);
  parent_path[parent_len] = '\0';
  strncpy(dir_name, last_slash + 1, MAX_NAME);
  return 0;
}

