#define FUSE_USE_VERSION 30

#include "utility.h"
#include "wfs.h"
#include <errno.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#define RAID_0 0
#define RAID_1 1
#define RAID_2 2
#define INODE_OFFSET(i) (sb.i_blocks_ptr + (i)*BLOCK_SIZE)
#define INODE_BITMAP_OFFSET sb.i_bitmap_ptr
#define DIRENTRY_OFFSET(block, i) (sb.d_blocks_ptr + (block)*BLOCK_SIZE + (i)*sizeof(struct wfs_dentry))
#define DATA_BLOCK_OFFSET(i) (sb.d_blocks_ptr + (i)*BLOCK_SIZE)
#define DATA_BITMAP_OFFSET sb.d_bitmap_ptr

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

struct global_mmap {
  void **disk_mmaps;
  int num_disks;
  size_t *disk_sizes;
};

struct global_mmap global_mmap;
struct wfs_sb sb;

//List of Functions declaration so that error doesnt occur:
void load_inode(struct wfs_inode *inode, size_t inode_index);
void write_inode(const struct wfs_inode *inode, size_t inode_index);
void load_inode_bitmap(char *inode_bitmap);
void write_inode_bitmap(const char *inode_bitmap);
int get_free_inode();
int find_dir_entry_in_inode(int parent_inode_num, const char *name);
int delete_directory_entry(int parent_inode_num, const char *name);
int get_inode_index(const char *path);
int setup_inode(mode_t mode, mode_t type_flag);
void free_inode(int inode_index);
int calculate_raid_disk(int* disk_index, int block_index);
void synchronize_disks(const void *block, size_t block_offset, size_t block_size, int primary_disk_index);
void initialize_raid(void **disk_mmaps, int num_disks, int raid_mode, size_t *disk_sizes);
void read_data_block(void *block, size_t block_index);
void set_indirect_block(int block_num);
void write_data_block(const void *block, size_t block_index);
int get_data_block();
void clear_data_block(int block_index);
int find_duplicate_directory_entry(const struct wfs_inode *parent_inode,const char *dirname);
int insert_directory_entry(struct wfs_inode *parent_inode, int parent_inode_num,const char *dirname, int inode_num);
void find_majority_block(void *block, int block_index);

//Operations related to data-blocks:
//To compute for raid1v:
void find_majority_block(void *block, int block_index) {
    int total_disks = global_mmap.num_disks;
    int primary_disk_idx, offset_within_disk;
    offset_within_disk = calculate_raid_disk(&primary_disk_idx, block_index);

    void **disk_data_blocks = malloc(total_disks * sizeof(void *));
    int *vote_counts = calloc(total_disks, sizeof(int));

    for (int disk = 0; disk < total_disks; disk++) {
        disk_data_blocks[disk] = malloc(BLOCK_SIZE);
        char *data_location = (char *)global_mmap.disk_mmaps[disk] + DATA_BLOCK_OFFSET(offset_within_disk);
        memcpy(disk_data_blocks[disk], data_location, BLOCK_SIZE);
    }

    for (int current = 0; current < total_disks; current++) {
        for (int compare = 0; compare < total_disks; compare++) {
            if (current != compare && memcmp(disk_data_blocks[current], disk_data_blocks[compare], BLOCK_SIZE) == 0) {
                vote_counts[current]++;
            }
        }
    }

    int chosen_disk = -1;
    int highest_votes = -1;
    for (int disk = 0; disk < total_disks; disk++) {
        if (vote_counts[disk] > highest_votes) {
            highest_votes = vote_counts[disk];
            chosen_disk = disk;
        }
    }

    char *correct_data = (char *)global_mmap.disk_mmaps[chosen_disk] + DATA_BLOCK_OFFSET(offset_within_disk);
    memcpy(block, correct_data, BLOCK_SIZE);

    for (int disk = 0; disk < total_disks; disk++) {
        free(disk_data_blocks[disk]);
    }
    free(disk_data_blocks);
    free(vote_counts);
}

//Reading a data block
void read_data_block(void *block, size_t block_index) {
    int primary_disk_idx;
    int local_block_idx = calculate_raid_disk(&primary_disk_idx, block_index);

    if (primary_disk_idx < 0) {
        return;
    }

    size_t offset = DATA_BLOCK_OFFSET(local_block_idx);
    char *source_data = (char *)global_mmap.disk_mmaps[primary_disk_idx];
    memcpy(block, source_data + offset, BLOCK_SIZE);
}

