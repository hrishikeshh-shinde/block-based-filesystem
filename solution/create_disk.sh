#!/bin/bash
rm -rf disk1
rm -rf disk2
dd if=/dev/zero of=disk1 bs=1M count=1
dd if=/dev/zero of=disk2 bs=1M count=1
rm -rf mnt/
mkdir -p mnt/
./mkfs -r 1 -d disk1 -d disk2 -i 32 -b 224
./wfs disk1 disk2 -s mnt/