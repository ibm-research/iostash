#!/bin/bash


# Loop device configuration
MAX_LOOPDEVS=4
LOOPDEV_SIZE_MiB=16


# Check for fio
FIO="fio"
FIO_CONFIG_FILE="/tmp/iostash.fio"

MODULE_NAME="iostash"
SCRIPT="/usr/bin/$MODULE_NAME"

IO_TIME=60    # seconds

VERBOSE=

usage() {
    echo
    echo "USAGE: $0 <options> \"<cache_devs>\" \"<target_devs>\""
    echo -e "\nIf devices are not specified, loop devices will be created and used."
    echo -e "\nOptions:"
    echo -e "\t-v Verbose output"
    echo -e "\nExample:"
    echo -e "$0 -v \"/dev/sda /dev/sdb\" \"/dev/mapper/volume1 /dev/mapper/volume2\""
}

check_ret()
{
    cur_ret_val=$?
    if [ $cur_ret_val -ne 0 ]
    then
        echo "ERROR: $* returned $cur_ret_val."
        echo "**************** COMMAND FAILED ****************"
        exit $cur_ret_val
    fi
}

add_space()
{
    echo -e "\n\n"
}

assert()
{
    lineno=$2

    if [ ! $1 ]
    then
        echo "Assertion failed:  \"$1\""
        exit 100
    fi
}

# Arguments
# $1   The list of cache devices
add_caches()
{
    for cd in $1;
    do
	CMD="$SCRIPT cache add $cd"

	if [ $VERBOSE -eq 1 ];
	then
	    echo $CMD | tee -a $LOG_FILE
	fi

	echo -e "\tADD CACHE DEVICE $cd" | tee -a $LOG_FILE
	eval $CMD >> $LOG_FILE
	check_ret "Cache device ($cd) addition"
    done
}

# Arguments
# $1   The list of target devices
add_targets()
{
    for td in $1;
    do
	CMD="$SCRIPT target add $td"

	if [ $VERBOSE -eq 1 ];
	then
	    echo $CMD | tee -a $LOG_FILE
	fi

	echo -e "\tADD TARGET DEVICE $td" | tee -a $LOG_FILE
	eval $CMD >> $LOG_FILE
	check_ret "Target device ($td) addition"
    done
}

# Arguments
# $1   The list of cache devices
remove_caches()
{
    for cd in $1;
    do
	CMD="$SCRIPT cache remove $cd"

	if [ $VERBOSE -eq 1 ];
	then
	    echo $CMD | tee -a $LOG_FILE
	fi

	echo -e "\tREMOVE CACHE DEVICE $cd" | tee -a $LOG_FILE
	eval $CMD >> $LOG_FILE
	check_ret "Cache device ($cd) removal"
    done
}

# Arguments
# $1   The list of target devices
remove_targets()
{
    for td in $1;
    do
	CMD="$SCRIPT target remove $td"

	if [ $VERBOSE -eq 1 ];
	then
	    echo $CMD | tee -a $LOG_FILE
	fi

	echo -e "\tREMOVE TARGET DEVICE $td" | tee -a $LOG_FILE
	eval $CMD >> $LOG_FILE
	check_ret "Target device ($td) removal"
    done
}

prepare_block_fio()
{
    echo "
[global]
name=iostash-test
rw=randrw
ioengine=psync
direct=1
sync=1
size=100%
do_verify=1
verify=md5
verify_fatal=1
verify_dump=1
invalidate=0
time_based
thread
group_reporting

" > $FIO_CONFIG_FILE
}