//Writing a data block
void write_data_block(const void *block, size_t block_index) {
    int target_disk_idx;
    int local_block_idx = calculate_raid_disk(&target_disk_idx, block_index);

    if (target_disk_idx < 0) {
        return;
    }

    size_t offset = DATA_BLOCK_OFFSET(local_block_idx);
    char *target_mmap = (char *)global_mmap.disk_mmaps[target_disk_idx];
    memcpy(target_mmap + offset, block, BLOCK_SIZE);

    if (sb.raid_mode == RAID_1) {
        synchronize_disks(block, offset, BLOCK_SIZE, target_disk_idx);
    }
}

//For indirect block:
void set_indirect_block(int block_num) {
    int disk_index;
    int block_index_within_disk = calculate_raid_disk(&disk_index, block_num);
    memset((char *)global_mmap.disk_mmaps[disk_index] + DATA_BLOCK_OFFSET(block_index_within_disk), -1, BLOCK_SIZE);
}

//Read the bitmap for datablock:
void read_data_block_bitmap(int disk_index, char *data_block_bitmap) {
    size_t bitmap_size = (sb.num_data_blocks + 7) / 8;
    char *mapped_disk = (char *)global_mmap.disk_mmaps[disk_index];
    memcpy(data_block_bitmap, mapped_disk + DATA_BITMAP_OFFSET, bitmap_size);
}

//Write the bitmap for datablock:
void write_data_block_bitmap(int disk_idx, const char *bitmap_data) {
    size_t bitmap_length = (sb.num_data_blocks + 7) / 8;
    
    char *disk_memory = (char *)global_mmap.disk_mmaps[disk_idx];
    memcpy(disk_memory + DATA_BITMAP_OFFSET, bitmap_data, bitmap_length);

    if (sb.raid_mode == RAID_1) {
        synchronize_disks(bitmap_data, DATA_BITMAP_OFFSET, bitmap_length, disk_idx);
    }
}

//Get free data block
int get_data_block() {
    size_t bitmap_size = (sb.num_data_blocks + 7) / 8;
    char block_bitmap[bitmap_size];

    for (int block = 0; block < sb.num_data_blocks; block++) {
        for (int disk = 0; disk < global_mmap.num_disks; disk++) {
            read_data_block_bitmap(disk, block_bitmap);
            if (!(block_bitmap[block / 8] & (1 << (block % 8)))) {
                block_bitmap[block / 8] |= (1 << (block % 8));
                write_data_block_bitmap(disk, block_bitmap);
                return block * global_mmap.num_disks + disk;
            }
        }
    }

    return -ENOSPC;
}

//Free the data block:
void clear_data_block(int index) {
    if (index < 0 || index >= sb.num_data_blocks) {
        return;
    }

    char block_bitmap[(sb.num_data_blocks + 7) / 8];
    int disk_idx;
    index = calculate_raid_disk(&disk_idx, index);
    read_data_block_bitmap(disk_idx, block_bitmap);

    block_bitmap[index / 8] &= ~(1 << (index % 8));
    write_data_block_bitmap(disk_idx, block_bitmap);
}

//Add the directory entry inside the parent
int insert_directory_entry(struct wfs_inode *dir_inode, int dir_inode_num, const char *entry_name, int file_inode_num) {
    int block_num = -1;
    for (int i = 0; i < N_BLOCKS; i++) {
        if (dir_inode->blocks[i] == -1) {
            block_num = get_data_block();
            if (block_num < 0) {
                return block_num;
            }
            dir_inode->blocks[i] = block_num;
            struct wfs_dentry new_entry[BLOCK_SIZE / sizeof(struct wfs_dentry)];
            memset(new_entry, -1, sizeof(new_entry));
            new_entry[0].num = file_inode_num;
            strncpy(new_entry[0].name, entry_name, MAX_NAME);

            write_data_block(new_entry, block_num);
            write_inode(dir_inode, dir_inode_num);
            return 0;
        }

        int disk_idx;
        int block_offset = calculate_raid_disk(&disk_idx, dir_inode->blocks[i]);
        if (disk_idx < 0) {
            return -EIO;
        }

        struct wfs_dentry *dir_block = (struct wfs_dentry *)((char *)global_mmap.disk_mmaps[disk_idx] + DATA_BLOCK_OFFSET(block_offset));
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            if (dir_block[j].num == -1) {
                dir_block[j].num = file_inode_num;
                strncpy(dir_block[j].name, entry_name, MAX_NAME);

                write_data_block(dir_block, dir_inode->blocks[i]);
                write_inode(dir_inode, dir_inode_num);
                return 0;
            }
        }
    }
    return -ENOSPC;
}

