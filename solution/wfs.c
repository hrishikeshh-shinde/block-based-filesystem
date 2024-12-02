#define FUSE_USE_VERSION 30

#include "wfs.h"
#include "data_block.h"
#include "fs_utils.h"
#include "fuse.h"
#include "globals.h"
#include "inode.h"
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

//Print contenets of superblock
void print_superblock() {
  printf("Superblock Contents:\n");
  printf("  Total Blocks: %ld\n", sb.num_data_blocks);
  printf("  Inode Count: %ld\n", sb.num_inodes);
  printf("  Data Blocks Pointer: %ld\n", sb.d_blocks_ptr);
  printf("  Inode Blocks Pointer: %ld\n", sb.i_blocks_ptr);
  printf("  Inode Bitmap Pointer: %ld\n", sb.i_bitmap_ptr);
  printf("  Data Bitmap Pointer: %ld\n", sb.d_bitmap_ptr);
}

//Function to bring file contents into mmap
int open_disk(const char* path){
    printf("Opening Disk:%s\n", path);
    int disk_fd = open(path, O_RDWR);
    if(disk_fd == -1){
        perror("Failed to open disk");
        return -1;
    }

    printf("Reading Superblock...\n");
    if(read(disk_fd, &sb, sizeof(struct wfs_sb)) != sizeof(struct wfs_sb)){
        perror("Failed to read superblock\n");
        close(disk_fd);
        return -1;
    }

    int disk_size = calc_size(sb.num_inodes, sb.num_data_blocks);

    //mmap the disk
    disk_mmap = mmap(NULL, disk_size, PROT_READ | PROT_WRITE, MAP_SHARED, disk_fd, 0);
    if(disk_mmap == MAP_FAILED){
        perror("Failed to mmap disk\n");
        close(disk_fd);
        return -1;
    }

    close(disk_fd);

    printf("Superblock read successfully!\n");
    print_superblock();

    return 0;
}

int wfs_getattr(const char *path, struct stat *stbuf) {
  printf("Entering wfs_getattr: path = %s\n", path);

  int inode_num = get_inode_index(path);
  if (inode_num == -ENOENT) {
    printf("File not found: %s\n", path);
    return -ENOENT;
  }

  printf("Found inode for %s: %d\n", path, inode_num);

  struct wfs_inode inode;

  read_inode(&inode, inode_num);

  printf("Inode read: mode = %o, size = %ld, nlinks = %d\n", inode.mode,
         inode.size, inode.nlinks);

  memset(stbuf, 0, sizeof(struct stat));
  stbuf->st_mode = inode.mode;
  stbuf->st_nlink = inode.nlinks;
  stbuf->st_size = inode.size;
  stbuf->st_atime = inode.atim;
  stbuf->st_mtime = inode.mtim;
  stbuf->st_ctime = inode.ctim;

  printf("Attributes populated for %s\n", path);

  fflush(stdout);
  return 0;
}

static struct fuse_operations ops = {
    .getattr = wfs_getattr,
};  

void print_arguments(int argc, char *argv[]) {
  printf("Arguments passed to the program:\n");
  for (int i = 0; i < argc; i++) {
    printf("  argv[%d]: %s\n", i, argv[i]);
  }
}


int main(int argc, char *argv[]) { 
    if(argc<4){
        fprintf(stderr, "Usage: %s disk1 disk2 [FUSE options] mount_point\n", argv[0]);
        return 1;
    }

    printf("Starting WFS with disk: %s\n", argv[1]);
    open_disk(argv[1]);

    printf("Initialising FUSE with mount point:%s\n", argv[argc-1]);
    print_arguments(argc - 2, argv + 2);
    return fuse_main(argc-2, argv+2, &ops, NULL);
}