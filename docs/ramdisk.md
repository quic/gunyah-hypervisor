# A simple Busybox Ramdisk

A simple RAM disk is required at a minimum to boot Linux in a VM.

The following instructions create a RAM disk using Busybox 1.33.0.

## Build Busybox 1.33.0

Build Busybox cross compiled for AArch64:

```bash
wget -c https://busybox.net/downloads/busybox-1.33.0.tar.bz2
tar xf busybox-1.33.0.tar.bz2
cd busybox-1.33.0
make ARCH=arm CROSS_COMPILE=aarch64-linux-gnu- defconfig
make ARCH=arm CROSS_COMPILE=aarch64-linux-gnu- menuconfig
sed -i '/# CONFIG_STATIC is not set/c\CONFIG_STATIC=y' .config
ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make -j4
ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make install
```

## Create a RAM disk

Create a RAM disk using the Busybox build and place it in an output directory:

```bash
cd _install
mkdir proc sys dev etc etc/init.d
cat <<EOF > etc/init.d/rcS
#!bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
EOF
chmod u+x etc/init.d/rcS
grep -v tty ../examples/inittab > ./etc/inittab
find . | cpio -o -H newc | gzip > <path-to-output-dir>/initrd.img
```
