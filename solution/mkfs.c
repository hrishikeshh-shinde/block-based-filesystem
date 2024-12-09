#include "utility.h"
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BLOCK_ALIGN(offset) (((offset) + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE)

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
                } else {
                    return 1;
                }
                i++;
            }
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

    size_t required_size = calc_size(num_inodes, num_data_blocks);
    for (int i = 0; i < num_disks; i++) {
        if (disk_initialize(disks[i], num_inodes, num_data_blocks,required_size, raid_mode, i, num_disks) != 0) {
            return -1;
        }
    }
    return 0;
}
