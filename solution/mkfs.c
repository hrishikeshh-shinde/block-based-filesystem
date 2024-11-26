#include <stdio.h>
#include <fcntl.h>
#include "wfs.h"

void write_superblock(int fd, size_t num_inodes, size_t num_data_blocks) {
    struct wfs_sb sb;
    sb.num_inodes = num_inodes;
    sb.num_data_blocks = num_data_blocks;

    // Calculate offsets for each structure in the filesystem
    sb.i_bitmap_ptr = sizeof(struct wfs_sb);
    sb.d_bitmap_ptr = sb.i_bitmap_ptr + ALIGN32(num_inodes) / 8;
    sb.i_blocks_ptr = sb.d_bitmap_ptr + ALIGN32(num_data_blocks) / 8;
    sb.d_blocks_ptr = sb.i_blocks_ptr + num_inodes * sizeof(struct wfs_inode);

    // Write the superblock
    write(fd, &sb, sizeof(sb));
}

void write_bitmap(int fd, size_t num_inodes, size_t num_data_blocks) {
    size_t i_bitmap_size = ALIGN32(num_inodes) / 8;
    size_t d_bitmap_size = ALIGN32(num_data_blocks) / 8;

    char *buf1 = calloc(1, i_bitmap_size);
    write(fd, buf1, i_bitmap_size);
    free(buf1);

    char *buf2 = calloc(1, d_bitmap_size);
    write(fd, buf2, d_bitmap_size);
    free(buf2);

}

void write_rootinode(){

}

int main(int argc, char* argv[]){
    int raid_mode = -1;
    int num_inodes = 0;
    int num_data_blocks = 0;
    int num_disks = 0;
    char* disks[MAX_DISKS];

    //parse the parameters passed in the input
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            raid_mode = atoi(argv[++i]); // Parse RAID mode
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            disks[num_disks++] = argv[++i]; // Add disk to list
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            num_inodes = atoi(argv[++i]); // Parse number of inodes
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            num_data_blocks = atoi(argv[++i]); // Parse number of blocks
            num_data_blocks = ROUND32(num_data_blocks);
        } else {
            return USAGE_ERROR;
        }
    }

    if(num_disks<2 || num_inodes<=0 || num_data_blocks<=0){
        return USAGE_ERROR;
    }

    //Initialise each disk
    for(int i=0; i<num_disks; i++){

        int fd = open(disks[i], O_RDWR | O_CREAT | O_TRUNC, 0644);
        if(fd==-1){
            return RUNTIME_ERROR;
        }

         // Validate disk size
        off_t required_size = sizeof(struct wfs_sb) +
                              ALIGN32(num_inodes) / 8 +
                              ALIGN32(num_data_blocks) / 8 +
                              num_inodes * sizeof(struct wfs_inode) +
                              num_data_blocks * BLOCK_SIZE;
        if( lseek(fd, 0, SEEK_END) < required_size ){
            return RUNTIME_ERROR;
        }

        lseek(fd, 0, SEEK_SET);

        //Write functions to initialise superblock, both bitmaps and root-inode
        write_superblock(fd, num_inodes, num_data_blocks);
        write_bitmap(fd, num_inodes, num_data_blocks);
        write_rootinode();
        
        //How, What changes for RAID0, RAID1, RAID10?

        close(fd);

    }
    return SUCCESS;
}
