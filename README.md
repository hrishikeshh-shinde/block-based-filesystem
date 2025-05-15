# Block-Based Filesystem with RAID using FUSE

This project is a user-level block-based filesystem developed using **FUSE** in C, implementing core features such as file creation, reading, writing, directory management, and RAID-level storage (RAID 0, RAID 1, RAID 1v). Designed as part of the Operating Systems curriculum (CS537), it demonstrates low-level filesystem logic including inode and block bitmap management, memory-mapped file I/O, and page-aligned metadata structures.

## Contributors

- Hrishikesh Shinde  
- Ritik Singh

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
- [Contributors](#contributors)

## Overview

The filesystem supports:

- Traditional block-based layout with inodes, bitmaps, and a superblock.
- RAID 0 (striping), RAID 1 (mirroring), and RAID 1v (verified mirroring).
- Verified mirroring performs majority-read validation across disks.
- Full integration with FUSE to support `mkdir`, `rmdir`, `read`, `write`, `unlink`, and more.

This project is composed of:

- `mkfs.c` – Initializes a new filesystem on given disk images.
- `wfs.c` – Entry point for the FUSE-based filesystem.
- `fuse_operations.c` – Contains the FUSE operation implementations.
- `wfs.h` – Contains all the filesystem structure definitions.
- Utility scripts: `create_disk.sh`, `umount.sh`, `Makefile`

## Key Features

- Block-aligned superblock, inode structures, and data blocks (512 bytes each)
- RAID 0 and RAID 1 support with metadata mirroring
- RAID 1v: Majority-based data verification during reads
- Lazy directory parsing and inode-based file structure
- Supports the following FUSE callbacks:
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

- Filesystem must be mounted with the same number of disks used during formatting.
- Disk order during mount does not matter; disk image filenames can be changed.
- Filesystem mode (RAID 0, 1, 1v) is stored in the superblock.
- Valid modes: `-r 0`, `-r 1`, `-r 1v`

## Error Handling

The filesystem returns standard Linux error codes when appropriate:

- `-ENOENT`: File or directory does not exist
- `-EEXIST`: File or directory already exists
- `-ENOSPC`: No space left on device

## Testing

This repository includes a `tests/` directory with functional tests to validate:

- File and directory creation
- RAID behavior (RAID 0, 1, and 1v)
- Reading and writing across block boundaries
- Mount and unmount correctness
- Edge cases like full disk, invalid flags, and max file size

To run tests:

```bash
make test
```

## Structure

- `mkfs.c` – Formats disks with a fresh filesystem and metadata layout
- `wfs.c` – Main function for FUSE mounting
- `fuse_operations.c` – Core filesystem logic and FUSE callbacks
- `wfs.h` – Structs for superblock, inodes, dirents, and constants
- `create_disk.sh` – Script to create zeroed disk images
- `umount.sh` – Script to unmount the filesystem
- `Makefile` – Compiles all parts of the project

## Debugging Tips

- Add `printf()` statements inside each FUSE callback to trace execution.
- Inspect raw disk contents with:

```bash
xxd -e -g 4 disk1 | less
```

- Run the filesystem in `gdb` with:

```bash
gdb --args ./wfs disk1 disk2 -f -s mnt
```

- Always `make clean` before testing or submitting.
