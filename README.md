# Block-Based Filesystem with RAID using FUSE

This project is a user-level block-based filesystem developed using **FUSE** in C, implementing core features such as file creation, reading, writing, directory management, and RAID-level storage (RAID 0, RAID 1, RAID 1v). Designed as part of the Operating Systems curriculum (CS537), it demonstrates low-level filesystem logic including inode and block bitmap management, memory-mapped file I/O, and page-aligned metadata structures.

## Table of Contents

- [Overview](#overview)
- [Key Features](#key-features)
- [Usage](#usage)
- [Mounting Behavior](#mounting-behavior)
- [Error Handling](#error-handling)
- [Testing](#testing)
- [Structure](#structure)
- [Debugging Tips](#debugging-tips)
- [Further References](#further-references)

## Overview

The filesystem supports:

- Traditional block-based layout with inodes, bitmaps, and a superblock.
- RAID 0 (striping) and RAID 1 (mirroring) modes.
- Verified mirroring (RAID 1v) with majority-based read validation.
- Full integration with FUSE to support `mkdir`, `rmdir`, `read`, `write`, `unlink`, and more.

This project is composed of:

- `mkfs.c` – Initializes a new filesystem on given disk images.
- `wfs.c` – FUSE-based implementation of the virtual filesystem.
- `wfs.h` – Contains all the filesystem structure definitions.
- Utility scripts: `create_disk.sh`, `umount.sh`, `Makefile`.

## Key Features

- Block-aligned superblock, inode structures, and data blocks (512B each).
- RAID 0 and RAID 1 support with automatic metadata mirroring.
- RAID 1v: Majority-based validation on reads.
- Lazy directory parsing and full inode-based file tracking.
- Supports all of the following FUSE operations:
  - `getattr`, `mknod`, `mkdir`, `unlink`, `rmdir`, `read`, `write`, `readdir`

## Usage

### Build

```bash
make
```

### Create Disks

```bash
./create_disk.sh     # Modify this script to create multiple disks
```

### Format Filesystem

```bash
./mkfs -r 1 -d disk1 -d disk2 -i 32 -b 200
```

### Mount Filesystem

```bash
mkdir mnt
./wfs disk1 disk2 -f -s mnt
```

### Interact

```bash
mkdir mnt/dir
echo "data" > mnt/file
cat mnt/file
ls mnt
```

## Mounting Behavior

- Filesystem must be mounted with **same number of disks** used during formatting.
- Disk **order does not matter**, and disks can be renamed.
- Filesystem mode (RAID 0 / RAID 1 / RAID 1v) is embedded in the superblock.
- Valid modes: `-r 0`, `-r 1`, `-r 1v`

## Error Handling

The filesystem properly returns standard Linux error codes:

- `-ENOENT`: File or directory does not exist
- `-EEXIST`: File or directory already exists
- `-ENOSPC`: No space left on device

## Testing

This repository includes a `tests/` directory with functional tests to validate:

- File and directory creation
- RAID behavior
- Reading and writing across block boundaries
- Mount/unmount correctness
- Edge cases such as max file size, missing disk, and more

To run tests:

```bash
make test
```

## Structure

- `mkfs.c` – Creates an empty filesystem image with proper metadata layout.
- `wfs.c` – Core FUSE logic, reads/writes metadata and data blocks via `mmap`.
- `wfs.h` – Contains `superblock`, `inode`, `dirent`, and constant definitions.
- `create_disk.sh` – Script to initialize disk image files.
- `umount.sh` – Unmount helper.

## Debugging Tips

- Use `printf()` inside each FUSE callback to track control flow.
- View raw disk content with `xxd -e -g 4 disk.img | less`
- Run in `gdb`:  
  ```bash
  gdb --args ./wfs disk1 disk2 -f -s mnt
  ```
- Always `make clean` before committing or submitting.

## Further References

- [FUSE API Docs](https://libfuse.github.io/doxygen/index.html)
- [RAID and Filesystems in OSTEP](https://pages.cs.wisc.edu/~remzi/OSTEP/file-raid.pdf)
- [FUSE Tutorial](https://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/html/index.html)

---

**Note**: This implementation follows the specification and constraints given in CS537 Fall 2024 Project 6. All testing was done on UW-Madison lab machines. The code was originally completed in December 2024 and open-sourced for educational purposes.

