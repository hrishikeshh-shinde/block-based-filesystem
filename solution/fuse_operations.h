#ifndef FUSE_OPERATIONS_H
#define FUSE_OPERATIONS_H
extern struct fuse_operations ops;
extern struct global_mmap global_mmap;
extern struct wfs_sb sb;

#define RAID_0 0
#define RAID_1 1
#define RAID_2 2
#define INODE_OFFSET(i) (sb.i_blocks_ptr + (i)*BLOCK_SIZE)
#define INODE_BITMAP_OFFSET sb.i_bitmap_ptr
#define DIRENTRY_OFFSET(block, i) (sb.d_blocks_ptr + (block)*BLOCK_SIZE + (i)*sizeof(struct wfs_dentry))
#define DATA_BLOCK_OFFSET(i) (sb.d_blocks_ptr + (i)*BLOCK_SIZE)
#define DATA_BITMAP_OFFSET sb.d_bitmap_ptr


//List of Functions declaration:
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
void synchronize_disks(const void *block, size_t block_offset, size_t block_size,int primary_disk_index);
void initialize_raid(void **disk_mmaps, int num_disks, int raid_mode, size_t *disk_sizes);
void read_data_block(void *block, size_t block_index);
void set_indirect_block(int block_num);
void write_data_block(const void *block, size_t block_index);
int get_data_block();
void clear_data_block(int block_index);
int find_duplicate_directory_entry(const struct wfs_inode *parent_inode, const char *dirname);
int insert_directory_entry(struct wfs_inode *parent_inode, int parent_inode_num, const char *dirname, int inode_num);
void find_majority_block(void *block, int block_index);

#endif
