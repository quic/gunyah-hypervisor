# Testing Gunyah Hypervisor

We provide two ways of testing the hypervisor:
1. [Using a Docker container](#using-a-docker-container)
2. [Using local machine](#using-a-local-linux-machine)

## Using a Docker container

A Docker image can been built with the required compiler and QEMU Arm System emulator.
See [Setup Instructions](setup.md).

## Using a local Linux machine

1. Build and install a recent QEMU (v7.2 is tested):
    - See Docker support scripts for reference
2. Download and build the hypervisor source code:
    - [Setup Instructions](setup.md)
    - [Build instructions](build.md)
3. Download and build the latest Linux kernel:
    [Linux instructions](linux.md)
4. Create a RAM disk for Linux:
    [RAM disk instructions](ramdisk.md)
5. Generate a device tree for the QEMU platform:
    - See Docker support scripts for reference
7. Boot the Gunyah Hypervisor with the Linux VM on the QEMU simulator:
    - See Docker support scripts for reference
