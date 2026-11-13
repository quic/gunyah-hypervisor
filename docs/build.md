# Build Instructions

These instructions for building the Gunyah Hypervisor can be used for both local builds or building within a Docker container.

## Build Environment

Ensure your build environment is correctly setup. See [Setup Instructions](setup.md).

Always ensure you have activated the `gunyah-venv` *before* running `configure` or building.
```bash
. gunyah-venv/bin/activate
```

## Download the source code

The following repositories are needed to build a Gunyah Hypervisor image:

These should all be cloned into the same top-level directory (this assumed in the Docker setup).

- [Gunyah Hypervisor](https://github.com/quic/gunyah-hypervisor) — The Gunyah Hypervisor.
 ```bash
 git clone https://github.com/quic/gunyah-hypervisor.git hyp
 ```
- [Resource Manager](https://github.com/quic/gunyah-resource-manager) — The privileged root VM and VM manager supporting the Gunyah Hypervisor.
 ```bash
 git clone https://github.com/quic/gunyah-resource-manager.git resource-manager
 ```
- [Gunyah C Runtime](https://github.com/quic/gunyah-c-runtime) — A runtime for light-weight OS-less application VMs.
 ```bash
git clone https://github.com/quic/gunyah-c-runtime.git musl-c-runtime
 ```

## Build Configuration

The Arm FVP Architecture Envelope Model uses `platform=fvp-aem`. See
[the FVP build and boot workflow](fvp.md) for packaging and simulator commands.

The build system has several configuration parameters that must be set:

* `platform`: selects the target hardware platform
* `featureset`:  selects a named hypervisor architecture configuration
* `quality`: specifies the build quality, e.g. `debug`, `production` etc., which modify the build, such as including runtime assertions, compiler optimisations etc.

These parameters must be set on the build system's command line; if one or more
of them is left unset, the build system will print the known values for the
missing parameter and abort. You may specify a comma-separated list to select
multiple values for a parameter, or `all` to select every valid combination.
You may also specify simply `all=true`, which is equivalent to specifying `all`
for every parameter that is not otherwise specified. The when multiple options
are selected, each combination (variant) will be built in separate output
directories under the `build` directory.

## Building

The Gunyah Hypervisor, Resource Manager and C runtime are built separately,
each following the similar build instructions below. These separate images need
to be packaged together into a final boot image.

> IMPORTANT! If making hypervisor public API changes, these changes will need to be updated in the Resource Manager and Runtime sources.

### Building with Ninja

The build system uses [Ninja](https://ninja-build.org/) to execute build tasks. The configuration stage generates build rules which are read by Ninja.

To configure the build system, run `configure.py <configuration>` either in the top-level source repository of the component, or using a relative path from an otherwise empty build directory, specifying the desired configuration parameters.

If `configure.py` is run from the top-level source directory, it will create a build output directory called `build`. If it is run from any other working directory, build outputs will be created in that directory.

For example, in each of the Gunyah Hypervisor, Resource Manager and Gunyah C Runtime source directories, run:
```sh
./configure.py platform=qemu featureset=gunyah-baseline quality=production
```

or to build all available configurations for the QEMU platform:
```sh
./configure.py platform=qemu all=true
```

This will create a `build` directory and Ninja build rules file for each enabled build variant. Generally, the `configure` step only needs to be run once.

> Note, if configuration files are modified, Ninja will rerun `configure.py` with the previous parameters. However, you must manually rerun `configure.py` if you rename or delete an existing module or configuration file, as Ninja will refuse to run if a build configuration file is missing.

To build, run `ninja` from the same working directory as `configure.py`. There is usually no need to specify `-j` or similar, as Ninja will select this automatically. Ninja will incrementally re-build if run again after making code changes.

```sh
ninja
```

To build a specific file (for example, a single variant when multiple variants have been configured), specify its full name as the target on the `ninja` command line.

To clean the build, run `ninja -t clean`. It should not be necessary to do this routinely.

### Building with SCons

The build system also supports integration with existing SCons projects, but the use of SCons for standalone builds is deprecated and is no longer routinely tested. Ninja should be used for all new projects.

## Producing a Boot Image

Once you have built the Gunyah Hypervisor, Resource Manager and C Runtime, a boot image needs be generated.

To reduce the size of the boot image, the generated binaries of Resource Manager and C Runtime need to be stripped with the following commands:
```bash
$LLVM/bin/llvm-strip -o <path-to-resource-manager-src>/build/resource-manager.strip <path-to-resource-manager-src>/build/resource-manager
$LLVM/bin/llvm-strip -o <path-to-c-runtime-src>/build/runtime.strip <path-to-c-runtime-src>/build/runtime
```

The individual executables generated by building [Gunyah Hypervisor](https://github.com/quic/gunyah-hypervisor), [Resource Manager](https://github.com/quic/gunyah-resource-manager), and [Gunyah C Runtime](https://github.com/quic/gunyah-c-runtime) need to be integrated into a single `hypvm.elf` boot image.

You will need the [pyelftools](https://github.com/eliben/pyelftools) Python
module. This should be installed in the gunyah python virtual-env.  However, it
is available from its upstream project:
```bash
cd <tools-directory>
git clone https://github.com/eliben/pyelftools.git
```

To generate `hypvm.elf` boot image run these steps (substituting `<path>`s for each tool / executable):
```bash
cd <path-to-gunyah-hypervisor-src>
tools/elf/package_apps.py \
 -a <path-to-resource-manager-src>/build/resource-manager.strip \
 -r <path-to-c-runtime-src>/build/runtime.strip \
 <path-to-gunyah-hypervisor-src>/build/production/qemu/gunyah-baseline/hyp.elf \
 -o <path-to-destination>/hypvm.elf
```
> Note, you may wish to pick a different hypervisor `hyp.elf` from a different build variant (i.e. `build/debug/qemu/gunyah-baseline/`).