# Run fio against the specified block devices
run_block_test()
{
    return
    declare -a FIO_DEVS
    FIO_DEVS=($@)

    if [ -z "$BS" ];
    then
	BS="4096"
    fi

    if [ -z "$RW" ];
    then
	RW="50"
    fi

    if [ -z "$QD" ];
    then
	QD="1"
    fi

    for bs in $BS;
    do
	for rw in $RW;
	do
	    for qd in $QD;
	    do
		prepare_block_fio
		for d in ${FIO_DEVS[@]};
		do
		    echo "[$d]" >> $FIO_CONFIG_FILE
		    echo "filename=$d" >> $FIO_CONFIG_FILE
		    echo "blocksize=$bs" >> $FIO_CONFIG_FILE
		    echo "rwmixread=$rw" >> $FIO_CONFIG_FILE
		    echo "runtime=$IO_TIME" >> $FIO_CONFIG_FILE
		    echo "numjobs=$qd" >> $FIO_CONFIG_FILE
		    if [ "$qd" -gt 1 ];
		    then
		    	echo "do_verify=0" >> $FIO_CONFIG_FILE
		    fi
		    echo "" >> $FIO_CONFIG_FILE
		done

		if [ $VERBOSE -eq 1 ];
		then
		    echo "Fio command: $FIO $FIO_CONFIG_FILE" | tee -a $LOG_FILE
		    echo "Fio configuration file:" | tee -a $LOG_FILE
		    cat $FIO_CONFIG_FILE | tee -a $LOG_FILE
		fi

		echo -e -n "\t\tBLOCK FIO (BS:$bs, RW:$rw, QD:$qd, DEVICES: ${FIO_DEVS[@]})..." | tee -a $LOG_FILE

		$FIO $FIO_CONFIG_FILE >> $LOG_FILE
		check_ret "FIO workload"
		sleep 5
		echo -e " DONE" | tee -a $LOG_FILE
	    done
	done
    done
}

prepare_file_fio()
{
    echo "
[global]
name=fs-test
rw=randrw
ioengine=psync
iodepth=1
sync=1
size=100
do_verify=1
verify=md5
verify_fatal=1
verify_dump=1
invalidate=0
time_based
thread
group_reporting
end_fsync=1

" > $FIO_CONFIG_FILE
}


run_file_test()
{
    FS_TYPE=$1
    shift 1
    declare -a BLOCK_DEVS
    BLOCK_DEVS=($@)

    if [ ! -f "/sbin/mkfs.$FS_TYPE" ];
    then
	echo -e "\t\t$FS_TYPE not supported on this host (/sbin/mkfs.$FS_TYPE is missing)." | tee -a $LOG_FILE
	return 0
    fi

    if [ -z "$BS" ];
    then
	BS="4096"
    fi

    if [ -z "$RW" ];
    then
	RW="50"
    fi

    if [ -z "$QD" ];
    then
	QD="1"
    fi

    if [ -z "$NF" ];
    then
	NF="100"
    fi

    MOUNT_PARENT=/tmp/mounts

    for bs in $BS;
    do
	for rw in $RW;
	do
	    for qd in $QD;
	    do
		for nf in $NF;
		do
		    prepare_file_fio
		    for d in ${BLOCK_DEVS[@]};
		    do
			FSIZE=$((($(blockdev --getsize $d) * 512 / 2) / $nf)) # Only use half the capacity

			echo -e -n "\t\tMKFS $FS_TYPE & MOUNT (under $MOUNT_PARENT, DEVICES: ${BLOCK_DEVS[@]})..." | tee -a $LOG_FILE
			MOUNT_POINT=$MOUNT_PARENT/$(basename $d).fs
			mkdir -p "$MOUNT_POINT"
			check_ret "Mount point creation"
			dd if=/dev/zero of=$d bs=1M count=$LOOPDEV_SIZE_MiB
			mkfs.$FS_TYPE $d >> $LOG_FILE 2>&1
			check_ret "$FS_TYPE filesystem creation"
			mount -t $FS_TYPE $d $MOUNT_POINT
			check_ret "$FS_TYPE filesystem mount"
			echo -e " DONE" | tee -a $LOG_FILE

			echo "[$(basename $d)]" >> $FIO_CONFIG_FILE
			echo "directory=$MOUNT_POINT" >> $FIO_CONFIG_FILE
			echo "blocksize=$bs" >> $FIO_CONFIG_FILE
			echo "filesize=$FSIZE" >> $FIO_CONFIG_FILE
			echo "nr_files=$(($nf/$qd))" >> $FIO_CONFIG_FILE
			echo "rwmixread=$rw" >> $FIO_CONFIG_FILE
			echo "runtime=$IO_TIME" >> $FIO_CONFIG_FILE
			echo "numjobs=$qd" >> $FIO_CONFIG_FILE
			if [ "$qd" -gt 1 ];
			then
		    	    echo "do_verify=0" >> $FIO_CONFIG_FILE
			fi
			echo "" >> $FIO_CONFIG_FILE
		    done

		    if [ "$VERBOSE" -eq 1 ];
		    then
			echo "Fio command: $FIO $FIO_CONFIG_FILE" | tee -a $LOG_FILE
			echo "Fio configuration file:" | tee -a $LOG_FILE
			cat $FIO_CONFIG_FILE | tee -a $LOG_FILE
		    fi

		    echo -e -n "\t\tFILE FIO (BS:$bs, RW:$rw, QD:$qd, NF:$nf, DEVICES: ${BLOCK_DEVS[@]})..." | tee -a $LOG_FILE

		    $FIO $FIO_CONFIG_FILE >> $LOG_FILE
		    check_ret "FIO workload"
		    sleep 5
		    echo -e " DONE" | tee -a $LOG_FILE

		    for d in ${BLOCK_DEVS[@]};
		    do
			echo -e -n "\t\tUNMOUNT FILESYSTEMS(under $MOUNT_PARENT, DEVICES: ${BLOCK_DEVS[@]})..." | tee -a $LOG_FILE
			umount -f $d
			check_ret "$FS_TYPE un-mount"
			echo -e " DONE" | tee -a $LOG_FILE
		    done
		    sleep 5
		done
	    done
	done
    done
}


