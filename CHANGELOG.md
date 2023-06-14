# Status and Changelog

This page documents current status, known issues and work in progress. Some of
these may impact your development or hypervisor usage.

## Open Issues

### 1. Secondary VM support

The Resource Manager being the root-VM, manages creation of the Primary VM
(HLOS) and controls the rights to create additional VMs. In the Gunyah Resource
Manager design, VM management services are provided by the Resource Manager

Gunyah patches are required in Linux and the CrosVM VMM to support SVM loading.

### Known issues:

- Only QEMU serial communication is tested. Using host Linux networking (qemu
  virtio) with adb (network) connection will permit greater flexibility in
  connecting to the device.
- SVM booting with Crosvm uses uart emulation, its very slow.
- Crosvm opens the UART console in the current terminal, so it is via the host
  uart terminal. We have not configured a way to open multiple terminals yet.
- Debugging a system running QEMU with a remote gdb connection is unstable.

### Untested scenarios:

- Launching of multiple SVM's simultaneously from PVM, because of the known
  issue of having only one console available.

### TODO list:

- Hardcoded platform parameters
    + Memory address ranges are hardcoded (get from dtb nodes)
    + Dtb address is hardcoded (get from register)

## Unreleased

Unreleased changes in the `develop` branch may be added here.

## Releases

Individual releases are tagged, and the latest release will be available in the `main` branch.

* No tagged releases have been made at this time.

## Contributions

Significant contributions are listed here.

### Initial Opensource Contribution

This is the initial contribution of source code to the Gunyah Hypervisor.

* Support for QEMU AArch64 Simulator
* Support unmodified Linux Primary VM kernel or with Gunyah patches for VM loading
* Support unmodified Linux Secondary VM kernel