//Find if directory entry already exists
int find_duplicate_directory_entry(const struct wfs_inode *dir_inode, const char *entry_name) {
    struct wfs_dentry *entry;

    for (int i = 0; i < N_BLOCKS && dir_inode->blocks[i] != -1; i++) {
        int disk_idx;
        int block_idx_within_disk = calculate_raid_disk(&disk_idx, dir_inode->blocks[i]);
        if (disk_idx < 0) {
            return -EIO;
        }

        size_t block_offset = DATA_BLOCK_OFFSET(block_idx_within_disk);
        entry = (struct wfs_dentry *)((char *)global_mmap.disk_mmaps[disk_idx] + block_offset);

        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            if (entry[j].num != -1 && strcmp(entry[j].name, entry_name) == 0) {
                return 0;
            }
        }
    }
    return -ENOENT;
}

//Operations related to inode:

//Get the next available inode:
int get_free_inode() {
  size_t bitmap_size = (sb.num_inodes + 7) / 8;
  char inode_bitmap[bitmap_size];
  load_inode_bitmap(inode_bitmap);
  for (int i = 0; i < sb.num_inodes; i++) {
      if (!(inode_bitmap[i / 8] & (1 << (i % 8)))) {
          inode_bitmap[i / 8] |= (1 << (i % 8));
          write_inode_bitmap(inode_bitmap);
          return i;
      }
  }
  return -ENOSPC;
}

//Initialise the inode
void load_inode(struct wfs_inode *inode, size_t index) {
    size_t position = INODE_OFFSET(index);
    void *mapped_region = (void *)((char *)global_mmap.disk_mmaps[0] + position);
    memcpy(inode, mapped_region, sizeof(struct wfs_inode));
}

//Write inode
void write_inode(const struct wfs_inode *inode, size_t inode_index) {
  off_t offset = INODE_OFFSET(inode_index);
  int disk_index = 0;
  void *inode_offset = (char *)global_mmap.disk_mmaps[disk_index] + offset;
  memcpy(inode_offset, inode, sizeof(struct wfs_inode));

  
  synchronize_disks(inode, offset, sizeof(struct wfs_inode), 0);
}

//Load inode bitmap
void load_inode_bitmap(char *inode_bitmap) {
  size_t inode_bitmap_size = (sb.num_inodes + 7) / 8;
  int disk_index = 0;

  memcpy(inode_bitmap,
         (char *)global_mmap.disk_mmaps[disk_index] + INODE_BITMAP_OFFSET,
         inode_bitmap_size);
}

//Write inode bitmap
void write_inode_bitmap(const char *inode_bitmap) {
  size_t inode_bitmap_size = (sb.num_inodes + 7) / 8;
  int disk_index = 0;
  memcpy((char *)global_mmap.disk_mmaps[disk_index] + INODE_BITMAP_OFFSET, inode_bitmap, inode_bitmap_size);
  synchronize_disks(inode_bitmap, INODE_BITMAP_OFFSET, inode_bitmap_size, 0);
}

//Initialise inode
int setup_inode(mode_t mode, mode_t type_flag) {
  int inode_num = get_free_inode();
  if (inode_num < 0) {
    return inode_num;
  }

  struct wfs_inode new_inode = {
      .num = inode_num,
      .mode = mode | type_flag,
      .nlinks = (type_flag == S_IFDIR) ? 2 : 1,
      .size = 0,
      .uid = getuid(),
      .gid = getgid(),
      .atim = time(NULL),
      .mtim = time(NULL),
      .ctim = time(NULL),
  };

  for (int i = 0; i < N_BLOCKS; i++) {
    new_inode.blocks[i] = -1;
  }

  write_inode(&new_inode, inode_num);
  return inode_num;
}

