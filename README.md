[<img src="docs/images/logo-quic-on%40h68.png" height="68px" width="393px" alt="Qualcomm Innovation Center" align="right"/>](https://github.com/quic)

# Gunyah Hypervisor

Gunyah is a high performance, scalable and flexible hypervisor built for
demanding battery powered, real-time, safety and security use cases.

The Gunyah Hypervisor open source project provides a reference Type-1
hypervisor configuration suitable for general purpose hosting of multiple
trusted and dependent VMs.

## Gunyah Origins

*Gunyah* is an Australian Aboriginal word. See: https://en.wiktionary.org/wiki/gunyah

The Gunyah Hypervisor was developed by Qualcomm in Sydney Australia.

## Type-1 Hypervisor Concept

Gunyah is a Type-1 hypervisor, meaning that it runs independently of any
high-level OS kernel such as Linux and runs in a higher CPU privilege level
than VMs. It does not depend on any lower-privileged OS kernel/code for its
core functionality. This increases its security and can support a much smaller
trusted computing base than a Type-2 like hosted-hypervisors.

Gunyah's design principle is not dissimilar to a traditional microkernel in
that it provides only a minimal set of critical services to its clients, and
delegates the provision of non-critical services to non-privileged (or
less-privileged) processes, wherever this is possible without an adverse impact
on performance or security.

The hypervisor uses the CPU's virtualization mode and features to isolate
itself from OS kernels in VMs and isolate VMs from each other. On ArM, this
includes trapping and emulating registers as required, virtualizing core
platform devices, Arm's GIC virtualization support, and the CPU's Stage-2 MMU
to provide isolated VMs in EL1/0.

## Why Gunyah

- **Strong security**: Mobile payments, secure user-interface, and many more security sensitive use-cases all require strong security. Gunyah's design is suited to providing strong isolation guarantees and its small size is conducive to audit.
- **Performance**: Mobile devices are particularly demanding. Battery powered devices demand low software overheads to get the most performance per-watt. Gunyah is designed to have high performance with minimal impact to high-level operating systems.
- **Modularity**: The hypervisor is designed to be modular, allowing customization and enhancement by swapping out module implementations and adding new feature via new modules.

## Features

- **Threads and Scheduling**: The scheduler schedules virtual CPUs (VCPUs) on physical CPUs and enables time-sharing of the CPUs.
- **Memory Management**: Gunyah tracks memory ownership and use of all memory under its control. Memory partitioning between VMs is a fundamental security feature.
- **Interrupt Virtualization**: All interrupts are handled in the hypervisor and routed to the assigned VM.
- **Inter-VM Communication**: There are several different mechanisms provided for communicating between VMs.
- **Device Virtualization**: Para-virtualization of devices is supported using inter-VM communication. Low level system features and devices such as interrupt controllers are supported with emulation where required.

## Platform Support

Gunyah is architected to support multiple CPU architectures, so its core design
ensures architecture independence and portability in non-architecture specific
areas.

Gunyah currently supports the ARM64 (ARMv8+) architecure, it uses AArch64 EL2
in VHE mode by default.

We have developed an initial port of Gunyah to the QEMU Arm System emulator.
*Note QEMU v7+ is recommended*. Additional platforms are expected to be
supported in future contributions.

## Getting Started
- [Terminology](docs/terminology.md)
- Other required Gunyah related repositories:
    - [Setup Tools and Scripts](https://github.com/quic/gunyah-support-scripts) (Start setup here..!!)
    - [Hypervisor](https://github.com/quic/gunyah-hypervisor.git) (Gunyah core hypervisor, this repository)
    - [Resource Manager](https://github.com/quic/gunyah-resource-manager.git) (Platform policy engine)
    - [C Runtime](https://github.com/quic/gunyah-c-runtime.git) (C runtime environment for Resource Manager)

- [Setup Instructions](docs/setup.md)
    + [Quick Start Instructions](https://github.com/quic/gunyah-support-scripts/blob/develop/quickstart.md) (will take to the setup tools and scripts repository)
- [Build Instructions](docs/build.md)
- [Status and Changelog](CHANGELOG.md)

## Resources
- [Gunyah Hypercall API](docs/api/gunyah_api.md)

## Contributions
Thank you for your interest in contributing to Gunyah!

Please read our [Contributions Page](CONTRIBUTING.md) for more information on contributing features or bug fixes.

## Team
Gunyah was developed by Qualcomm and aims to be an open and community supported project.

Check out the [AUTHORS](AUTHORS) for major contributors.

## License
Gunyah is licensed on the BSD 3-clause "New" or "Revised" License.  Check out the [LICENSE](LICENSE) for more details.
