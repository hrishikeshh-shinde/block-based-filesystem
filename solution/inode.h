#ifndef INODE_H
#define INODE_H

#include "wfs.h"
#include <stddef.h>

void read_inode(struct wfs_inode *inode, size_t inode_index);
void write_inode(const struct wfs_inode *inode, size_t inode_index);
void read_inode_bitmap(char *inode_bitmap);
void write_inode_bitmap(const char *inode_bitmap);
int allocate_free_inode();
int find_dentry_in_inode(int parent_inode_num, const char *name);
int get_inode_index(const char *path);

#endif