//Clear the inode
void free_inode(int inode_index) {
  size_t inode_bitmap_size = (sb.num_inodes + 7) / 8;
  char inode_bitmap[inode_bitmap_size];

  load_inode_bitmap(inode_bitmap);

  inode_bitmap[inode_index / 8] &= ~(1 << (inode_index % 8));
  write_inode_bitmap(inode_bitmap);
}

//Check the directory inside the inode
int find_dir_entry_in_inode(int parent_inode_num, const char *name) {
  struct wfs_inode parent_inode;
  load_inode(&parent_inode, parent_inode_num);

  for (int i = 0; i < N_BLOCKS; i++) {
    if (parent_inode.blocks[i] == -1) {
      continue;
    }

    size_t num_entries = BLOCK_SIZE / sizeof(struct wfs_dentry);

    for (size_t j = 0; j < num_entries; j++) {
      struct wfs_dentry entry;
      int disk_index;
      int block_index_within_disk = calculate_raid_disk(&disk_index, parent_inode.blocks[i]);
      off_t offset = DIRENTRY_OFFSET(block_index_within_disk, j);
      memcpy(&entry, (char *)global_mmap.disk_mmaps[disk_index] + offset, sizeof(struct wfs_dentry));

      if (entry.num != -1){
        if (strcmp(entry.name, name) == 0) {
          return entry.num;
        }
      }
    }
  }
  return -ENOENT;
}

//Remove dir entry
int delete_directory_entry(int parent_inode_id, const char *entry_name) {
    struct wfs_inode parent_node;
    load_inode(&parent_node, parent_inode_id);

    for (int block_idx = 0; block_idx < N_BLOCKS; block_idx++) {
        if (parent_node.blocks[block_idx] == -1) {
            continue;
        }

        size_t entries_per_block = BLOCK_SIZE / sizeof(struct wfs_dentry);

        for (size_t entry_idx = 0; entry_idx < entries_per_block; entry_idx++) {
            struct wfs_dentry current_entry;
            off_t entry_offset = DIRENTRY_OFFSET(parent_node.blocks[block_idx], entry_idx);
            int raid_disk_id;
            calculate_raid_disk(&raid_disk_id, parent_node.blocks[block_idx]);

            char *disk_memory = (char *)global_mmap.disk_mmaps[0];
            memcpy(&current_entry, disk_memory + entry_offset, sizeof(struct wfs_dentry));

            if (strcmp(current_entry.name, entry_name) == 0) {
                memset(&current_entry, -1, sizeof(struct wfs_dentry));
                memcpy(disk_memory + entry_offset, &current_entry, sizeof(struct wfs_dentry));

                if (sb.raid_mode == RAID_1) {
                    synchronize_disks(&current_entry, entry_offset, sizeof(struct wfs_dentry), 0);
                }
                return 0;
            }
        }
    }

    printf("Entry not found in directory: %s\n", entry_name);
    return -ENOENT;
}


//Find the index of inode
int get_inode_index(const char *path) {
  if (strcmp(path, "/") == 0) {
    return 0;
  }

  char *path_copy = strdup(path);
  char *component = strtok(path_copy, "/");
  int parent_inode_num = 0;
  int result = 0;

  while (component != NULL) {
    result = find_dir_entry_in_inode(parent_inode_num, component);
    if (result < 0) {
      free(path_copy);
      return result;
    }

    parent_inode_num = result;
    component = strtok(NULL, "/");
  }

  free(path_copy);
  return parent_inode_num;
}

//Operations related to raid:

//Find which disk belongs to:
int calculate_raid_disk(int *disk_id, int block_id) {
    if (sb.raid_mode == RAID_0) {
        *disk_id = block_id % global_mmap.num_disks;
    } else {
        *disk_id = 0;
    }
    return block_id / global_mmap.num_disks; //re-explain
}

//Replicate the disks for making raid1
void synchronize_disks(const void *data, size_t offset, size_t size, int main_disk_id) {
    for (int disk_id = 0; disk_id < global_mmap.num_disks; disk_id++) {
        if (disk_id == main_disk_id) {
            continue;
        }

        char *replica_mmap = (char *)global_mmap.disk_mmaps[disk_id];
        if (!replica_mmap) {
            continue;
        }

        memcpy(replica_mmap + offset, data, size);
    }
}

