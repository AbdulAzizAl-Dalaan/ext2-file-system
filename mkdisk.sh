#! /bin/sh
# mkdisk: create a virtual disk diskimage

cd ./images
sudo dd if=/dev/zero of=diskimage bs=1024 count=1440
sudo mke2fs -b 1024 diskimage 1440

sudo mount diskimage /mnt

(cd /mnt; sudo mkdir dir1 dir2 dir1/dir3; sudo touch file1 file2; ls -al)

sudo umount /mnt

