BINS = wfs mkfs
CC = gcc
CFLAGS = -Wall -Werror -pedantic -std=gnu18 -g -D_FILE_OFFSET_BITS=64
FUSE_CFLAGS = `pkg-config fuse --cflags --libs`

MKFS_SRCS = mkfs.c fs_utils.c inode.c globals.c
MKFS_OBJS = $(MKFS_SRCS:.c=.o)

WFS_SRCS = wfs.c fs_utils.c inode.c globals.c

WFS_OBJS = $(WFS_SRCS:.c=.o)

.PHONY: all clean

all: $(BINS)

wfs: $(WFS_OBJS)
	$(CC) $(CFLAGS) $(WFS_OBJS) $(FUSE_CFLAGS) -o wfs
mkfs: $(MKFS_OBJS)
	$(CC) $(CFLAGS) $(MKFS_OBJS) -o mkfs

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(BINS) *.o