//Initialise our global mmap for raid
void initialize_raid(void **disk_mmaps, int num_disks, int raid_mode, size_t *disk_sizes) {
  global_mmap.disk_mmaps = disk_mmaps;
  global_mmap.num_disks = num_disks;
  global_mmap.disk_sizes = disk_sizes;
  sb.raid_mode = raid_mode;
}


//Fuse operations:
int wfs_mknod(const char *path, mode_t mode, dev_t dev) {
  char parent_path[PATH_MAX];
  char filename[MAX_NAME];
  split_path(path, parent_path, filename);

  int parent_inode_num = get_inode_index(parent_path);
  if (parent_inode_num == -ENOENT) {
    return -ENOENT;
  }

  struct wfs_inode parent_inode;
  load_inode(&parent_inode, parent_inode_num);

  if (!S_ISDIR(parent_inode.mode)) {
    return -ENOTDIR;
  }

  if (find_duplicate_directory_entry(&parent_inode, filename) == 0) {
    return -EEXIST;
  }

  int inode_num = setup_inode(mode, S_IFREG);
  if (inode_num < 0) {
    return inode_num;
  }

  if (insert_directory_entry(&parent_inode, parent_inode_num, filename,inode_num) < 0) {
    return -EIO;
  }
  return 0;
}

int wfs_mkdir(const char *path, mode_t mode) {
  char parent_path[PATH_MAX];
  char dirname[MAX_NAME];
  split_path(path, parent_path, dirname);

  int parent_inode_num = get_inode_index(parent_path);
  if (parent_inode_num == -ENOENT) {
    return -ENOENT;
  }

  struct wfs_inode parent_inode;
  load_inode(&parent_inode, parent_inode_num);

  if (!S_ISDIR(parent_inode.mode)) {
    return -ENOTDIR;
  }

  if (find_duplicate_directory_entry(&parent_inode, dirname) == 0) {
    return -EEXIST;
  }

  int inode_num = setup_inode(mode, S_IFDIR);
  if (inode_num < 0) {
    return inode_num;
  }

  if (insert_directory_entry(&parent_inode, parent_inode_num, dirname,inode_num) < 0) {
    return -EIO;
  }

  return 0;
}

int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
  (void)offset;
  (void)fi;

  int inode_num = get_inode_index(path);
  if (inode_num == -ENOENT) {
    return -ENOENT;
  }

  struct wfs_inode dir_inode;
  load_inode(&dir_inode, inode_num);

  if (!S_ISDIR(dir_inode.mode)) {
    return -ENOTDIR;
  }

  for (int i = 0; i < N_BLOCKS && dir_inode.blocks[i] != -1; i++) {
    int disk_index;
    int block_index_within_disk = calculate_raid_disk(&disk_index, dir_inode.blocks[i]);
    if (disk_index < 0) {
      return -EIO;
    }

    size_t block_offset = DATA_BLOCK_OFFSET(block_index_within_disk);

    struct wfs_dentry *dentry =
        (struct wfs_dentry *)((char *)global_mmap.disk_mmaps[disk_index] +
                              block_offset);
    for (size_t entry_idx = 0;
         entry_idx < BLOCK_SIZE / sizeof(struct wfs_dentry); entry_idx++) {
      if (dentry[entry_idx].num == -1) {
        continue;
      }
      filler(buf, dentry[entry_idx].name, NULL, 0);
    }
  }
  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);

  fflush(stdout);

  return 0;
}

int wfs_getattr(const char *path, struct stat *stbuf) {

  int inode_num = get_inode_index(path);
  if (inode_num == -ENOENT) {
    return -ENOENT;
  }
  struct wfs_inode inode;
  load_inode(&inode, inode_num);

  memset(stbuf, 0, sizeof(struct stat));
  stbuf->st_mode = inode.mode;
  stbuf->st_nlink = inode.nlinks;
  stbuf->st_size = inode.size;
  stbuf->st_atime = inode.atim;
  stbuf->st_mtime = inode.mtim;
  stbuf->st_ctime = inode.ctim;

  fflush(stdout); 
  return 0;
}

int write_to_data_block(int block_num, const char *buf, size_t size, size_t offset) {
    int disk_index;
    int block_index_within_disk = calculate_raid_disk(&disk_index, block_num);
    if (disk_index < 0) return -EIO;

    char *block_start = (char *)global_mmap.disk_mmaps[disk_index] + DATA_BLOCK_OFFSET(block_index_within_disk);
    
    memcpy(block_start + offset, buf, size);
    if (sb.raid_mode == RAID_1){
      synchronize_disks(buf, DATA_BLOCK_OFFSET(block_index_within_disk) + offset, size, disk_index); 
    }
    return size;
}

