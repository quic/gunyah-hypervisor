# QEMU AArch64 Simulator

Note, Gunyah requires QEMU version >= `5.0.0`

## Installation

Install the dependencies described in the QEMU documentation.

For example for a Linux host:
https://wiki.qemu.org/Hosts/Linux

Download and build latest version:
https://www.qemu.org/download/#source

Make sure that it is built with aarch64-softmmu included in the target list.

For example, to build QEMU only for aarch64-softmmu target:

```bash
git clone https://git.qemu.org/git/qemu.git
cd qemu
git submodule init
git submodule update --recursive
./configure --target-list=aarch64-softmmu --enable-debug
make
```
