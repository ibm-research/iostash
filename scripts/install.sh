#!/bin/bash

## Require superuser privileges
if [[ $(/usr/bin/id -u) -ne 0 ]]; then
    echo "Superuser privileges required. Aborting..."
    exit 1
fi

check_ret()
{
  cur_ret_val=$?
  if [ $cur_ret_val -ne 0 ]
  then
    echo "ERROR: $* returned $cur_ret_val."
    exit 1
  fi
}

CAC_PRESENT=
VOL_PRESENT=

handle_existing_devs ()
{
    # Check if iostash is running
    RET=$(lsmod | grep iostash)
    CAC_PRESENT=$(/usr/bin/iostash cache list | grep -v "Listing" | grep -v "Loading")
    VOL_PRESENT=$(/usr/bin/iostash target list | grep -v "Listing" | grep -v "Loading")

    if [ "$1" = "--force" ];
    then
	for c in $CAC_PRESENT;
	do
	    /usr/bin/iostash cache remove $c
	done
	for t in $VOL_PRESENT;
	do
	    /usr/bin/iostash target remove $t
	done
	if [ ! -z "$RET" ];
	then
	    modprobe -r iostash
	    check_ret "Module unload"
	fi
    else
	if [ -z "$RET" ];
	then
	    return
	fi

	echo "The iostash module was found loaded."
	echo

	if [ ! -z "$CAC_PRESENT" ];
	then
	    echo "The following devices are configured cache devices:"
	    echo $CAC_PRESENT
	    echo
	fi

	if [ ! -z "$VOL_PRESENT" ];
	then
	    echo "The following volumes are configured cache targets:"
	    echo $VOL_PRESENT
	    echo
	fi
	echo "To make the installation effective, the existing caches and targets need"
	echo "to be removed and the iostash module will need to be reloaded. This will"
	echo "cause the cached content to get purged. Please re-run with '--force' to"
	echo "purge the caches and reload iostash".
	echo
	echo "Otherwise, the installation will become effective the next time you"
	echo "unload and reload the iostash kernel module."
	echo
	exit 1
    fi
}

FILES="iostash.ko iostash"

for f in $FILES;
do
    if [ ! -f "$f" ]
    then
	echo "File missing: $f"
	exit 1
    fi
done

handle_existing_devs $1

MODULE_DIR="/lib/modules/$(uname -r)/extra"

mkdir -p $MODULE_DIR
check_ret "Directory creation"
install -o root -g root -m 0755 iostash.ko $MODULE_DIR/iostash.ko
check_ret "Module installation"
depmod -a
check_ret "Depmod"

# clean up previous versions of the script
rm -f /usr/bin/iostash
rm -f /usr/local/bin/iostash

install -o root -g root -m 0755 iostash /usr/bin/iostash
check_ret "Script installation"

echo
echo "iostash installed successfully as /usr/bin/iostash."
echo

if [ ! -z "$CAC_PRESENT" ];
then
    for c in $CAC_PRESENT;
    do
	/usr/bin/iostash cache add $c
    done
fi

if [ ! -z "$VOL_PRESENT" ];
then
    for t in $VOL_PRESENT;
    do
	/usr/bin/iostash target add $t
    done
fi