start_test()
{
    echo -e "$1" | tee -a $LOG_FILE
}

end_test()
{
    echo -e "FINISHED $(date)\n"
}

################################################################################
########################  Prepare the test devices  ############################
################################################################################

## Require superuser privileges
if [[ $(/usr/bin/id -u) -ne 0 ]];
then
    echo "Superuser privileges required. Aborting."
    exit 1
fi

# Specify a list of devices to use. Otherwise, file-backed loop devices will
# be created and used, based on the values below.
CACHE_DEV_NAMES=
TARGET_DEV_NAMES=

USE_LOOP_DEVS=0
VERBOSE=0

if [ -z "$1" ];
then
    USE_LOOP_DEVS=1
else
    # we have cmd-line arguments
    if [ "$1" = "-v" ];
    then
	VERBOSE=1
	shift
    fi

    if [ -z "$1" ];
    then
	USE_DEV_LOOPS=1
    else
	if [ -z "$(echo $1 | grep '/dev/')" ];
	then
	    usage
	    exit 1
	else
	    CACHE_DEV_NAMES=$1
	    if [ -z "$(echo $2 | grep '/dev/')" ];
	    then
		usage
		exit 1
	    else
		TARGET_DEV_NAMES=$2
	    fi
	fi
    fi
fi

declare -a CACHE_DEVS
declare -a TARGET_DEVS

LOOP_DEV_NAMES=

