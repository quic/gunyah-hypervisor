# Setup Instructions

The following instructions describe setting up a Linux machine for Gunyah development and testing. You may alternatively wish to use Docker:

[Docker Instructions](docker.md)

## Toolchain
The Gunyah Hypervisor projects use the LLVM 10+ toolchain, cross-compiled for AArch64. Standalone applications (Resource Manager) are built with the musl libc library.

A [script](https://github.com/quic/gunyah-support-scripts/tree/develop/gunyah-qemu-aarch64/llvm_musl_build.sh) to help build a suitable LLVM compiler (with musl libc) can be found in [Gunyah support scripts](https://github.com/quic/gunyah-support-scripts).

```bash
git clone https://github.com/quic/gunyah-support-scripts.git
```

To build the LLVM toolchain, first execute the provided [script](https://github.com/quic/gunyah-support-scripts/tree/develop/gunyah-qemu-aarch64/llvm_musl_build.sh) in the directory where you want it to be installed.

```bash
cd <desired-installation-path>
./<path-to-script>/llvm_musl_build.sh
```

> Note, this script requires cmake to be installed.

This script will generate a `llvm-musl-install` folder in the current directory.

## C Application Sysroot

The Resource Manager needs to be compiled using the libfdt library, cross-compiled for AArch64.

The following instructions indicate how to get the libfdt source code and cross compile it for AArch64:

```bash
git clone https://github.com/dgibson/dtc.git
cd dtc
CC=aarch64-linux-gnu-gcc make install libfdt PREFIX=<path-to-c-application-sysroot>
```

> Note, \<path-to-c-application-sysroot\> refers to the desired path where the built will be installed.

## Set up environment variables
You must set the following environment variables:
- To point to the toolchain (assumed to be Clang 10.0 or later):
```bash
export LLVM=/path/to/llvm10/
```
> Note, when using the toolchain built with the provided script, point to the "llvm-musl-install" generated folder.
> `export LLVM=/path/to/llvm-musl-install`

- To point to the C application sysroot:
```bash
export LOCAL_SYSROOT=/path/to/c-application-sysroot
```

## Install the Python dependencies

Create a virtual environment, activate it, and install the modules used by the auto-generation code:

```bash
python3 -m venv gunyah-venv
. gunyah-venv/bin/activate
pip install -r <path-to-gunyah-src>/tools/requirements.txt
```

We recommend installing the Python environment _outside_ the Gunyah source directory. This is so the automatic dependency detection in the Python scripts ignores modules imported from the virtual environment.
