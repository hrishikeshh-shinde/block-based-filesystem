BINS = wfs mkfs
CC = gcc
CFLAGS = -Wall -Werror -pedantic -std=gnu18 -g
FUSE_CFLAGS = `pkg-config fuse --cflags --libs`

MKFS_SRCS = mkfs.c
MKFS_OBJS = mkfs.o

.PHONY: all clean

all: $(BINS)

wfs:
	$(CC) $(CFLAGS) wfs.c $(FUSE_CFLAGS) -o wfs
mkfs: $(MKFS_OBJS)
	$(CC) $(CFLAGS) $(MKFS_OBJS) -o mkfs

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(BINS) *.o