if [ $USE_LOOP_DEVS -eq "1" ];
then
    # set up loop devices
    for i in $(seq 0 $MAX_LOOPDEVS);
    do
	LDEV=$(losetup -f 2>/dev/null)
	if [ $? -ne 0 ];
	then
	    break;
	fi

	LOOP_DEV_NAMES="$LOOP_DEV_NAMES $LDEV"
	LFNAME="/tmp/file$i.dat"
	if [ -f "$LFNAME" ];
	then
	    let LFSIZE=$(stat -c %s "$LFNAME")/1048576
	    if [ "$LFSIZE" -ne "$LOOPDEV_SIZE_MiB" ];
	    then
		# correct the size
		dd if=/dev/zero of=$LFNAME bs=1M count=$LOOPDEV_SIZE_MiB
	    fi
	else
	    dd if=/dev/zero of=$LFNAME bs=1M count=$LOOPDEV_SIZE_MiB
	fi
	losetup $LDEV $LFNAME
	check_ret "Loop device creation"
    done

    if [ $(echo "$LOOP_DEV_NAMES"| wc -w) -lt 4 ];
    then
	echo "At least 4 loop devices required"
	exit 4
    fi

    declare -a LOOP_DEVS
    LOOP_DEVS=($LOOP_DEV_NAMES)

    # Use half of the devices as caches
    for d in $(seq 0 $((${#LOOP_DEVS[@]} / 2 - 1)));
    do
	CACHE_DEVS[$d]=${LOOP_DEVS[$d]}
    done

    # Use the rest of the devices as targets
    for d in $(seq 0 $((${#LOOP_DEVS[@]} / 2 - 1)));
    do
	TARGET_DEVS[$d]=${LOOP_DEVS[(($d + (${#LOOP_DEVS[@]} / 2)))]}
    done
else
    # not using loop devices
    CACHE_DEVS=($CACHE_DEV_NAMES)
    TARGET_DEVS=($TARGET_DEV_NAMES)
fi

echo "Using cache devices: ${CACHE_DEVS[@]}"
echo "Using target devices: ${TARGET_DEVS[@]}"

# Make sure the kernel module has been loaded
MOD_LOADED=$(lsmod | grep $MODULE_NAME)
if [ -z "$MOD_LOADED" ];
then
    echo "Loading the $MODULE_NAME kernel module."
    modprobe "$MODULE_NAME"
    check_ret "$MODULE_NAME kernel module load."
fi


LOG_FILE="/tmp/iostash-test-$(date +%y%m%d-%H.%M).out"

echo -e "Logging to $LOG_FILE\n"
rm -f $LOG_FILE

################################################################################
##########################  Test Cases Start Here  #############################
################################################################################


# USAGE:
# start_test        name
# start_cache
# BS= ; RW= ; QD= ; run_block_test <device list>
# BS= ; RW= ; QD= ; NF= ; run_file_test <device list>
# stop_cache
# end_test

# start_test "SINGLE CACHE DEVICE, SINGLE TARGET DEVICE, 100% READS"
# add_caches ${CACHE_DEVS[0]}
# add_targets ${TARGET_DEVS[0]}
# BS="512 4096 1048576"; RW="100"; QD="1 8 32";
# run_block_test ${TARGET_DEVS[0]}
# run_file_test ext4 ${TARGET_DEVS[0]}
# run_file_test btrfs ${TARGET_DEVS[0]}
# remove_caches ${CACHE_DEVS[0]}
# remove_targets ${TARGET_DEVS[0]}
# end_test

start_test "SINGLE CACHE DEVICE, SINGLE TARGET DEVICE, 50% READS 50% WRITES"
add_caches ${CACHE_DEVS[0]}
add_targets ${TARGET_DEVS[0]}
BS="512 4096 1048576"; RW="50"; QD="1 8 32";
run_block_test ${TARGET_DEVS[0]}
run_file_test ext4 ${TARGET_DEVS[0]}
run_file_test btrfs ${TARGET_DEVS[0]}
remove_caches ${CACHE_DEVS[0]}
remove_targets ${TARGET_DEVS[0]}
end_test

exit

start_test "ALL CACHE DEVICES, SINGLE TARGET DEVICE, 100% READS"
add_caches ${CACHE_DEVS[@]}
add_targets ${TARGET_DEVS[0]}
BS="512 4096 1048576"; RW="100"; QD="1 8 32";
run_block_test ${TARGET_DEVS[0]}
run_file_test ext4 ${TARGET_DEVS[0]}
run_file_test btrfs ${TARGET_DEVS[0]}
remove_caches ${CACHE_DEVS[@]}
remove_targets ${TARGET_DEVS[0]}
end_test

start_test "SINGLE CACHE DEVICE, ALL TARGET DEVICES, 100% READS"
add_caches ${CACHE_DEVS[0]}
add_targets ${TARGET_DEVS[@]}
BS="512 4096 1048576"; RW="100"; QD="1 8 32"; run_block_test ${TARGET_DEVS[@]}
run_file_test ext4 ${TARGET_DEVS[@]}
run_file_test btrfs ${TARGET_DEVS[@]}
remove_caches ${CACHE_DEVS[0]}
remove_targets ${TARGET_DEVS[@]}
end_test

start_test "ALL CACHE DEVICES, ALL TARGET DEVICES, MIXED WORKLOADS"
add_caches ${CACHE_DEVS[@]}
add_targets ${TARGET_DEVS[@]}
BS="512 4096 1048576"; RW="100 50 0"; QD="1 8 32"; run_block_test ${TARGET_DEVS[@]}
run_file_test ext4 ${TARGET_DEVS[@]}
run_file_test btrfs ${TARGET_DEVS[@]}
remove_caches ${CACHE_DEVS[@]}
remove_targets ${TARGET_DEVS[@]}
end_test


################################################################################

# Cleanup
for d in ${LOOP_DEVS[@]};
do
    losetup -d $d
done
