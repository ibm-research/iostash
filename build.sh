#!/bin/bash

SEP='--------------------------------------------------------------------------------'

PKG_NAME="iostash-$(uname -r)"

P=$(grep processor /proc/cpuinfo | wc -l)
P=$((P/2))

if [ "$P" -eq 0 ];
then
    P=1
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

echo $SEP
echo "Cleaning up previous builds."
make clean
check_ret "Make clean"
echo

echo $SEP
echo "Building the iostash kernel module"
make -j$P
check_ret "Make"
echo

echo $SEP
echo "Packaging iostash"
rm -fr /tmp/$PKG_NAME
mkdir -p /tmp/$PKG_NAME
check_ret "Packaging directory creation"

cp build/iostash.ko /tmp/$PKG_NAME
check_ret "File copy"

if [ -z "$(which md5sum)" ];
then
    echo "md5sum missing, please install and build again."
    exit 1
fi

MD5SUM="$($(which md5sum) build/iostash.ko | awk '{ print $1 }')"

cp scripts/iostash /tmp/$PKG_NAME/iostash
check_ret "File copy"
BRANCH=$(git branch | grep '\*' | awk '{print $2 }')
COMMIT=$(git log -1 | grep commit | awk '{ print $2 }')
DATE=$(date)
sed -i -e "s/BRANCH_NAME_PH/$BRANCH/" -e "s/COMMIT_HASH_PH/$COMMIT/" -e "s/BUILD_DATE_PH/$DATE/" -e "s/MD5SUM_PH/$MD5SUM/" /tmp/$PKG_NAME/iostash

cp scripts/install.sh /tmp/$PKG_NAME
check_ret "File copy"
cp README /tmp/$PKG_NAME/
check_ret "File copy"
git branch | grep '*' | awk '{print $2}' > /tmp/$PKG_NAME/version
git log | head -n3 | grep -v Author >> /tmp/$PKG_NAME/version

pushd /tmp
tar cvfz $PKG_NAME.tar.gz $PKG_NAME/*
check_ret "Tarball creation"
popd

mkdir -p package
check_ret "Directory creation"
rm -fr package/$PKG_NAME.tar.gz
cp /tmp/$PKG_NAME.tar.gz package/
check_ret "File copy"

rm -fr /tmp/$PKG_NAME*

echo
echo $SEP

if [ -f package/$PKG_NAME.tar.gz ]
then
    echo "SUCCESS"
    echo "Output at $(pwd)/package/$PKG_NAME.tar.gz"
else
    echo "BUILD FAILED"
fi
echo
