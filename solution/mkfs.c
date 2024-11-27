#include "wfs.h"
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

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

struct wfs_sb write_superblock(int fd, size_t num_inodes, size_t num_data_blocks) {
    size_t i_bitmap_size = (num_inodes + 7) / 8;
    size_t d_bitmap_size = (num_data_blocks + 7) / 8;
    size_t inodes_size = num_inodes * BLOCK_SIZE;

    struct wfs_sb sb = {
        .num_inodes = num_inodes,
        .num_data_blocks = num_data_blocks,
        .i_bitmap_ptr = sizeof(struct wfs_sb),
        .d_bitmap_ptr = (sizeof(struct wfs_sb) + i_bitmap_size),
        .i_blocks_ptr = ROUNDBLOCK(sizeof(struct wfs_sb) + i_bitmap_size + d_bitmap_size),
        .d_blocks_ptr = ROUNDBLOCK(sizeof(struct wfs_sb) + i_bitmap_size + d_bitmap_size + inodes_size)
    };
    // Write the superblock
    lseek(fd, 0 , SEEK_SET);
    ssize_t bytes_written = write(fd, &sb, sizeof(struct wfs_sb));

    if(bytes_written != sizeof(struct wfs_sb)){
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
        free(bitmap);
        exit(EXIT_FAILURE);
    }
    free(bitmap);

    bitmap = calloc(1, d_bitmap_size);
    lseek(fd, sb->d_bitmap_ptr, SEEK_SET);
    bytes = write(fd, bitmap, d_bitmap_size);
    if(bytes != (ssize_t)d_bitmap_size){
        free(bitmap);
        exit(EXIT_FAILURE);
    }
    free(bitmap);

}

void write_inode(int fd, struct wfs_inode *inode, size_t inode_index, struct wfs_sb *sb) {
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

  write_inode(fd, &root, 0, sb);
}

int main(int argc, char* argv[]){   
    int raid_mode = -1;
    int num_inodes = 0;
    int num_data_blocks = 0;
    int num_disks = 0;
    char* disks[MAX_DISKS];

    //parse the parameters passed in the input
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0) {
            if(i + 1 < argc){
                if (strcmp(argv[i + 1], "0") == 0) {
                    raid_mode = 0;
                } else if (strcmp(argv[i + 1], "1") == 0) {
                    raid_mode = 1;
                } else if (strcmp(argv[i + 1], "1v") == 0) {
                    raid_mode = 2;
                }
            }
            else{
                return 1;
            }
            i++;
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            disks[num_disks++] = argv[++i]; 
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            num_inodes = atoi(argv[++i]); 
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            num_data_blocks = atoi(argv[++i]); 
        } else {
            return 1;
        }
    }

    if(raid_mode==-1 || num_disks<2 || num_inodes<=0 || num_data_blocks<=0){
        return 1;
    }

    num_inodes = (num_inodes+31) & ~31;
    num_data_blocks = (num_data_blocks+31) & ~31;


    //Initialise each disk
    for(int i=0; i<num_disks; i++){
        int fd = open(disks[i], O_RDWR, 0644);
        if(fd<0){
            return -1;
        }

         // Validate disk size
        off_t required_size = calc_size(num_inodes, num_data_blocks);
        off_t disk_size = lseek(fd, 0, SEEK_END);
        if( disk_size < required_size ){
            close(fd);
            return -1;
        }

        lseek(fd, 0, SEEK_SET);
        struct wfs_sb sb = write_superblock(fd, num_inodes, num_data_blocks);
        write_bitmap(fd, num_inodes, num_data_blocks, &sb);
        write_rootinode(fd, &sb);
        

        close(fd);

    }
    return 0;
}
