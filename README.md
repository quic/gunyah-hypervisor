![Qualcomm Innovation Center.](docs/images/logo-quic-on%40h68.png)

# Gunyah Hypervisor

*Gunyah* is an Australian Aboriginal word. See: https://en.wiktionary.org/wiki/gunyah

The Gunyah Hypervisor was developed by Qualcomm in Sydney Australia.

## Type-1 Hypervisor Concept

Gunyah is a Type-1 hypervisor, meaning that it is independent of any high-level
OS kernel, and runs in a higher CPU privilege level. It does not depend on any
lower-privileged OS kernel/code for its core functionality. This increases its
security and can support a much smaller trusted computing base than a Type-2
hypervisor.

Gunyah's design principle is not dissimilar to a traditional microkernel in
that it provides only a minimal set of critical services to its clients, and
delegates the provision of non-critical services to non-privileged (or
less-privileged) processes, wherever this is possible without an adverse impact
on performance or security.

The hypervisor uses the CPU's virtualization mode and features to isolate
itself from OS kernels in VMs. On ARM, this includes trapping privileged
registers, using GIC virtualization support, and the Stage-2 MMU to provide
isolated VMs in EL1/0.

## Why Gunyah

- **strong security**: Mobile payments, secure user-interface, and many more security sensitive use-cases all require strong security. Gunyah's design is suited to providing strong isolation guarantees and its small size is conducive to audit.
- **performance**: Mobile devices are particularly demanding. Battery powered devices demand low software overheads to get the most performance per-watt. Gunyah is designed to have high performance with minimal impact to high-level operating systems.
- **modularity**: The hypervisor is designed to be modular, allowing customization and enhancement by swapping out module implementations and adding new feature via new modules.

## Features

- **Threads and Scheduling**: The scheduler schedules virtual CPUs (VCPUs) on physical CPUs and enables time-sharing of the CPUs.
- **Memory Management**: Gunyah tracks memory ownership and use of all memory under its control. Memory partitioning between VMs is a fundamental security feature.
- **Interrupt Virtualization**: All interrupts are handled in the hypervisor and routed to the assigned VM.
- **Inter-VM Communication**: There are several different mechanisms provided for communicating between VMs.
- **Device Virtualization**: Para-virtualization of devices is supported using inter-VM communication. Low level system features and devices such as interrupt controllers are supported with emulation where required.

## Platform Support

Gunyah is architected to support other CPU architectures, so its core design ensures architecture independence and portability in non-architecture specific areas.

Gunyah currently supports ARMv8.2+ platforms as it uses AArch64 EL2 in VHE mode. Some porting is required to support ARMv8.0.

We have developed an initial port of Gunyah to the QEMU ARMv8 simulator. *Note QEMU v5+ is required*. Additional platforms are expected to be supported in future contributions.

## Getting Started
- [Setup Instructions](docs/setup.md)
- [Build Instructions](docs/build.md)
- [Testing Instructions](docs/test.md)
- [Changelog](CHANGELOG.md)

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
