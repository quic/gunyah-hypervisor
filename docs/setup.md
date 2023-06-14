# Setup Instructions

## Development Environment

We recommend using a Docker container with the required compilers and tools for
convenience.

A separate _Gunyah Support Scripts_ repository is maintained with reference
Docker based environment instructions and scripts.

See:
[Gunyah Support Scripts](https://github.com/quic/gunyah-support-scripts/tree/develop)

```bash
git clone https://github.com/quic/gunyah-support-scripts.git
```

## Custom Dev Environment

If you intend to setup your own development environment, you can follow the
reference Docker setup on your development host. This process is not
documented.

### Toolchain

The Gunyah Hypervisor projects use the LLVM v15 toolchain, cross-compiled for
AArch64 and musl libc. This is due to standalone application VMs (Resource
Manager) are built with a runtime supporting the musl libc library.

### Set up environment variables

You must set the following environment variables:
```bash
export LLVM=/path/to/llvm15/
```
> Note, when using the toolchain built with the provided script, point to the "llvm-musl-install" generated folder.
> `export LLVM=/path/to/llvm-musl-install`

- To point to the C application sysroot:
```bash
export LOCAL_SYSROOT=/path/to/c-application-sysroot
```

### Install the Python dependencies

Create a virtual environment, activate it, and install the modules used by the auto-generation code:

```bash
python3 -m venv gunyah-venv
. gunyah-venv/bin/activate
pip install -r <path-to-gunyah-src>/tools/requirements.txt
```

We recommend installing the Python environment _outside_ the Gunyah source
directory. This is so the automatic dependency detection in the Python scripts
ignores modules imported from the virtual environment.
