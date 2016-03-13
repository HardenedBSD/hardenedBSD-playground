#!/bin/sh

# Comment out the following when you have cehcked the following settings
#echo "You need to set check the script settings" ; exit 1;

# Where to find the Raspberry Pi 3 firmware
FIRMWAREDIR=${HOME}/rpi3/firmware/boot

# Set this based on how many CPUs you have
JFLAG=-j$(sysctl -n hw.ncpu)

# Where to put you're build objects, you need write access
export MAKEOBJDIRPREFIX=${HOME}/rpi3/obj

# Where to install to
DEST=${MAKEOBJDIRPREFIX}/stage

set -e

make TARGET=arm64 -s ${JFLAG} buildworld -DNO_CLEAN
make TARGET=arm64 -s ${JFLAG} buildkernel -DNO_CLEAN

mkdir -p ${DEST}/root
make TARGET=arm64 -s -DNO_ROOT installworld distribution installkernel \
     DESTDIR=${DEST}/root

echo "/dev/mmcsd0s2a / ufs rw,noatime 0 0" > ${DEST}/root/etc/fstab
echo "./etc/fstab type=file uname=root gname=wheel mode=0644" >> ${DEST}/root/METALOG

echo "hostname=\"rpi3\"" > ${DEST}/root/etc/rc.conf
echo "growfs_enable=\"YES\"" >> ${DEST}/root/etc/rc.conf
echo "./etc/rc.conf type=file uname=root gname=wheel mode=0644" >> ${DEST}/root/METALOG

touch ${DEST}/root/firstboot
echo "./firstboot type=file uname=root gname=wheel mode=0644" >> ${DEST}/root/METALOG

makefs -t ffs -B little -F ${DEST}/root/METALOG ${DEST}/ufs.img ${DEST}/root

mkimg -s bsd -p freebsd-ufs:=${DEST}/ufs.img -o ${DEST}/ufs_part.img

newfs_msdos -C 50m -F 16 ${DEST}/fat.img

cp ${DEST}/root/boot/dtb/bcm2710-rpi-3-b.dtb ${DEST}/bcm2710.dtb
mcopy -i ${DEST}/fat.img ${DEST}/bcm2710.dtb  ::
mcopy -i ${DEST}/fat.img ${DEST}/root/boot/fbsdboot.bin ::
mcopy -i ${DEST}/fat.img ${DEST}/root/boot/kernel/kernel ::
mcopy -i ${DEST}/fat.img rpi3/config.txt ::

mcopy -i ${DEST}/fat.img ${FIRMWAREDIR}/bootcode.bin ::
mcopy -i ${DEST}/fat.img ${FIRMWAREDIR}/fixup.dat ::
mcopy -i ${DEST}/fat.img ${FIRMWAREDIR}/fixup_cd.dat ::
mcopy -i ${DEST}/fat.img ${FIRMWAREDIR}/fixup_x.dat ::
mcopy -i ${DEST}/fat.img ${FIRMWAREDIR}/start.elf ::
mcopy -i ${DEST}/fat.img ${FIRMWAREDIR}/start_cd.elf ::
mcopy -i ${DEST}/fat.img ${FIRMWAREDIR}/start_x.elf ::

dd if=/dev/urandom of=${DEST}/root/boot/entropy bs=4096 count=1
chown root:wheel ${DEST}/root/boot/entropy
chmod 000 ${DEST}/root/boot/entropy

mkimg -s mbr -p fat16b:=${DEST}/fat.img -p freebsd:=${DEST}/ufs_part.img \
    -o ${DEST}/rpi3.img

