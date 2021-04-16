# Testing Gunyah Hypervisor

We provide two ways of testing the hypervisor:
1. [Using a Docker container](#1-using-a-docker-container)
2. [Using local machine](#2-using-a-local-linux-machine)

## 1. Using a Docker container

A Docker image can been built (with this [Dockerfile](https://github.com/quic/gunyah-support-scripts/tree/develop/gunyah-qemu-aarch64/Dockerfile)) to make the testing easier, without needing to install the tools directly on your machine.

The Dockerfile provided contains:
- Latest QEMU (version required: >= 5.0.0)
- GDB 9.2 (version required: >= 9.2)
- Latest Linux image (to be used as primary VM)
- RAM disk generated with Busybox-1.33.0 (to be used by primary VM)
- Device tree (to be used by primary VM)
- LLVM 10.0.1 toolchain with musl libc for AArch64
- Virtual environment (gunyah-venv)
- Script with the QEMU start command (start_cmd.sh)
- Required environment variables set
- Required dependencies installed

Once a Docker image is built, the required files can be found in `$OUTPUT_DIR` (`/usr/local/src/out`).

### Instructions

1. Build the Docker image:
    [Docker instructions](docker.md)

> Note, make sure that at this point you are running on the Docker container's shell. (e.g. `docker run -it <name>:<tag>`)

2. Download and build the hypervisor source code:
    [Build instructions](build.md)
3. Boot the Gunyah Hypervisor with the Linux VM on the QEMU simulator.

   Use the following QEMU start command:
```bash
qemu-system-aarch64 -machine virt,virtualization=on,gic-version=3,highmem=off \
-cpu max -m size=2G -smp cpus=8 -nographic \
-kernel <path-to-hypvm-elf>/hypvm.elf \
-device loader,file=$OUTPUT_DIR/Image,addr=$LINUX_BASE \
-device loader,file=$OUTPUT_DIR/virt.dtb,addr=$DT_BASE \
-device loader,file=$OUTPUT_DIR/initrd.img,addr=$INITRD_BASE
```

> Note, see [Hardcoded Parameters](#hardcoded-parameters) section for an explanation of why these `_BASE` values have been set as environment variables.

## 2. Using a local Linux machine

1. Install latest QEMU:
    [QEMU instructions](qemu.md)
2. Download and build the hypervisor source code:
    - [Setup Instructions](setup.md)
    - [Build instructions](build.md)
3. Download and build the latest Linux kernel:
    [Linux instructions](linux.md)
4. Create a RAM disk for Linux:
    [RAM disk instructions](ramdisk.md)
5. Generate a device tree with 512M of RAM:

```bash
qemu-system-aarch64 \
-machine virt,virtualization=on,gic-version=3,highmem=off \
-cpu max -m size=512M -smp cpus=8 -nographic \
-kernel <path-to-hypvm-elf>/hypvm.elf \
-append "rw root=/dev/ram rdinit=/sbin/init earlyprintk=serial,ttyAMA0 console=ttyAMA0" \
-machine dumpdtb=virt_qemu.dtb
```

6. Modify the "chosen" node in the device tree based on the created RAM disk:

   Create a device tree overlay with the new "chosen" node values and apply it to the dtb generated in the previous step.

```bash
export INITRD_BASE=0x44400000
export INITRD_SIZE=$(stat -Lc %s <path-to-output-dir>/initrd.img)
export INITRD_END=$(printf "0x%x" $((${INITRD_BASE} + ${INITRD_SIZE})))
cat <<EOF > overlay.dts
/dts-v1/;
/{
fragment@0{
target-path = "/chosen";
__overlay__{
linux,initrd-start = <${INITRD_BASE}>;
linux,initrd-end = <${INITRD_END}>;
};
};
};
EOF
dtc -@  -I dts -O dtb overlay.dts -o overlay.dtbo
fdtoverlay -v -i virt_qemu.dtb -o virt.dtb overlay.dtbo
rm overlay.dts overlay.dtbo virt_qemu.dtb
dtc -I dtb -O dts virt.dtb > virt.dts
cp virt.dts <path-to-output-dir>/.
cp virt.dtb <path-to-output-dir>/.
```

7. Boot the Gunyah Hypervisor with the Linux VM on the QEMU simulator:

```bash
qemu-system-aarch64 \
-machine virt,virtualization=on,gic-version=3,highmem=off \
-cpu max -m size=2G -smp cpus=8 -nographic \
-kernel <path-to-hypvm-elf>/hypvm.elf \
-device loader,file=<path-to-output-dir>/Image,addr=$LINUX_BASE \
-device loader,file=<path-to-output-dir>/virt.dtb,addr=$DT_BASE \
-device loader,file=<path-to-output-dir>/initrd.img,addr=$INITRD_BASE
```

> Note, see [Hardcoded Parameters](#hardcoded-parameters) section for an explanation of why these `_BASE` values have been set as environment variables.

## Hardcoded parameters

In the [Gunyah Hypervisor](https://github.com/quic/gunyah-hypervisor) repository, specifically in `hyp/platform/soc_qemu/src/boot.c`, we currently have hardcoded the base addresses to be used in the QEMU start command. We hope to address this in future contributions such that these values only need to be set in the QEMU command. For now, in case you want to change these values in the start command, please make sure you also modify these values in the code, without overlapping with the range assigned to the hypervisor RAM.

The current hardcoded addresses that need to be used in the QEMU start command are:
(set in [boot.c](https://github.com/quic/gunyah-hypervisor/tree/develop/hyp/platform/soc_qemu/src/boot.c) in function soc_qemu_handle_rootvm_init())
- LINUX_BASE=0x41080000
- DT_BASE=0x44200000
- INITRD_BASE=0x44400000

Current hypervisor RAM range:
(set in [boot.c](https://github.com/quic/gunyah-hypervisor/tree/develop/hyp/platform/soc_qemu/src/boot.c) in function platform_ram_probe())
- 0x80000000..0xBFFFFFFF
