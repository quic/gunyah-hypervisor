# Changelog

All notable changes to this project will be documented in this file.

## Open Issues

### 1. Secondary VM support

The Resource Manager being the root-VM, manages creation of the Primary VM
(HLOS) and controls the rights to create additional VMs. In the Gunyah Resource
Manager design, VM management services are provided by the Resource Manager
(although it is technically possible for these rights to be delegated to other
VMs).

The current Resource Manager does not support Secondary VM loading. Support
will be added in a subsequent contribution.

### 2. Virtio support

Virtio support is under development and should be contributed along with secondary VM support.

## Unreleased

Unreleased changes in the `develop` branch may be added here.

## Releases

Individual releases are tagged, and the latest release will be available in the `main` branch.

* No releases have been made at this time.

## Contributions

Significant contributions are listed here.

### Initial Opensource Contribution

This is the initial contribution of source code to the Gunyah Hypervisor.

* Support for QEMU AArch64 Simulator
* Support unmodified Linux Primary VM kernel