int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    int inode_num = get_inode_index(path);
    if (inode_num == -ENOENT) {
        return -ENOENT;
    }

    struct wfs_inode file_inode;
    load_inode(&file_inode, inode_num);

    if (!S_ISREG(file_inode.mode)) {
        return -EISDIR;
    }

    size_t bytes_written = 0;

    while (bytes_written < size) {
        int block_index = (offset + bytes_written) / BLOCK_SIZE;
        size_t block_offset = (offset + bytes_written) % BLOCK_SIZE;

        if (block_index >= N_BLOCKS-1){
          break;
        }

        if (file_inode.blocks[block_index] == -1) {
            int new_block = get_data_block(&file_inode);
            if (new_block < 0) {
                return new_block;
            }
            file_inode.blocks[block_index] = new_block;
        }

        int block_num = file_inode.blocks[block_index];
        size_t write_size = MIN(BLOCK_SIZE - block_offset, size - bytes_written);

        int result = write_to_data_block(block_num, buf + bytes_written, write_size, block_offset);
        if (result < 0) {
            return result;
        }

        bytes_written += result;
    }


    if (bytes_written < size && (offset + bytes_written) / BLOCK_SIZE >= N_BLOCKS-1){

      if (file_inode.blocks[N_BLOCKS - 1] == -1) {
          int new_indirect_block_num = get_data_block(&file_inode);
          if (new_indirect_block_num < 0) {
              return new_indirect_block_num;
          }
          file_inode.blocks[N_BLOCKS - 1] = new_indirect_block_num;
          set_indirect_block(new_indirect_block_num);
      }
      off_t indirect_block[(BLOCK_SIZE / sizeof(off_t))];
      read_data_block(indirect_block, file_inode.blocks[N_BLOCKS - 1]);

      while (bytes_written < size) {
          int block_index = (offset + bytes_written) / BLOCK_SIZE;
          size_t block_offset = (offset + bytes_written) % BLOCK_SIZE;
          int block_index_within_indirect_block = block_index - N_BLOCKS + 1;
          if (indirect_block[block_index_within_indirect_block] == -1) {
              int new_block = get_data_block(&file_inode);
              if (new_block < 0) {
                  return new_block;
              }
              indirect_block[block_index_within_indirect_block] = new_block;
              write_data_block(indirect_block, file_inode.blocks[N_BLOCKS - 1]);
          }

          int block_num = indirect_block[block_index_within_indirect_block];
          size_t write_size = MIN(BLOCK_SIZE - block_offset, size - bytes_written);

          int result = write_to_data_block(block_num, buf + bytes_written, write_size, block_offset);
          if (result < 0) {
              return result;
          }

          bytes_written += result;
      }
    }

    file_inode.size = MAX(file_inode.size, offset + bytes_written);
    write_inode(&file_inode, inode_num);
    return bytes_written;
}

