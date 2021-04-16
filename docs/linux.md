# Linux

Build a Linux kernel for use as the primary VM OS kernel on the QEMU simulator.

## Host cross-compilers

You will need an GCC aarch64 targeted compiler. It may be available in your Linux distribution, E.g.:
```bash
sudo apt install build-essential gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu ncurses-dev
```

## Download the source code

Download the latest Linux source code:
> Note, we show a shallow clone to speed up checkouts

```bash
git clone --depth=1 https://github.com/torvalds/linux.git
```

## Build Linux kernel

Build Linux kernel with the default defconfig, cross-compiled for arm64.

```bash
cd linux/
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
make -j4 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
cp ./arch/arm64/boot/Image <path-to-output-dir>/.
```
