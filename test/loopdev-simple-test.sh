#!/bin/bash

fallocate -l 100M /dev/shm/cache
fallocate -l 100M /dev/shm/cache2
fallocate -l 1g /dev/shm/hdd
fallocate -l 500M /dev/shm/hdd2
sudo losetup  /dev/loop0 /dev/shm/hdd
sudo losetup  /dev/loop1 /dev/shm/cache
sudo losetup  /dev/loop2 /dev/shm/cache2
sudo losetup  /dev/loop3 /dev/shm/hdd2

iotest_pid=0
/usr/local/bin/io-test-count dev=/dev/loop0 pattern=tzipf95_20 ws=0 bs=4096 timeout=120 writes=0 direct_io=yes tc=8 2>&1 > /tmp/io-test.log & iotest_pid=$!
iotest_pid2=0
/usr/local/bin/io-test-count dev=/dev/loop3 pattern=tzipf80_20 ws=0 bs=4096 timeout=120 writes=20 direct_io=yes tc=8 2>&1 > /tmp/io-test2.log & iotest_pid2=$!
sudo iostash target add /dev/loop0
sleep 2

sudo iostash cache add /dev/loop1
sleep 5
sudo iostash global stats
sleep 2
sudo iostash global stats
sleep 2
sudo iostash cache add /dev/loop2
sleep 5
sudo iostash global stats
sleep 2
sudo iostash global stats

sudo iostash target add /dev/loop3
sleep4
sudo iostash global stats
sleep 4
sudo iostash global stats
sleep 5
sudo iostash global stats

sudo modprobe -r iostash

kill $iotest_pid
kill $iotest_pid2

sudo losetup -d /dev/loop0 /dev/loop1 /dev/loop2 /dev/loop3