int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    int inode_num = get_inode_index(path);
    if (inode_num == -ENOENT) {
        return -ENOENT;
    }

    struct wfs_inode file_inode;
    load_inode(&file_inode, inode_num);

    if (!S_ISREG(file_inode.mode)) {
        return -EISDIR;
    }

    size = MIN(size, file_inode.size - offset);

    size_t bytes_read = 0;

    while (bytes_read < size) {
        int block_index = (offset + bytes_read) / BLOCK_SIZE;
        size_t block_offset = (offset + bytes_read) % BLOCK_SIZE;

        if (block_index >= N_BLOCKS-1){
          break;
        }

        int block_num = file_inode.blocks[block_index];
        size_t read_size = MIN(BLOCK_SIZE - block_offset, size - bytes_read);

        if (sb.raid_mode == RAID_2){
          char *block = malloc(BLOCK_SIZE);
          find_majority_block(block, block_num);
          memcpy(buf + bytes_read, block + block_offset, read_size);
        }
        else{
            int disk_index;
            int block_index_within_disk = calculate_raid_disk(&disk_index, block_num);
            if (disk_index < 0) {
                return -EIO;
            }

            char *block_start = (char *)global_mmap.disk_mmaps[disk_index] + DATA_BLOCK_OFFSET(block_index_within_disk);
            memcpy(buf + bytes_read, block_start + block_offset, read_size);
        }
        bytes_read += read_size;
    }

    if (bytes_read < size && (offset + bytes_read) / BLOCK_SIZE >= N_BLOCKS-1){
      off_t indirect_block[BLOCK_SIZE / sizeof(off_t)];
      read_data_block(indirect_block, file_inode.blocks[N_BLOCKS - 1]);

      while (bytes_read < size) {
          int block_index = (offset + bytes_read) / BLOCK_SIZE;
          size_t block_offset = (offset + bytes_read) % BLOCK_SIZE;
          int block_index_within_indirect_block = block_index - N_BLOCKS + 1;

          int block_num = indirect_block[block_index_within_indirect_block];
          size_t read_size = MIN(BLOCK_SIZE - block_offset, size - bytes_read);

          int disk_index;
          int block_index_within_disk = calculate_raid_disk(&disk_index, block_num);
          if (disk_index < 0) {
              return -EIO;
          }

          char *block_start = (char *)global_mmap.disk_mmaps[disk_index] + DATA_BLOCK_OFFSET(block_index_within_disk);
          memcpy(buf + bytes_read, block_start + block_offset, read_size);

          bytes_read += read_size;
      }
    }
    return bytes_read;
}

int wfs_rmdir(const char *path) {
  int inode_num = get_inode_index(path);
  if (inode_num == -ENOENT) {
    return -ENOENT;
  }

  struct wfs_inode dir_inode;
  load_inode(&dir_inode, inode_num);

  if (!S_ISDIR(dir_inode.mode)) {
    return -ENOTDIR;
  }

  char parent_path[100];
  char dir_name[MAX_NAME];
  if (split_path(path, parent_path, dir_name) == -1) {
    return -EINVAL;
  }

  int parent_inode_num = get_inode_index(parent_path);
  if (parent_inode_num == -ENOENT) {
    return -ENOENT;
  }

  if (delete_directory_entry(parent_inode_num, dir_name) != 0) {
    return -EIO;
  }

  free_inode(inode_num);

  fflush(stdout);
  return 0;
}

int wfs_unlink(const char *path) {

    int inode_num = get_inode_index(path);
    if (inode_num == -ENOENT) {
        return -ENOENT;
    }

    struct wfs_inode file_inode;
    load_inode(&file_inode, inode_num);

    if (!S_ISREG(file_inode.mode)) {
        return -EISDIR;
    }

    for (int i = 0; i < N_BLOCKS; i++) {
        if (i == N_BLOCKS-1 && file_inode.blocks[i] != -1){
            off_t *indirect_block = malloc(BLOCK_SIZE / sizeof(off_t));
            read_data_block(indirect_block, file_inode.blocks[N_BLOCKS - 1]);
            for (off_t j=0; j<BLOCK_SIZE/sizeof(off_t); j++){
              if (indirect_block[j] != -1){
                clear_data_block(indirect_block[j]);
                indirect_block[j] = -1;
              }
            }
            write_data_block(indirect_block, file_inode.blocks[N_BLOCKS - 1]);
        }
        if (file_inode.blocks[i] != -1) {
            clear_data_block(file_inode.blocks[i]);
            file_inode.blocks[i] = -1;
        }
    }

    memset(&file_inode, -1, sizeof(file_inode));
    write_inode(&file_inode, inode_num);
    free_inode(inode_num);

    char parent_path[100];
    char dir_name[MAX_NAME];
    if (split_path(path, parent_path, dir_name) == -1) {
      return -EINVAL;
    }

    int parent_inode_num = get_inode_index(parent_path);
    if (parent_inode_num == -ENOENT) {
      return -ENOENT;
    }

    if (delete_directory_entry(parent_inode_num, dir_name) != 0) {
      return -EIO;
    }

    return 0;
}


//Fuse ops as mentioned in Readme.md:
struct fuse_operations ops = {
  .getattr = wfs_getattr,
  .mknod   = wfs_mknod,
  .mkdir   = wfs_mkdir,
  .unlink  = wfs_unlink,
  .rmdir   = wfs_rmdir,
  .read    = wfs_read,
  .write   = wfs_write,
  .readdir = wfs_readdir,
};