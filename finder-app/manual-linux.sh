#!/bin/sh
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.
# Assignment Completed By: Biplav Poudel

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    #Kernel Building Steps
	# QEMU build - clean: deep cleans kernel build tree by removing .config files
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
	
	#QEMU build - defconfig: configuring virt arm devboard for simulation in QEMU
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig

	#QEMU build - vmlinux: building kernel image for booting with QEMU
	make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
	
	#QEMU build - module: building any modules needed for the kernel
	#make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
	
	#QEMU build - devicetree
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

echo "Adding the Image in outdir"
ls -l ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/Image

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

#Create necessary base directories
ROOTFS=${OUTDIR}/rootfs

mkdir -p ${ROOTFS}
cd "$ROOTFS"
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin usr/lib64
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    #Configure busybox
	make distclean
	make defconfig

else
    cd busybox
fi

#Make and install busybox
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX=${ROOTFS} ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install


#Add library dependencies to rootfs

cd "$ROOTFS"
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)

cp ${SYSROOT}/lib/ld-linux-aarch64.so.1 ${ROOTFS}/lib/
cp ${SYSROOT}/lib64/libm.so.6 ${ROOTFS}/lib64/
cp ${SYSROOT}/lib64/libresolv.so.2 ${ROOTFS}/lib64/
cp ${SYSROOT}/lib64/libc.so.6 ${ROOTFS}/lib64/

echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

#Make device nodes
sudo mknod -m 666 ${ROOTFS}/dev/null c 1 3
sudo mknod -m 666 ${ROOTFS}/dev/console c 5 1
sudo mknod -m 666 ${ROOTFS}/dev/zero c 1 5   # not specifically mentioned in assignment but I thought it is needed for infinite stream of 0s

#Clean and build the writer utility
cd "${FINDER_APP_DIR}"
make clean
make CROSS_COMPILE=${CROSS_COMPILE}   #gcc is appended in Makefile

# Copy the finder related scripts and executables to the /home directory on the target rootfs
cp -rL ./writer ./finder.sh ./conf/ ./finder-test.sh ./autorun-qemu.sh ${ROOTFS}/home/

#Chown the root directory
cd "$ROOTFS"
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio

#Create initramfs.cpio.gz
gzip -f ${OUTDIR}/initramfs.cpio

