# Running Gunyah on Arm FVP AEM

The Arm Architecture Envelope Model target is `platform=fvp-aem`. It is
available with the `gunyah-baseline` and `unittests` featuresets. The model
settings are in `tools/fvp_aem/fvp_aem_config_v8_9.txt`; they configure two
clusters with four CPUs each and Armv8.9 features.

This flow builds the open-source hypervisor, resource manager, and C runtime
and runs the model on FVP on a Linux host without Docker. It uses the repository
layout described in [build.md](build.md): `hyp`, `resource-manager`,
`musl-c-runtime`, and `gunyah-app-sysroot` under a common parent directory.

## Prerequisites

Install LLVM with an AArch64 C library, Ninja, Python with virtual-environment
support, `dtc`, and the Arm Base RevC 2xAEMvA FVP model. Supply the open-source
application sysroot described in [setup.md](setup.md).

The last FVP validation used FVP Base RevC 2xAEMvA 11.30.27 (installation
suffix `11.30_27`), Snapdragon LLVM 22.1.0, Python 3.10.12,
and DTC 1.6.1. The default setup uses upstream LLVM 22.1.0.
_The Docker environment with LLVM 22.1.0 has not been tested with this flow._

Supply a TF-A BL31 binary built for this FVP model, with the non-secure entry
point at `0x88000000`. For Linux boot, also supply an AArch64 Linux `Image` and
a compatible initramfs. The runner currently recognizes the Buildroot welcome
banner as its Linux completion condition. Existing TF-A and Linux images may
be reused when compatible; successful boot does not establish support for all
guest drivers or devices.

Create a dedicated Python environment outside directories scanned for source
dependencies. Here the environment and build outputs use the ignored `build-*`
directories in the hypervisor checkout:

```sh
cd /path/to/opensource/hyp
python3 -m venv build-fvp-venv
build-fvp-venv/bin/python -m pip install -r tools/requirements.txt
export LLVM=/path/to/llvm/22.1.0/
export LOCAL_SYSROOT=/path/to/opensource/gunyah-app-sysroot
```

## Build and package

```sh
FEATURESET=gunyah-baseline,unittests bash tools/fvp_aem/build.sh
```

The helper configures and builds all components from the open-source sibling
repositories. Outputs are under `build-fvp/` in the hypervisor checkout:

| Component | Output |
| --- | --- |
| Hypervisor | `hyp/debug/fvp-aem/<featureset>/hyp.elf` |
| Resource manager | `resource-manager/fvp-aem/debug/resource-manager` |
| C runtime | `runtime/debug/runtime` |
| Linux base device tree | `resource-manager/fvp-aem/debug/dtb/fvp-aem-simple.dtb` |
| Model boot binary | `hyp/debug/fvp-aem/<featureset>/hypvm.bin` |

`QUALITY` defaults to `debug`, and `FEATURESET` to `gunyah-baseline`.
`BUILD_JOBS` defaults to 8. `FVP_VENV` can override the Python environment path.
The helper sets the environment and clears private toolchain overrides before
building. It does not install tools or dependencies.

For `gunyah-baseline`, packaging combines the newly built hypervisor, stripped
resource manager, and stripped C runtime into `hypvm.elf`, then converts it to
`hypvm.bin`. The `unittests` featureset runs without a root VM and converts
`hyp.elf` directly to `hypvm.bin`. Rebuild and repackage before validating any
source change: the simulator loads the binary, rather than the source or ELF.
Each variant also receives a `build-inputs.txt` with source revisions, worktree
status, tool versions, and image hashes.

## Run headless FVP

Set explicit paths to the installed model and reusable images:

```sh
FVP_MODEL=/path/to/FVP_Base_RevC-2xAEMvA
BL31=/path/to/arm-trusted-firmware/build/fvp/debug/bl31.bin
LINUX_IMAGE=/path/to/Linux/Image
RAMDISK=/path/to/Linux/ramdisk-buildroot.img

build-fvp-venv/bin/python tools/fvp_aem/run.py \
    --mode unittests --model "$FVP_MODEL" --bl31 "$BL31"

build-fvp-venv/bin/python tools/fvp_aem/run.py \
    --mode linux --model "$FVP_MODEL" --bl31 "$BL31" \
    --linux "$LINUX_IMAGE" --ramdisk "$RAMDISK"
```

The runner creates a new directory under `build-fvp/logs/` for every run, with
four UART logs, model output in `fvp.stdout`, and the exact command and image
hashes in `inputs.json`. It stops its own model process on completion, failure,
or timeout. `--timeout` defaults to 180 seconds; `--quality` defaults to `debug`.
The model opens local UART listener sockets even with GUI terminals disabled;
allow those sockets when using a restricted execution environment.

| Input | Physical load address |
| --- | --- |
| TF-A BL31 | `0x04010000` |
| `hypvm.bin` | `0x88000000` |
| Linux base device tree, Linux mode only | `0xa4000000` |
| Linux kernel, Linux mode only | `0xa0000000` |
| Initramfs, Linux mode only | `0xa6000000` |

Gunyah is configured via a shared (with Linux) system device tree. The Linux
base device tree used here comes from the Resource Manager repo.

UART0 is used for TF-A output, UART1 the hypervisor/resource-manager console,
and UART2 the Linux console.

_Note: the runner checks the hypervisor boot banner against the revision being
tested. Unit-test success requires completion messages for all eight
CPUs; Linux boot success requires `Welcome to Buildroot`. Panics and assertions
are failures._

Inspect the logs even after a successful boot to assess device warnings and the
scope of validation.

The 2026-11-13 validation reached the Buildroot login prompt with all eight
CPUs. The complete unit-test run failed its timer lateness check; it must not
be treated as passing based on earlier subsystem completion messages. The
reused Linux/ramdisk combination also logged an initramfs unpacking warning
before reaching login. The base DTS has a fixed `linux,initrd-end`; check that
range against the supplied ramdisk size when preparing other boot inputs.

## Docker environments

The existing support-scripts Docker workflow is documented for QEMU. An FVP
container workflow has not been validated. To adapt it, provide the model and
its runtime dependencies inside the container, mount the open-source sibling
repositories, application sysroot, and boot inputs, and use paths valid inside
the container. Install the pinned Python dependencies and `dtc`, and invoke the
same build and run helpers there. Keep the build output and UART log directory
on a writable mount. Model installation, licensing, and host/container
compatibility must be checked for the chosen Arm distribution before presenting
that setup as a tested alternative.
