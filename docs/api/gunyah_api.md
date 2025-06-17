# Gunyah API

## AArch64 HVC ABI

The Gunyah AArch64 hypercall interface generally follows the ARM AAPCS64 conventions for general purpose register argument and result passing, and preservation of registers, unless explicitly documented otherwise. The hypervisor does not use SIMD, Floating-Point or SVE registers in the hypercall interface.
Gunyah hypercalls use a range of HVC opcode immediate numbers, and reserves the following HVC immediate range:
```
hvc #0x6000
```
through to:
```
hvc #0x61ff
```
Note, Gunyah hypercalls encode the Call-ID in the HVC immediate, encoded within the instruction. This differs from the ARM defined and reserved `HVC #0` namespace, which uses `r0/x0` as the call identifier.

### General-purpose Register

| Register | Role in AAPCS64 | Role in Gunyah HVC |
|--|---|-----|
|     SP_EL0 / SP_EL1          |     The Stack Pointers                                                                                                                                               |     Preserved. (Callee-saved)                                                            |
|     r30 / LR                 |     The Link Register                                                                                                                                                |     Preserved. (Callee-saved)                                                            |
|     r29 / FR                 |     The Frame Register                                                                                                                                               |     Preserved. (Callee-saved)                                                            |
|     r19…r28                  |     Callee-saved registers                                                                                                                                           |     Preserved. (Callee-saved)                                                            |
|     r18                      |     The Platform Register, if needed; otherwise a temporary   register                                                                                               |     Preserved. (Callee-saved)                                                            |
|     r17                      |     IP1     The second intra-procedure-call temporary register (can   be used by call veneers and PLT code); at other times may be used as a   temporary register    |     Temporary. (Caller-saved)                                                            |
|     r16                      |     IP0     The first intra-procedure-call scratch register (can be   used by call veneers and PLT code); at other times may be used as a temporary   register       |     Temporary. (Caller-saved)                                                            |
|     r9…r15                   |     Temporary registers                                                                                                                                              |     Temporary. (Caller-saved)                                                            |
|     r8                       |     Indirect result location register                                                                                                                                |     Temporary. (Caller-saved)                                                            |
|     r0…r7                    |     Parameter / Result registers                                                                                                                                     |     Parameter / Result registers. <br>*Note: Unused result registers not preserved. (Caller-saved)*    |

Preserved registers are unchanged across the HVC call. Temporary registers have an unpredictable value on return and must be saved by the caller before making the HVC call.

### SIMD and Floating-Point and SVE Registers

Gunyah does not use SIMD and Floating-Point (or SVE) registers in the HVC API, and defines all SIMD and Floating-Point and SVE registers as *callee-saved*. (The register values are preserved across HVC calls).

## Common Types

The following types are commonly referred to in the HVC interface.

### Error Result

The hypervisor API, where possible, uses a consistent error return convention and non-overlapping error code values.

When an error result is returned from a hypercall, this is typically in the first result register (`X0`).

When an error result and one or more return values are returned from a hypercall, the error result is placed in (`X0`), and the return values are returned in `X1…X7`.

The error value of zero (`0`) is special and is named “OK”. It indicates that no error occurred, or that the operation was successful.

The error value of (`-1`) is special and indicates that the hypercall call is unimplemented.

### Boolean

A Boolean value with `0` representing *False*, and `1` representing *True*.

### CapID

Capabilities are objects within a Capability Space (CSpace). These are not directly accessible to hypervisor clients (VMs). Instead, a VM uses Capability IDs (CapID) which index into a CSpace and address capabilities in order to interact with the hypervisor’s access control.

A CapID is a register-sized opaque integer value, the value has no meaning outside the associated CSpace.

### Size

A value that represents the size in bytes of an object or buffer in memory.

### VMAddr

A pointer in the VM’s current virtual address space, in the context of the caller.

### VMPhysAddr

A pointer in the VM’s physical address space. In ARMv8 terminology, this is an IPA.

### Access Rights

An enumeration describing the rights given to a memory mapping. Follows the standard RWX format.

### Virtual IRQ Info

A bitfield type that identifies a virtual IRQ within a virtual interrupt controller.

*Virtual IRQ Info:*

| Bits | Mask | Description |
|-|---|-----|
|     23:0             |     `0x00FFFFFF`           |     Virtual IRQ Number. The valid range of this field is   defined by the platform-specific virtual interrupt controller implementation.   The range may be discontiguous, and some sub-ranges may have special meanings   (e.g. there may be a range reserved for VCPU-specific VIRQs).                                                                    |
|     31..24           |     `0xFF000000`           |     Target VCPU index. This is the attachment index of a VCPU   as defined by the hypercall API that configures the virtual interrupt   controller. Valid only if the virtual IRQ number is in a range reserved by   the virtual interrupt controller for VCPU-specific IRQs, and the operation   being performed is implemented for VCPU-specific IRQs.    |
|     63:32            |     `0xFFFFFFFF.00000000`  |     Reserved,   Must be Zero                                                                                                                                                                                                                                                                                                                                |

## Object Rights

Gunyah hypercalls identify objects through capabilities, and use the rights on the capability for operation-type access control. The rights field is a 32-bit bitmap of rights. The following section lists the capability rights values for the various object types.

### Generic Rights

Generic rights are valid for all object types.

| Right             |  Value            |
|-------------------|-------------------|
| Object Activate   |  `0x80000000`     |

### Partition Rights

| Right             |  Value            |
|-------------------|-------------------|
| Partition Object Create | `0x00000001`  |
| Partition Donate        | `0x00000002`  |
| Partition Query         | `0x00000004`  |

### Capability Space Rights

| Right             |  Value            |
|-------------------|-------------------|
| Cspace Cap Create  | `0x00000001` |
| Cspace Cap Delete  | `0x00000002` |
| Cspace Cap Copy    | `0x00000004` |
| Cspace Attach      | `0x00000008` |

### Address Space Rights

| Right             |  Value            |
|-------------------|-------------------|
| Address Space Attach | `0x00000001` |
| Address Space Map | `0x00000002` |
| Address Space Lookup | `0x00000004` |
| Address Space Add VMMIO Range | `0x00000008` |
| Address Space Map Protected | `0x00000010` |
| Address Space Modify Protected | `0x00000020` |
| Address Space Add Info Entry | `0x00000040` |

### Memory Extent Rights

| Right             |  Value            |
|-------------------|-------------------|
| Memory Extent Map    | `0x00000001` |
| Memory Extent Derive | `0x00000002` |
| Memory Extent Attach | `0x00000004` |
| Memory Extent Lookup | `0x00000008` |
| Memory Extent Donate | `0x00000010` |
| Memory Extent Protected Host | `0x00000020` |
| Memory Extent Protected Guest | `0x00000040` |
| Memory Extent Map Private | `0x00000080` |

### Thread Rights

| Right             |  Value            |
|-------------------|-------------------|
| Thread Power On/Off  | `0x00000001` |
| Thread Set Affinity  | `0x00000002` |
| Thread Set Priority  | `0x00000004` |
| Thread Set Timeslice | `0x00000008` |
| Thread Yield To      | `0x00000010` |
| Thread Bind VIRQ     | `0x00000020` |
| Thread Access State  | `0x00000040` |
| Thread Lifecycle     | `0x00000080` |
| Thread Write Context | `0x00000100` |
| Thread Disable       | `0x00000200` |

### Doorbell Rights

| Right             |  Value            |
|-------------------|-------------------|
| Doorbell Send     | `0x00000001` |
| Doorbell Receive  | `0x00000002` |
| Doorbell Bind     | `0x00000004` |

### Message Queues Rights

| Right             |  Value            |
|-------------------|-------------------|
| Message Queue Send      | `0x00000001` |
| Message Queue Receive   | `0x00000002` |
| Message Queue Bind Send | `0x00000004` |
| Message Queue Bind Receive | `0x00000008` |

### Virtual Interrupt Controller Rights

| Right             |  Value            |
|-------------------|-------------------|
| Virtual Interrupt Controller Bind Source | `0x00000001` |
| Virtual Interrupt Controller Attach VCPU | `0x00000002` |

### HW IRQ Rights

| Right             |  Value            |
|-------------------|-------------------|
| HW IRQ Bind VIC   | `0x00000001` |

### Virtual PM Group Rights

| Right             |  Value            |
|-------------------|-------------------|
| Virtual PM Group Attach VCPU | `0x00000001` |
| Virtual PM Group Bind VIRQ   | `0x00000002` |
| Virtual PM Group Query       | `0x00000004` |

### Watchdog Rights

| Right             |  Value            |
|-------------------|-------------------|
| Watchdog Attach VCPU | `0x00000001` |
| Watchdog Bind VIRQ   | `0x00000002` |

### Virtual IO Rights

| Right             |  Value            |
|-------------------|-------------------|
| Virtual IO Bind Backend VIRQ  | `0x00000001` |
| Virtual IO MMIO Bind Frontend VIRQ | `0x00000002` |
| Virtual IO Assert VIRQ   | `0x00000004` |
| Virtual IO Config        | `0x00000008` |

### Virtual GIC ITS

| Right             |  Value            |
|-------------------|-------------------|
| Virtual GIC ITS Bind VIC | `0x00000001` |

### VirtIO IOMMU Rights

| Right             |  Value            |
|-------------------|-------------------|
| VirtIO IOMMU Bind VPCI | `0x00000001` |

### Virtual PCI

| Right             |  Value            |
|-------------------|-------------------|
| Virtual PCI Attach Function | `0x00000001` |
| Virtual PCI Bind VIRQ | `0x00000002` |

### Physical PCIe Root Complex

| Right             |  Value            |
|-------------------|-------------------|
| Physical PCIe RC Create Function | `0x00000001` |
| Physical PCIe RC Set Lockdown State | `0x00000002` |

### Physical PCIe Function

| Right             |  Value            |
|-------------------|-------------------|
| Physical PCIe Function Passthrough | `0x00000001` |
| Physical PCIe Function Attach | `0x00000002` |

## Hypervisor Identification

### Hypervisor API Version and Features

Identifies the hypervisor version and feature set.

|    **Hypercall**:       |     `hypervisor_identify`    |
|-------------------------|------------------------------|
|     Call number:        |     `hvc 0x6000`             |
|     Inputs:             |     None                     |
|     Outputs:            |     X0: Hyp API Info         |
|                         |     X1: API Flags 0          |
|                         |     X2: API Flags 1          |
|                         |     X3: API Flags 2          |


**Types:**

*Hyp API Info:*

| Bits | Mask | Description |
|-|---|-----|
| 13:0            | `0x00001FFF`           | API Version = “1”                                             |
| 14              | `0x00004000`           | 0 = API is Little Endian.   <br>1 = API is Big Endian.        |
| 15              | `0x00008000`           | If set to 1, the API is 64-bit, otherwise 32-bit.             |
| 55:16           | `0xFFFFFF.FFFF0000`    | Reserved                                                      |
| 63:56           | `0xFF000000.00000000`  | Hypervisor   variant.<br>- Unknown = 0x0<br>- Haven = 0x48    |

*API Flags 0:*

| Bits | Mask | Description |
|-|---|-----|
|     0                |     `0x1`                  |     1   = Partition and CSpace APIs supported                            |
|     1                |     `0x2`                  |     1 = Doorbell APIs supported                                          |
|     2                |     `0x4`                  |     1 = Message Queue APIs supported                                     |
|     3                |     `0x8`                  |     1 = Virtual Interrupt Controller and Virtual IRQ   APIs supported    |
|     4                |     `0x10`                 |     1 = Virtual Power Management APIs supported                          |
|     5                |     `0x20`                 |     1 = Virtual CPU APIs supported                                       |
|     6                |     `0x40`                 |     1 = Memory Extent APIs supported                                     |
|     7                |     `0x80`                 |     1 = Tracing control API supported                                    |
|     15:8             |     `0xFF00`               |     Reserved = 0 [TBD   additional API flags]                            |
|     16               |     `0x10000`              |     Reserved                                                             |
|     63:17            |     `0xFFFFFFFF.FFFE0000`  |     Reserved = 0 [TBD   additional API flags]                            |


*API Flags 1:*

| Bits | Mask | Description |
|-|---|-----|
|     0                |     `0x1`                  |     1 = ARM v8.2 SVE support         |
|     1                |     `0x2`                  |     1 = vGIC extended SPI support    |
|     2                |     `0x4`                  |     1 = vGIC extended PPI support    |
|     63:3             |     `0xFFFFFFFF.FFFFFFF8 ` |     Reserved = 0                     |

*API Flags 2:*

| Bits | Mask | Description |
|-|---|-----|
|     63:0             |     `0xFFFFFFFF.FFFFFFFF`  |     Reserved = 0     |


## Partitions

### Partition Object Creation

Allocates a new Partition object from the Partition and creates a master capability in the given CSpace.

|    **Hypercall**:       |      `partition_create_partition`    |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6001`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Partition CapID              |

On successful creation, the object's state is `OBJECT_STATE_INIT` and the newly created master Capability ID is returned.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Capability Space Object Creation

Allocates a new CSpace object from the Partition and creates a master capability in the given CSpace.

|    **Hypercall**:       |      `partition_create_cspace`       |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6002`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Cspace CapID                 |

On successful creation, the object's state is `OBJECT_STATE_INIT` and the newly created master Capability ID is returned.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)


### Address Space Object Creation

Allocates a new Address Space object from the Partition and creates a master capability in the given CSpace.

|    **Hypercall**:       |      `partition_create_addrspace`    |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6003`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

On successful creation, the object's state is `OBJECT_STATE_INIT` and the newly created master Capability ID is returned.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Memory Extent Object Creation

Allocates a new Memory Extent object from the Partition and creates a master capability in the given CSpace.

|    **Hypercall**:       |      `partition_create_memextent`    |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6004`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: MemExtent CapID              |

On successful creation, the object's state is `OBJECT_STATE_INIT` and the newly created master Capability ID is returned.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Thread (Virtual CPU) Object Creation

Allocates a new Thread object from the Partition and creates a master capability in the given CSpace.

|    **Hypercall**:       |      `partition_create_thread`       |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6005`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Thread CapID                 |

On successful creation, the object's state is `OBJECT_STATE_INIT` and the newly created master Capability ID is returned.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Doorbell Object Creation

Allocates a new Doorbell object from the Partition and creates a master capability in the given CSpace.

|    **Hypercall**:       |      `partition_create_doorbell`     |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6006`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Doorbell CapID               |

On successful creation, the object's state is `OBJECT_STATE_INIT` and the newly created master Capability ID is returned.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Message Queue Object Creation

Allocates a new Message Queue object from the Partition and creates a master capability in the given CSpace.

|    **Hypercall**:       |      `partition_create_msgqueue`       |
|-------------------------|----------------------------------------|
|     Call number:        |     `hvc 0x6007`                       |
|     Inputs:             |     X0: Partition CapID                |
|                         |     X1: CSpace CapID                   |
|                         |     X2: Reserved — Must be Zero        |
|     Outputs:            |     X0: Error Result                   |
|                         |     X1: MessageQueue CapID             |

On successful creation, the object's state is `OBJECT_STATE_INIT` and the newly created master Capability ID is returned.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Watchdog object creation

Allocates a new Watchdog object from the Partition and creates a master capability in the given CSpace.

|    **Hypercall**:       |      `partition_create_watchdog`     |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6009`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Watchdog CapID               |

On successful creation, the object's state is `OBJECT_STATE_INIT` and the newly created master Capability ID is returned.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Virtual Interrupt Controller Object Creation

Allocates a new Virtual Interrupt Controller object from the Partition and creates a master capability in the given CSpace.

|    **Hypercall**:       |      `partition_create_vic`          |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x600A`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Virtual IC CapID             |

On successful creation, the object's state is `OBJECT_STATE_INIT` and the newly created master Capability ID is returned.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Virtual PM Group Object Creation

Allocates a new Virtual PM Group object from the Partition and creates a master capability in the given CSpace.

|    **Hypercall**:       |      `partition_create_vpm_group`    |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x600B`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: VPMGroup CapID               |

On successful creation, the object's state is `OBJECT_STATE_INIT` and the newly created master Capability ID is returned.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Virtual IO Backend object creation

Allocates a new Virtual IO Backend object from the Partition and creates a master capability in the given CSpace.

|    **Hypercall**:       |      `partition_create_virtio_backend` |
|-------------------------|----------------------------------------|
|     Call number:        |     `hvc 0x6048`                       |
|     Inputs:             |     X0: Partition CapID                |
|                         |     X1: CSpace CapID                   |
|                         |     X2: Reserved — Must be Zero        |
|     Outputs:            |     X0: Error Result                   |
|                         |     X1: Virtio CapID                   |

This hypercall was formerly named `partition_create_virtio_mmio`.

On successful creation, the object's state is `OBJECT_STATE_INIT` and the newly created master Capability ID is returned.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Virtual PCI object creation

Allocates a new Virtual PCI object from the Partition and creates a master capability in the given CSpace.

|    **Hypercall**:       |      `partition_create_vpci`          |
|-------------------------|---------------------------------------|
|     Call number:        |     `hvc 0x606C`                      |
|     Inputs:             |     X0: Partition CapID               |
|                         |     X1: CSpace CapID                  |
|                         |     X2: Reserved   – Must be Zero     |
|     Outputs:            |     X0: Error Result                  |
|                         |     X1: VPCI CapID                    |

On successful creation, the object's state is `OBJECT_STATE_INIT` and the newly created master Capability ID is returned.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Virtual ITS object creation

Allocates a new Virtual ITS object from the Partition and creates a master capability in the given CSpace.

|    **Hypercall**:       |      `partition_create_vgic_its`      |
|-------------------------|---------------------------------------|
|     Call number:        |     `hvc 0x6071`                      |
|     Inputs:             |     X0: Partition CapID               |
|                         |     X1: CSpace CapID                  |
|                         |     X2: Reserved   – Must be Zero     |
|     Outputs:            |     X0: Error Result                  |
|                         |     X1: VITS CapID                    |

On successful creation, the object's state is `OBJECT_STATE_INIT` and the newly created master Capability ID is returned.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Virtual SMMUv2 object creation

Allocates a new Virtual SMMUv2 object from the Partition and creates a master capability in the given CSpace.

|    **Hypercall**:       |      `partition_create_vsmmuv2`       |
|-------------------------|---------------------------------------|
|     Call number:        |     `hvc 0x6079`                      |
|     Inputs:             |     X0: Partition CapID               |
|                         |     X1: CSpace CapID                  |
|                         |     X2: Reserved   – Must be Zero     |
|     Outputs:            |     X0: Error Result                  |
|                         |     X1: vSMMUv2 CapID                 |

On successful creation, the object's state is `OBJECT_STATE_INIT` and the newly created master Capability ID is returned.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### VirtIO IOMMU Object Creation

Allocates a new VirtIO IOMMU object from the Partition and creates a master capability in the given CSpace.

|    **Hypercall**:       |      `partition_create_virtio_iommu`  |
|-------------------------|---------------------------------------|
|     Call number:        |     `hvc 0x6082`                      |
|     Inputs:             |     X0: Partition CapID               |
|                         |     X1: CSpace CapID                  |
|                         |     X2: Reserved   – Must be Zero     |
|     Outputs:            |     X0: Error Result                  |
|                         |     X1: VirtIO IOMMU CapID            |

On successful creation, the object's state is `OBJECT_STATE_INIT` and the newly created master Capability ID is returned.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Physical PCIe Root Complex object creation

Allocates a new Physical PCIe Root Complex object from the Partition and creates a master capability in the given CSpace.

|    **Hypercall**:       |      `partition_create_pci_host`      |
|-------------------------|---------------------------------------|
|     Call number:        |     `hvc 0x607A`                      |
|     Inputs:             |     X0: Partition CapID               |
|                         |     X1: CSpace CapID                  |
|                         |     X2: Reserved   – Must be Zero     |
|     Outputs:            |     X0: Error Result                  |
|                         |     X1: PCI Root Complex CapID        |

On successful creation, the object's state is `OBJECT_STATE_INIT` and the newly created master Capability ID is returned.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Physical PCIe Function object creation

Allocates a new Physical PCIe Function object from the Partition and creates a master capability in the given CSpace.

|    **Hypercall**:       |      `partition_create_pci_function`  |
|-------------------------|---------------------------------------|
|     Call number:        |     `hvc 0x607E`                      |
|     Inputs:             |     X0: Partition CapID               |
|                         |     X1: CSpace CapID                  |
|                         |     X2: Reserved   – Must be Zero     |
|     Outputs:            |     X0: Error Result                  |
|                         |     X1: PCI Function CapID            |

On successful creation, the object's state is `OBJECT_STATE_INIT` and the newly created master Capability ID is returned.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Partition donation

Donate memory from one partition to another, or to and from a partition's heap.

|    **Hypercall**:       |      `partition_donate`               |
|-------------------------|---------------------------------------|
|     Call number:        |     `hvc 0x6075`                      |
|     Inputs:             |     X0: Partition Donate Flags        |
|                         |     X1: Partition CapID               |
|                         |     X2: Arg2	                  |
|                         |     X3: Phys Base                     |
|                         |     X4: Size                          |
|                         |     X5: Reserved – Must be Zero       |
|     Outputs:            |     X0: Error Result                  |

The purpose of the Arg2 input is dependent on the partition donation type and is described below.

**Types:**

*Partition Donate Flags*

| Bits | Mask | Description |
|-|---|-----|
|     7:0             |     `0xFF`          |     Partition Donate Type               |
|     31:8            |     `0xFFFFFF00`    |     Reserved — Must be Zero             |

*Partition Donate Type*

| Partition Donate Type | Integer Value | Description |
|---|-|-----|
| TO_PARTITION   | 0 | Donate memory from one partition to another. Arg2 is the destination partition cap.      |
| ADD_HEAP       | 1 | Add memory owned by a partition to its heap. Arg2 is the allocator memory attributes.    |
| REMOVE_HEAP    | 2 | Remove memory owned by a partition from its heap. Arg2 is reserved and must be zero.     |

*Allocator Memory Attributes*

| Bits | Mask | Description |
|-|---|-----|
|     3:0             |     `0xF`       |     Allocator Memory Type               |
|     15:4            |     `0xFFF0`    |     Reserved — Must be Zero             |

*Allocator Memory Type*

| Allocator Memory Type | Integer Value | Description |
|---|-|-----|
| HYPERVISOR     | 0 | Memory can be used for general hypervisor allocations.           |
| VM_PAGE_TABLE  | 1 | Memory can only be used for VM stage 2 page table allocations.   |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid donate option, phys base or size.

ERROR_ARGUMENT_SIZE – the Size provided is zero, or leads to an overflow.

ERROR_ARGUMENT_ALIGNMENT – the Phys Base or Size is not page aligned.

ERROR_MEMDB_NOT_OWNER – the donating partition did not have ownership of the specified memory range.

ERROR_DENIED – the partition does not support additional heap regions, or the range isn't suitable for the given allocator memory type.

ERROR_ALLOCATOR_MEM_INUSE – the specified range is still in use by the partition's heap.

Also see: [Capability Errors](#capability-errors)

### Partition query

Query information about a partition and its heap.

|    **Hypercall**:       |      `partition_query`                |
|-------------------------|---------------------------------------|
|     Call number:        |     `hvc 0x6076`                      |
|     Inputs:             |     X0: Partition CapID               |
|                         |     X1: Partition Query Flags         |
|                         |     X2: Addr	                  |
|                         |     X3: Size                          |
|                         |     X4: Arg4                          |
|                         |     X5: Reserved – Must be Zero       |
|     Outputs:            |     X0: Error Result                  |

The purpose of the Addr, Size and Arg4 inputs is dependent on the partition query type described below.

**Types:**

*Partition Query Flags*

| Bits | Mask | Description |
|-|---|-----|
|     7:0             |     `0xFF`          |     Partition Query Type                |
|     31:8            |     `0xFFFFFF00`    |     Reserved — Must be Zero             |

*Partition Query Type*

| Partition Donate Type | Integer Value | Description |
|---|-|-----|
| HEAP_IS_FREE   | 0 | Check if a range of heap memory is free and can be reclaimed. The range is specified via Addr and Size; Arg4 is reserved and must be zero.                  |
| HEAP_STATS     | 1 | Get heap statistics. Addr and Size specify the guest buffer to copy the allocator stats to. Arg4 is the allocator memory attributes of the heap to query.   |

*Allocator Stats*

| Byte Offset | Type | Description |
|-|---|-----|
|   0         | Allocator Stats Info | General information about the allocator stats.               |
|   8         | Size                 | Total amount of memory in the heap.                          |
|   16        | Size                 | Amount of memory that has been allocated from the heap.      |
|   24        | Size                 | Amount of memory reserved for internal use by the heap.      |
|   32        | Size                 | The largest free chunk of memory available for allocation.   |

*Allocator Stats Info*

| Bits | Mask | Description |
|-|---|-----|
|     7:0             |     `0xFF`          |     Version = "1"              |
|     31:8            |     `0xFFFFFF00`    |     Reserved — Must be Zero    |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid query flag, address or size.

ERROR_ARGUMENT_SIZE – the Size provided is zero, or leads to an overflow.

ERROR_ARGUMENT_ALIGNMENT – the Phys Base or Size is not page aligned when querying if a heap range is free.

ERROR_DENIED – the given heap range cannot be queried.

ERROR_ALLOCATOR_MEM_INUSE – the given heap range is still in-use.

ERROR_ADDR_INVALID – some, or the whole of the allocator stats buffer is not mapped.

Also see: [Capability Errors](#capability-errors)

## Object Management

### Activate an Object

Activate an object.

|    **Hypercall**:       |      `object_activate`               |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x600C`                     |
|     Inputs:             |     X0: Cap CapID                    |
|                         |     X1: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the object has moved into `OBJECT_STATE_ACTIVE` state.

ERROR_OBJECT_STATE – if the object is not in OBJECT_STATE_INIT state.

Additional [error codes](#error-code-enumeration) can be returned depending on the type of object to be activated.

Also see: [Capability Errors](#capability-errors)

### Activate an Object from a CSpace

Activate an object from a Cspace.

|    **Hypercall**:       |      `object_activate_from`          |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x600D`                     |
|     Inputs:             |     X0: CSpace CapID                 |
|                         |     X1: Cap CapID                    |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_OBJECT_STATE – if the object is not in OBJECT_STATE_INIT state.

Additional [error codes](#error-code-enumeration) can be returned depending on the type of object to be activated.

Also see: [Capability Errors](#capability-errors)

### Reset an Object

Reset an object to its initial state.

|    **Hypercall**:       |      `object_reset`                  |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x600E`                     |
|     Inputs:             |     X0: Cap CapID                    |
|                         |     X1: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_UNIMPLEMENTED – if functionality not implemented.

Additional [error codes](#error-code-enumeration) can be returned depending on the type of object to be reset.

Also see: [Capability Errors](#capability-errors)

### Reset an Object from a CSpace

Reset an object from a Cspace to its initial state.

|    **Hypercall**:       |      `object_reset_from`             |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x600F`                     |
|     Inputs:             |     X0: CSpace CapID                 |
|                         |     X1: Cap CapID                    |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_UNIMPLEMENTED – if functionality not implemented.

Additional [error codes](#error-code-enumeration) can be returned depending on the type of object to be reset.

Also see: [Capability Errors](#capability-errors)

### TBD object create / Partition

<!-- TODO: -->

0x6008 – `Reserved`

## Communication APIs

### Doorbell Management

#### Doorbell Bind

Binds a Doorbell to a virtual interrupt.

|    **Hypercall**:       |      `doorbell_bind_virq`            |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6010`                     |
|     Inputs:             |     X0: Doorbell CapID               |
|                         |     X1: Virtual IC CapID             |
|                         |     X2: Virtual IRQ Info             |
|                         |     X3: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_VIRQ_BOUND – the specified doorbell is already bound to a VIRQ number.

ERROR_BUSY – the specified VIRQ number is already bound to a source.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Virtual IRQ Info value.

Also see: [Capability Errors](#capability-errors)

#### Doorbell Unbind

Unbinds a Doorbell from a virtual IRQ number.

|    **Hypercall**:       |      `doorbell_unbind_virq`          |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6011`                     |
|     Inputs:             |     X0: Doorbell CapID               |
|                         |     X1: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, or the Doorbell’s interrupt was already unbound.

Also see: [Capability Errors](#capability-errors)

#### Doorbell Send

Sets flags in the Doorbell, and possibly asserts the bound virtual interrupt.

The specified NewFlags will be set (with a bitwise-OR) in the Doorbell flags.

If a VIRQ has been bound to the Doorbell, it will be asserted after setting the flags if either of the following is true:

* The enabled Doorbell flags, as defined by the bitwise-AND of the flags and the EnableFlags argument to the most recent `doorbell_mask` call, is non-zero.
* The VIRQ is edge-triggered.

|    **Hypercall**:       |      `doorbell_send`                              |
|-------------------------|---------------------------------------------------|
|     Call number:        |     `hvc 0x6012`                                  |
|     Inputs:             |     X0: Doorbell CapID                            |
|                         |     X1: NewFlags FlagsBitmap                      |
|                         |     X2: Reserved — Must be Zero                   |
|     Outputs:            |     X0: Error Result                              |
|                         |     X1: OldFlags FlagsBitmap                      |

The returned OldFlags result contains the Doorbell’s previous unmasked flags before the NewFlags were added.

**Types:**

FlagsBitmap: unsigned 64-bit bitmap

**Errors:**

OK – the operation was successful, and the result is valid.

Also see: [Capability Errors](#capability-errors)

#### Doorbell Receive

Reads and clears the flags of the Doorbell, and possibly clears the bound virtual interrupt.

The specified ClearFlags will be set in the Doorbell flags. These must be nonzero; otherwise the call would have no effect.

If a VIRQ has been bound to the Doorbell, it will be cleared after clearing the flags if all of the following are true:

* The enabled Doorbell flags, as defined by the bitwise-AND of the flags and the EnableFlags argument to the most recent `doorbell_mask` call, is zero.
* The VIRQ is level-triggered.

The implementation does not guarantee that the VIRQ is cleared before the call returns. If level-triggered, the VIRQ is guaranteed to have been cleared before either of the following events occurs:

* The VIRQ is delivered after being individually unmasked using a platform-specific Virtual Interrupt Controller API. This includes EOI events, if the implementation supports them.
* An unspecified finite period of time has elapsed after the call is made.

If the VIRQ is edge-triggered, then this call's effect on it is unspecified.

|    **Hypercall**:       |      `doorbell_receive`                               |
|-------------------------|-------------------------------------------------------|
|     Call number:        |     `hvc 0x6013`                                      |
|     Inputs:             |     X0: Doorbell CapID                                |
|                         |     X1: ClearFlags FlagsBitmap – Must be non-zero.    |
|                         |     X2: Reserved — Must be Zero                       |
|     Outputs:            |     X0: Error Result                                  |
|                         |     X1: OldFlags FlagsBitmap                          |

The returned OldFlags result contains the Doorbell’s previous unmasked flags before the ClearFlags were removed.

**Types:**

FlagsBitmap: unsigned 64-bit bitmap

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – if a zero ClearFlags value is passed in.

Also see: [Capability Errors](#capability-errors)

#### Doorbell Reset

Clears all the flags of the Doorbell and sets all bits in the Doorbell’s mask. If there is a pending bound virtual interrupt, it will be de-asserted.

|    **Hypercall**:       |      `doorbell_reset`                |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6014`                     |
|     Inputs:             |     X0: Doorbell CapID               |
|                         |     X1: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

Also see: [Capability Errors](#capability-errors)

#### Doorbell Mask

Sets the Doorbell object’s masks. A Doorbell object has two masks which are configured by the receiver to control which flags it is interested in, and which flags if any should be automatically acknowledged. The EnableMask is the mask of set flags that will cause an assertion of the Doorbell’s bound virtual interrupt. The EnableMask defaults to all-set if it is not configured. The AckMask controls which flags should be automatically cleared when the interrupt is asserted. The Doorbell objects flags are bitwise-NANDed with the AckMask when a interrupt is asserted. Note, the AckMask is unrelated to the EnableMask, and any flags not enabled for asserting an interrupt may be cleared by an AckMask covering those flags. The AckMask defaults to non-set if not configured. Doorbell flags that are not automatically cleared, must be cleared explicitly by the receiver of the virtual interrupt with the Doorbell Receive call prior to acknowledging the virtual interrupt, otherwise the interrupt may be re-asserted.

|    **Hypercall**:       |      `doorbell_mask`                 |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6015`                     |
|     Inputs:             |     X0: Doorbell CapID               |
|                         |     X1: EnableMask FlagsBitmap       |
|                         |     X2: AckMask FlagsBitmap          |
|                         |     X3: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Types:**

FlagsBitmap: unsigned 64-bit bitmap of Boolean flags.

**Errors:**

OK – the operation was successful, and the result is valid.

Also see: [Capability Errors](#capability-errors)

#### Doorbell Halt

<!-- TODO: -->

0x6016 – `Reserved`

### Message Queue Management

#### Message Queue Bind Send vIRQ

Binds a Message Queue send interface to a virtual IRQ number.

|    **Hypercall**:       |      `msgqueue_bind_send_virq`       |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6017`                     |
|     Inputs:             |     X0: Message Queue CapID          |
|                         |     X1: Virtual IC CapID             |
|                         |     X2: Virtual IRQ Info             |
|                         |     X3: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_VIRQ_BOUND – the specified message queue is already bound to a VIRQ number.

ERROR_BUSY – the specified VIRQ number is already bound to a source.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Virtual IRQ Info value, or invalid Message Queue End.

Also see: [Capability Errors](#capability-errors)

#### Message Queue Bind Receive vIRQ

Binds a Message Queue receive interface to a virtual IRQ number.

|    **Hypercall**:       |      `msgqueue_bind_receive_virq`    |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6018`                     |
|     Inputs:             |     X0: Message Queue CapID          |
|                         |     X1: Virtual IC CapID             |
|                         |     X2: Virtual IRQ Info             |
|                         |     X3: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_VIRQ_BOUND – the specified doorbell is already bound to a VIRQ number.

ERROR_BUSY – the specified VIRQ number is already bound to a source.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Virtual IRQ Info value, or invalid Message Queue End.

Also see: [Capability Errors](#capability-errors)

#### Message Queue Unbind Send vIRQ

Unbinds a Message Queue send interface virtual IRQ number.

|    **Hypercall**:       |      `msgqueue_unbind_send_virq`     |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6019`                     |
|     Inputs:             |     X0: Message Queue CapID          |
|                         |     X1: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, or the Message Queue’s send interrupt was already unbound.

Also see: [Capability Errors](#capability-errors)

#### Message Queue Unbind Receive vIRQ

Unbinds a Message Queue receive interface virtual IRQ number.

|    **Hypercall**:       |      `msgqueue_unbind_receive_virq`  |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x601A`                     |
|     Inputs:             |     X0: Message Queue CapID          |
|                         |     X1: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, or the Message Queue’s receive interrupt was already unbound.

Also see: [Capability Errors](#capability-errors)

#### Message Queue Send

Append a message to the tail of a Message Queue, if it is not full. The message is copied from a specified buffer in the caller’s address space. If the Message Queue’s used buffer count was previously below the not-empty interrupt threshold, any receive-side bound virtual interrupt will be asserted.

|    **Hypercall**:       |      `msgqueue_send`                       |
|-------------------------|--------------------------------------------|
|     Call number:        |     `hvc 0x601B`                           |
|     Inputs:             |     X0: Message Queue CapID                |
|                         |     X1: Size Size — Must be non-zero.      |
|                         |     X2: Data VMAddr                        |
|                         |     X3: MsgQSendFlags                      |
|                         |     X4: Reserved — Must be Zero            |
|     Outputs:            |     X0: Error Result                       |
|                         |     X1: NotFull Boolean                    |

**Types:**

*MsgQSendFlags:*

| Bits | Mask | Description |
|-|---|-----|
|     0                |     `0x1`                  |     Message Push                |
|     63:1             |     `0xFFFFFFFF.FFFFFFFE`  |     Reserved — Must be Zero     |

Message Push: If set to 0x1, this flag indicates that the hypervisor should push the message immediately to the receiver. This may cause a receive interrupt to be raised immediately, regardless of any interrupt threshold or interrupt delay configuration.

**Errors:**

OK – the operation was successful.

ERROR_MSGQUEUE_FULL – the Message Queue is full and cannot take the message.

ERROR_ARGUMENT_SIZE – the Size provided is zero, or larger than the Message Queues maximum message size.

ERROR_ADDR_INVALID – some, or the whole of the message buffer is not mapped.

Also see: [Capability Errors](#capability-errors)

#### Message Queue Receive

Fetch a message from the head of a Message Queue, if it is not empty, into a specified buffer in the caller’s address space. If the Message Queue’s used buffer count was previously greater than the not-full interrupt threshold, any send-side bound virtual interrupt will be asserted.

|    **Hypercall**:       |      `msgqueue_receive`              |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x601C`                     |
|     Inputs:             |     X0: Message Queue CapID          |
|                         |     X1: Buffer VMAddr                |
|                         |     X2: MaximumSize Size             |
|                         |     X3: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Size Size                    |
|                         |     X2: NotEmpty Boolean             |

**Errors:**

OK – the operation was successful. In this case, Size is the number of bytes received, and NotEmpty is true if there are more messages available in the queue.

ERROR_MSGQUEUE_EMPTY – the Message Queue is empty and cannot fetch a message.

ERROR_ADDR_INVALID – some, or the whole of the message buffer is not mapped.

ERROR_ADDR_OVERFLOW – the message at the head of the queue is larger than the provided buffer, and could not be received.

Also see: [Capability Errors](#capability-errors)

#### Message Queue Flush

Rmoves all messages from a Message Queue. If the Message Queue was previously non-empty, any send bound virtual interrupt will be deasserted.

|    **Hypercall**:       |      `msgqueue_flush`                |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x601D`                     |
|     Inputs:             |     X0: Message Queue CapID          |
|                         |     X1: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful.

Also see: [Capability Errors](#capability-errors)

#### Messsage Queue Halt

<!-- TODO: -->

0x601E – `Reserved`

#### Message Queue Configure Send

Modify configuration of a Message Queue send interface. The interface allows for configuring of a Message Queue, including setting interrupt thresholds and timeouts.

|    **Hypercall**:       |      `msgqueue_configure_send`                    |
|-------------------------|---------------------------------------------------|
|     Call number:        |     `hvc 0x601F`                                  |
|     Inputs:             |     X0: Message Queue CapID                       |
|                         |     X1: NotFull interrupt threshold               |
|                         |     X2: NotFull threshold delay (microseconds)    |
|                         |     X3: Reserved — Must be -1                     |
|     Outputs:            |     X0: Error Result                              |

Any parameter passed in as -1 indicates no change to the corresponding is requested.

The NotFull threshold modifies the queue used-count at or below which a not-full queue condition is signaled and the send bound virtual interrupt is asserted. This value must be less than the Message Queue’s queue depth.

**Errors:**

OK – the operation was successful.

ERROR_UNIMPLEMENTED – if not implemented.

ERROR_ARGUMENT_INVALID – an argument was invalid.

Also see: [Capability Errors](#capability-errors)

#### Message Queue Configure Receive

Modify configuration of a Message Queue receive interface. The interface allows for configuring of a Message Queue, including setting interrupt thresholds and timeouts.

|    **Hypercall**:       |      `msgqueue_configure_receive`                  |
|-------------------------|----------------------------------------------------|
|     Call number:        |     `hvc 0x6020`                                   |
|     Inputs:             |     X0: Message Queue CapID                        |
|                         |     X1: NotEmpty interrupt threshold               |
|                         |     X2: NotEmpty threshold delay (microseconds)    |
|                         |     X3: Reserved — Must be -1                      |
|     Outputs:            |     X0: Error Result                               |

Any parameter passed in as -1 indicates no change to the corresponding is requested.

The NotEmpty threshold modifies the queue used-count at or above which a not-empty queue condition is signaled and an receive virtual interrupt asserted. This value must be nonzero and no greater than the Message Queue’s queue depth. A special value of -2 sets the threshold to the Message Queue’s queue-depth.

**Errors:**

OK – the operation was successful.

ERROR_UNIMPLEMENTED – if not implemented.

ERROR_ARGUMENT_INVALID – an argument was invalid.

Also see: [Capability Errors](#capability-errors)

#### Configure a Message Queue

Configure a Message Queue whose state is OBJECT_STATE_INIT.

|    **Hypercall**:       |      `msgqueue_configure`            |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6021`                     |
|     Inputs:             |     X0: Message Queue CapID          |
|                         |     X1: MessageQueueCreateInfo       |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*MessageQueueCreateInfo:*

| Bits | Mask | Description |
|-|---|-----|
|     15:0             |     `0x0000FFFF`           |     Queue Depth                 |
|     31:15            |     `0xFFFF0000`           |     Max Message Size            |
|     63:32            |     `0xFFFFFFFF.00000000`  |     Reserved,   Must be Zero    |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_OBJECT_STATE – if the message queue is not in OBJECT_STATE_INIT state.

ERROR_ARGUMENT_INVALID – an argument was invalid. This could be due to Queue Depth or Max Message size.

Also see: [Capability Errors](#capability-errors)

## Capability Management

APIs to manage capabilities in Capability Spaces (CSpace).

### Delete a Capability from a CSpace

Delete a Capability in a CSpace.

|    **Hypercall**:       |      `cspace_delete_cap_from`        |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6022`                     |
|     Inputs:             |     X0: CSpace CapID                 |
|                         |     X1: Cap CapID                    |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |


**Errors:**

OK – the operation was successful, and the result is valid.

Also see: [Capability Errors](#capability-errors)

### Copy a Capability from a specific CSpace

Copy a Capability from one CSpace to another.

|    **Hypercall**:       |      `cspace_copy_cap_from`          |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6023`                     |
|     Inputs:             |     X0: SourceCSpace CapID           |
|                         |     X1: SourceCap CapID              |
|                         |     X2: DestCSpace CapID             |
|                         |     X3: RightsMask                   |
|                         |     X4: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: New CapID                    |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the operation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Revoke a Capability from a CSpace

Revoke a Capability from another CSpace.

|    **Hypercall**:       |      `cspace_revoke_cap_from`        |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6024`                     |
|     Inputs:             |     X0: CSpace CapID                 |
|                         |     X1: Cap CapID                    |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_UNIMPLEMENTED – if functionality not implemented.

`TODO: TBD. Currently unimplemented`

Also see: [Capability Errors](#capability-errors)

### Configure a CSpace

Configure a CSpace whose state is OBJECT_STATE_INIT.

|    **Hypercall**:       |      `cspace_configure`              |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6025`                     |
|     Inputs:             |     X0: CSpace CapID                 |
|                         |     X1: MaxCaps                      |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_OBJECT_STATE – if the Cspace is not in OBJECT_STATE_INIT state.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Max Caps value.

Also see: [Capability Errors](#capability-errors)

### CSpace to Thread Attachment

Configure a CSpace whose state is OBJECT_STATE_INIT.

Attaches a thread to a CSpace. The Cspace object must have been activated before this function is called. The thread object must not have been activated.

|    **Hypercall**:       |      `cspace_attach_thread`          |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x603e`                     |
|     Inputs:             |     X0: CSpace CapID                 |
|                         |     X1: Thread CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_OBJECT_STATE – The Thread object has already been activated, or the Cspace object has not yet been activated.

Also see: [Capability Errors](#capability-errors)

### Revoke Children Capabilities from a master capability in a CSpace

Revoke children Capabilities from a CSpace.

|    **Hypercall**:       |      `cspace_revoke_caps_from`       |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6059`                     |
|     Inputs:             |     X0: CSpace CapID                 |
|                         |     X1: MasterCap CapID              |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

Also see: [Capability Errors](#capability-errors)

## Interrupt Management

### Virtual IRQ Bind to controller

Binds a virtual IRQ to an IRQ source. The source may be any hypervisor object that generates IRQs, including an object representing a hardware IRQ line. For objects that have more than one IRQ source, the index argument may be used to select the source to operate on; otherwise, the index should be set to zero.

This hypercall was previously known as `hwirq_bind_virq`.

Some hypervisor object types have their own IRQ binding hypercalls, which should be used instead. In future, these will be deprecated and replaced with this hypercall.

|    **Hypercall**:       |      `vic_bind_virq`                 |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6026`                     |
|     Inputs:             |     X0: IRQSource CapID              |
|                         |     X1: Virtual IC CapID             |
|                         |     X2: Virtual IRQ Info             |
|                         |     X3: Index                        |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_VIRQ_BOUND – the specified VIRQ number is already bound to a source.

ERROR_BUSY – the specified VIRQ number is already bound to a source.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Virtual IRQ Info value.

Also see: [Capability Errors](#capability-errors)

### Virtual IRQ Unbind from controller

Unbinds a virtual IRQ from an IRQ source. The source may be any hypervisor object that generates IRQs, including an object representing a hardware IRQ line. For objects that have more than one IRQ source, the index argument may be used to select the source to operate on; otherwise, the index should be set to zero.

This hypercall was previously known as `hwirq_unbind_virq`.

Some hypervisor object types have their own IRQ unbinding hypercalls, which should be used instead. In future, these will be deprecated and replaced with this hypercall.


|    **Hypercall**:       |      `vic_unbind_virq`               |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6027`                     |
|     Inputs:             |     X0: IRQSource CapID              |
|                         |     X1: Index                        |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, or the hardware IRQ was already unbound.

Also see: [Capability Errors](#capability-errors)

### Configure a Virtual Interrupt Controller

Configure a Virtual Interrupt Controller whose state is OBJECT_STATE_INIT.

This call sets the maximum number of VCPUs that can be attached to the Virtual Interrupt Controller and receive interrupts from it. It also sets the maximum number of shared (non-VCPU-specific) VIRQ sources that can be registered for delivery though the Virtual Interrupt Controller.

Note that both of these numbers may have implementation-defined upper bounds. Also note that the VIRQ numbers implemented by the controller do not necessarily range from 0 to the specified maximum and may not be contiguous; for example, for a virtual ARM GICv3.1, shared VIRQs are numbered in the ranges 32–1019 and 4096–5119.

|    **Hypercall**:       |      `vic_configure`                 |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6028`                     |
|     Inputs:             |     X0: VIC CapID                    |
|                         |     X1: MaxVCPUs                     |
|                         |     X2: MaxSharedVIRQs               |
|                         |     X3: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – A configuration value was out of range.

ERROR_OBJECT_STATE – The Virtual Interrupt Controller object has already been activated.

Also see: [Capability Errors](#capability-errors)

### Virtual Interrupt Controller to VCPU Attachment

Attaches a VCPU to a Virtual Interrupt Controller. The Virtual Interrupt Controller object must have been activated before this function is called. The VCPU object must not have been activated. An attachment index must be specified which is a non-negative integer less than the MaxVCPUs value used to configure the controller.

|    **Hypercall**:       |      `vic_attach_vcpu`               |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6029`                     |
|     Inputs:             |     X0: Virtual IC CapID             |
|                         |     X1: VCPU CapID                   |
|                         |     X2: Index                        |
|                         |     X3: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_ARGUMENT_INVALID – the specified attachment index is outside the range supported by this Virtual Interrupt Controller.

ERROR_OBJECT_STATE – The VCPU object has already been activated, or the Virtual Interrupt Controller object has not yet been activated.

Also see: [Capability Errors](#capability-errors)

### MSI Source to Virtual Interrupt Controller Attachment

Attaches a message-signalled interrupt (MSI) source object to a Virtual Interrupt Controller, permitting interrupt messages from the source to be routed to virtual interrupts. The Virtual Interrupt Controller object must have been activated before this function is called. An attachment index must be specified in the Source Configuration which is unique among the MSI source attachments to the controller. If the MSI source has a memory-mapped interface, the attachment index may be used to determine its address in the VM address space.

Each MSI source capability represents one or more devices or buses. In the current implementation, the only type of MSI source supported is a GICv4 virtual ITS. Virtual ITSes can be allocated via the partition_create_vgic_its hypercall.

|    **Hypercall**:       |      `vic_bind_msi_source`                                    |
|-------------------------|---------------------------------------------------------------|
|     Call number:        |     `hvc 0x6056`                                              |
|     Inputs:             |     X0: Virtual IC CapID                                      |
|                         |     X1: MSI Source (platform-specific object type)   CapID    |
|                         |     X2: Source Configuration                                  |
|     Outputs:            |     X0: Error Result                                          |

**Types:**

*Source Configuration:*

| Bits | Mask | Description |
|-|---|-----|
|     15:0             |     `0xFFFF`               |     Index                       |
|     63:16            |     `0xFFFFFFFF.FFFF0000`  |     Reserved — Must be Zero     |

Index: Indicates that the attachment index of the MSI source. If a virtual ITS is attached as MSI source it corresponds to the virtual ITS number. If the virtual ITS has a memory-mapped interface, the attachment index may be used to determine its address in the VM address space.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_ARGUMENT_INVALID – the specified attachment index is outside the range supported by this Virtual Interrupt Controller.

ERROR_OBJECT_STATE – The VCPU object has already been activated, or the Virtual Interrupt Controller object has not yet been activated.

Also see: [Capability Errors](#capability-errors)

### Platform Devices to Virtual ITS Binding

Attaches one or multiple platform devices with consecutive device IDs to a virtual ITS, permitting events from the specified devices to be sent to the virtual ITS. The virtual ITS can then be configured by a guest to generate virtual LPIs based on these events. A capability for the physical ITS the platform device's controller is attached to must be specified. Capabilities for all physical ITSes in the system are provided to the root VM at boot time.

|    **Hypercall**:       |      `vgic_its_bind_devices`                                  |
|-------------------------|---------------------------------------------------------------|
|     Call number:        |     `hvc 0x6072`                                              |
|     Inputs:             |     X0: Virtual ITS CapID                                     |
|                         |     X1: Physical ITS CapID                                    |
|                         |     X2: First Device ID                                       |
|                         |     X3: Number of Consecutive Devices                         |
|                         |     X4: Reserved — Must be Zero                               |
|     Outputs:            |     X0: Error Result                                          |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_ARGUMENT_INVALID – the specified devices or the virtual ITS or the physical ITS were not found.

ERROR_BUSY — the virtual ITS is already assigned to a different physical ITS or one of the devices is already attached to a different virtual ITS.

### Platform Devices Unbinding from Virtual ITS

Detaches one or multiple platform devices with consecutive device IDs from a virtual ITS. Returns the number of successfully detached devices, even in case an error was encountered while detaching further devices.

|    **Hypercall**:       |      `vgic_its_unbind_devices`                                |
|-------------------------|---------------------------------------------------------------|
|     Call number:        |     `hvc 0x6073`                                              |
|     Inputs:             |     X0: Virtual ITS CapID                                     |
|                         |     X1: First Device ID                                       |
|                         |     X2: Number of Consecutive Devices                         |
|                         |     X3: Reserved — Must be Zero                               |
|     Outputs:            |     X0: Error Result                                          |
|                         |     X1: Number of Successfully Detached Devices               |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – the specified devices or the virtual ITS were not found or were already attached to a different virtual ITS.

## Address Space Management

### Address Space to Thread Attachment

Attaches an address space to a thread. The address space object must have been activated before this function is called. The thread object must not have been activated. It will be detached only during the deactivation of the thread.

|    **Hypercall**:       |      `addrspace_attach_thread`       |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x602a`                     |
|     Inputs:             |     X0: Address Space CapID          |
|                         |     X1: Thread CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to a thread of kind different from VCPU.

ERROR_OBJECT_STATE – The Thread object has already been activated, or the Address Space object has not yet been activated.

Also see: [capability errors](#capability-errors)

### Address Space Map

Map a memory extent into a specified address space. By default, the entire memory extent is mapped, except for any carveouts contained within the extent.

#### Partial Mapping

If the Partial flag is set in Map Flags, only the range of the memory extent specified by Offset and Size will be mapped. If not set, these arguments are ignored. Partial mappings are only supported by sparse memory extents.

#### Private Mapping

If the Private flag is set in Map Flags, then the map operation creates a private memory mapping in a protected guest VM. This is mutually exclusive with the VMMIO flag.

This flag changes the capability rights required for both the Address Space and Memory Extent CapIDs to the protected and private map rights, respectively, instead of regular map rights.

Apart from the capability rights, map operations that have the Private flag set differ from regular map operations in the following ways:

* The memory region must not already be mapped at a different address in the specified address space.
* The memory region must not already be mapped in any other address space.
* The memory region must be wholly owned by the specified memory extent.
* The specified address range must have previously been marked as paged memory by a call to `addrspace_configure_range`.
* The Kernel Access field of the Map Attributes must specify write permission. Note that this may also constrain the User Access field of the Map Attributes, if the architecture requires the fields to be consistent.
* The hypervisor will ensure that the memory is cache-coherent prior to mapping it.
* Each mapped page will be marked as private and locked.

Marking a page as private has the following effects:

* A map or unmap operation that removes or replaces the page will zero it, unless the page is unlocked and sanitisation was explicitly disabled when it was unlocked. This includes all map and unmap operations performed implicitly by `memextent_donate` or during the lifecycle of any Memory Extent or Address Space object.
* If the page is not locked, it will be automatically locked if it is written by the guest VM. It may also be marked as locked if it is read or executed by the guest VM. If the page has recently been unlocked by a call to `addrspace_modify_pages`, the automatic locking is subject to the synchronisation guarantees documented for that call.

Marking a page as locked has the following effects:

* A map or unmap operation with the Private flag set cannot remove or replace the page.
* A call to `addrspace_modify_pages` can mark the page as unlocked, and optionally also exclude it from sanitisation.

Note that it is not specified whether a private page is initially locked when mapped, or whether it will be automatically locked upon a read access by the guest. However, a private page will never be automatically locked by a speculative memory access.

The size of the page that is automatically marked as locked upon a memory access is not specified. It may not be the smallest page size of the address space.

If a private page is marked as locked, the guest may unlock it by calling `addrspace_modify_pages`.

#### VMMIO Mapping

If the VMMIO flag is set in Map Flags, then the map operation creates a Virtual Memory-Mapped IO mapping in a protected guest VM. This is mutually exclusive with the Private flag.

This flag changes the capability right required for the Address Space CapID to the protected map right instead of the regular map right. Unlike the Private flag, it does _not_ change the right required for the Memory Extent CapID.

Apart from the capability rights, map operations that have the VMMIO flag set differ from regular map operations in the following ways:

* The specified address range must have previously been marked as VMMIO by a call to `addrspace_configure_range`.
* The Kernel Access and User Access fields of the Map Attributes must not specify execute permission.

#### Synchronisation

If successful, the hypervisor will automatically synchronise with other cores to ensure they have completed any successful accesses to pages that were previously mapped in the given address range and were implicitly unmapped by this map operation. Any subsequent accesses to those pages will access the new mapping. Note that in some cases, due to architectural limitations, concurrent accesses to pages which are updated by the map operation or pages adjacent to the mapped pages may fault. If possible, the hypervisor will detect and retry such transiently faulting accesses.

The implicit synchronisation is skipped if the NoSync flag is set. In this case, the caller can synchronise a batch of map and unmap operations either by making a `memextent_modify` call, or by making the final map or unmap call in the batch with the NoSync flag clear.

#### Rollback on Failure

If an error occurs and the Private flag is not set, then any mappings that were created by this operation prior to the failure will be removed. Any existing mappings that were removed and replaced by the failed operation will not be restored.

Note that mappings created by a failed operation may be briefly accessible in the address space prior to being removed. If the NoSync flag is set, then accesses to the mapped pages may still be in progress when the call returns.

If the Private flag is set, then any mappings created by this operation will remain in place even if an error occurs.

|    **Hypercall**:       |      `addrspace_map`                 |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x602b`                     |
|     Inputs:             |     X0: Address Space CapID          |
|                         |     X1: Memory Extent CapID          |
|                         |     X2: Base VMAddr                  |
|                         |     X3: Map Attributes               |
|                         |     X4: Map Flags                    |
|                         |     X5: Offset                       |
|                         |     X6: Size                         |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*Map Attributes:*

| Bits | Mask | Description |
|-|---|-----|
|     2..0             |     `0x7`                  |     User Access (if Supported)    |
|     6..4             |     `0x70`                 |     Kernel Access                 |
|     23:16            |     `0xFF0000`             |     Memory Type                   |
|     63:24,15:7,3     |     `0xFFFFFFFF.0000FF88`  |     Reserved,   Must be Zero      |

*Map Flags:*

| Bits | Mask | Description |
|-|---|-----|
|     0                |     `0x1`                  |     Partial                       |
|     1                |     `0x2`                  |     Private (protected)           |
|     2                |     `0x4`                  |     VMMIO (protected)             |
|     31               |     `0x80000000`           |     NoSync                        |
|     30:3             |     `0x7FFFFFF8`           |     Reserved, Must be Zero        |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Address Space.

ERROR_MEMEXTENT_MAPPINGS_FULL – the memory extent has exceeded its mappings capacity. Currently it can have up to 4 mappings.

ERROR_DENIED – the specified Address Space is not allowed to execute map operations, or the Private or VMMIO flag was set and the specified range has not been correspondingly configured as Private or VMMIO.

ERROR_ARGUMENT_ALIGNMENT – the specified base address is not page size aligned.

ERROR_ADDR_OVERFLOW – the specified base address may cause an overflow.

Also see: [capability errors](#capability-errors)

### Address Space Unmap

Unmaps a memory extent from a specified address space. By default, the entire memory extent is unmapped starting at a specified base IPA, except for any carveouts contained within the extent.

#### Partial Unmapping

If the Partial flag is set in Map Flags, only the range of the Memory Extent specified by Offset and Size will be unmapped. If not set, these arguments are ignored. Partial unmappings are only supported by sparse memory memextents.

#### Whole Extent Unmapping

If the Whole Extent flag is set in Map Flags, the Base argument will be ignored, and every mapping of the extent in the given address space will be removed regardless of its IPA. This flag is mutually exclusive with the Partial flag.

#### Private Unmapping

If the Private flag is set in Map Flags, then the operation attempts to remove a previously created private memory mapping from a protected address space. This is mutually exclusive with the VMMIO flag.

This flag changes the capability rights required for both the Address Space and Memory Extent CapIDs to the protected and private map rights, respectively, instead of regular map rights.

This flag also restricts the operation to only unmapping pages that were created by an `addrspace_map` call with the Private flag set, and which have not been locked or have been unlocked by an `addrspace_modify_pages` call after they were last locked; see that hypercall for more details. If a private unmap operation finds a mapped page that matches the specified memory range but is either locked or not private, it will fail with `ERROR_BUSY`.

#### VMMIO Unmapping

If the VMMIO flag is set in Map Flags, then the operation attempts to remove a mapping from a protected VMMIO region. This is mutually exclusive with the Private flag.

This flag changes the capability right required for the Address Space CapID to the protected map right instead of the regular map right. Unlike the Private flag, it does _not_ change the right required for the Memory Extent CapID.

This flag also restricts the operation to only unmapping pages from an address range that was previously marked as VMMIO by a call to `addrspace_configure_range`. Note that unlike the Private flag, it does _not_ require the pages to be unlocked by `addrspace_modify_pages`.

#### Synchronisation

If successful, the hypervisor will automatically synchronise with other cores to ensure they have completed any successful accesses to pages that were unmapped by this map operation. Any subsequent accesses to those pages will fault. Note that in some cases, due to architectural limitations, concurrent accesses to pages adjacent to the unmapped pages may fault. If possible, the hypervisor will detect and retry such transiently faulting accesses.

The implicit synchronisation is skipped if the NoSync flag is set. In this case, the caller can synchronise a batch of map and unmap operations either by making a `memextent_modify` call, or by making the final map or unmap call in the batch with the NoSync flag clear.

|    **Hypercall**:       |      `addrspace_unmap`               |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x602c`                     |
|     Inputs:             |     X0: Address Space CapID          |
|                         |     X1: Memory Extent CapID          |
|                         |     X2: Base VMAddr                  |
|                         |     X3: Unmap Flags                  |
|                         |     X4: Offset                       |
|                         |     X5: Size                         |
|     Outputs:            |     X0: Error Result                 |

*Unmap Flags:*

| Bits | Mask | Description |
|-|---|-----|
|     0                |     `0x1`                  |     Partial                       |
|     1                |     `0x2`                  |     Private (protected)           |
|     2                |     `0x4`                  |     VMMIO (protected)             |
|     3                |     `0x4`                  |     VMMIO (protected)             |
|     31               |     `0x80000000`           |     NoSync                        |
|     30:4             |     `0x7FFFFFF0`           |     Reserved, Must be Zero        |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Address Space or a non-existing mapping.

ERROR_DENIED – the specified Address Space is not allowed to execute map operations, or the VMMIO flag was set and the specified range has not been correspondingly configured as VMMIO.

ERROR_BUSY — the Private flag was specified, and a matching mapping was found that was either locked, or not private.

ERROR_ARGUMENT_ALIGNMENT – the specified base address is not page size aligned.

Also see: [capability errors](#capability-errors)

### Address Space Update Access

Update access rights on an existing mapping.

This call has a similar effect to `addrspace_map`, except that the mapping is required to already exist, and to be unchanged by this call except for the Attributes.

The semantics of the Map Flags argument are the same as for `addrspace_map`. See the description of that hypercall for further details.

|    **Hypercall**:       |      `addrspace_update_access`       |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x602d`                     |
|     Inputs:             |     X0: Address Space CapID          |
|                         |     X1: Memory Extent CapID          |
|                         |     X2: Base VMAddr                  |
|                         |     X3: Update Attributes            |
|                         |     X4: Map Flags                    |
|                         |     X5: Offset                       |
|                         |     X6: Size                         |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*Update Attributes:*

| Bits | Mask | Description |
|-|---|-----|
|     2..0             |     `0x7`                  |     User Access (if Supported)    |
|     6..4             |     `0x70`                 |     Kernel Access                 |
|     63:7,3           |     `0xFFFFFFFF.FFFFFF88`  |     Reserved,   Must be Zero      |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Address Space or a non-existing mapping.

ERROR_ARGUMENT_ALIGNMENT – the specified base address is not page size aligned.

ERROR_DENIED – the specified Address Space is not allowed to update access of mappings.

Also see: [capability errors](#capability-errors)

### Address Space Modify Pages

Perform one or more of the following operations on pages in a specified range of the address space:
* Unlock private pages, allowing a subsequent `addrspace_unmap` operation with the Private flag set to unmap them.
* Optionally mark unlocked private pages as not requiring sanitisation.

These operations may fail after partial completion. To allow the operation to be retried or resumed after a failure, this API returns the size of the address space range that was not modified prior to the failure. This value is always 0 on success.

#### Unlocking

If the Unlock flag is set in the Modify Flags argument, this call will unlock any locked private pages in the specified address range. The host VM can then use an `addrspace_unmap` operation with the Private flag set to reclaim those pages, without having the right to unconditionally unmap them (by calling `addrspace_unmap` with the Private flag unset). Note that the latter right would also give the host VM the ability to read or modify the contents of the guest VM's memory, so this call is necessary to protect the guest VM from direct access by the host VM.

This call operates directly on a virtual address range in an Address Space, and therefore does not require any knowledge of the Memory Extent objects providing the physical memory backing the unlocked mappings.

The hypervisor does not guarantee that all VCPUs in the guest VM have completed any concurrent accesses to an unlocked page before it is unlocked, and therefore before the host VM reclaims it. It is the caller's responsibility to ensure this is the case.

Additionally, a call made with the No Unlock Sync flag set does not provide any guarantee that the unlocked pages will be automatically re-locked on the next write access.

A call made with the No Unlock Sync flag clear and the Unlock flag set guarantees that all pages unlocked by this or any preceding call will be automatically re-locked if they are written after the call returns success but have not yet been unmapped or re-locked by the host VM. This is the case even if the call does not unlock any pages because the specified size is zero.

#### Sanitisation

Normally, private pages are zeroed by the hypervisor when they are unmapped. This will happen regardless of whether the page is unmapped by an explicit `addrspace_unmap` operation with or without the Private flag, by replacement of the page with an `addrspace_map` operation specifying different physical memory, or by a memory ownership change caused by a Memory Extent operation such as `memextent_donate`.

If the Do Not Sanitise flag is set in the Modify Flags argument, then any pages unlocked by the operation will also be marked as not requiring sanitisation when they are unmapped. Note that this will allow the host VM to access the contents of those pages after reclaiming them.

Sanitisation of pages during unmapping is performed with locks held in the hypervisor and may cause operations on large address ranges to be interrupted and restarted, which is potentially expensive. Therefore, guest VMs are recommended to perform their own sanitisation, by zeroing the pages and cleaning the CPU caches prior to unlocking the pages with the Do Not Sanitise flag set.

|    **Hypercall**:       |      `addrspace_modify_pages`        |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6069`                     |
|     Inputs:             |     X0: Address Space CapID          |
|                         |     X1: Base VMAddr                  |
|                         |     X2: Size                         |
|                         |     X3: Modify Flags                 |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Size Remaining               |

**Types:**

*Modify Flags:*

| Bits | Mask | Description |
|-|---|-----|
| 0    | `0x1`                  | Unlock                  |
| 2    | `0x4`                  | No Unlock Sync          |
| 3    | `0x8`                  | Do Not Sanitise |
| 63:4,1 | `0xFFFFFFFF.FFFFFFF2` | Reserved — Must be Zero |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Address Space, or unrecognised bits set in Unlock Flags.

ERROR_DENIED – the specified Address Space is not allowed to execute map operations.

ERROR_ARGUMENT_ALIGNMENT – the specified base address is not page size aligned.

ERROR_ADDR_OVERFLOW – the specified base address and size may cause an overflow.

Also see: [capability errors](#capability-errors)

### Configure an Address Space

Configure an address space whose state is OBJECT_STATE_INIT.

|    **Hypercall**:       |     `addrspace_configure`            |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x602e`                     |
|     Inputs:             |     X0: Address Space CapID          |
|                         |     X1: VMID                         |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Types:**

16-Bit VMID, upper bits reserved and must be zero.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_OBJECT_STATE – the Address Space object has already been activated.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid VMID.

Also see: [capability errors](#capability-errors)

### Configure the information area of an Address Space

Specifies the address of the read-only hypervisor information area of an address space, and the Memory Extent object to be used as storage for the information area. The state of the address space must be OBJECT_STATE_INIT.

The specified Memory Extent must be a basic memory extent that permits cacheable write accesses. Its size determines the size of the information area, and there must be a valid IPA range starting at the specified IPA with the extent's size.

The caller is responsible for actually mapping the specified extent into the address space after it is activated. This should be a read-only mapping.

|    **Hypercall**:       |     `addrspace_configure_info_area`  |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x605b`                     |
|     Inputs:             |     X0: Address Space CapID          |
|                         |     X1: Info area memextent CapID    |
|                         |     X2: Info area IPA                |
|                         |     X3: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – The operation was successful, and the result is valid.

ERROR_OBJECT_STATE – The Address Space object has already been activated.

ERROR_ARGUMENT_ALIGNMENT – The provided IPA is not page-aligned.

ERROR_ADDR_OVERFLOW – The range beginning at the specified IPA extends past the end of the address space.

ERROR_MEMEXTENT_TYPE – The specified Memory Extent object is not a basic extent.

ERROR_DENIED – The specified Memory Extent object does not have write access to the memory or does not permit cacheable mappings.

Also see: [capability errors](#capability-errors)

### Locate the Information Area

Returns the configured IPA range of the information area of the caller's address space. No capability is required to perform this operation.

|    **Hypercall**:       |     `addrspace_find_info_area`       |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x606a`                     |
|     Inputs:             |     X0: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Info area IPA                |
|                         |     X2: Info area size               |

The information area starts with a variable-length array of info area entry descriptors. Each descriptor is 16 bytes long and has the following format:

| Bits | Description |
|-|-----|
| 31..0  | Entry Type |
| 63..32 | Payload size in bytes |
| 95..64 | Payload offset from info area base |
| 126..96 | Reserved — Must be Zero |
| 127 | Valid |

If the offset field is zero, then the entry is the last in the array, and all other fields must be ignored. Otherwise, if the Valid bit is clear or the Entry Type is unknown, the entry should be ignored.

An appropriate memory fence should separate the read of the Valid bit of the entry descriptor from any access to the Entry Type or payload. The barrier should be equivalent to a call to the C11 standard function `atomic_thread_fence(memory_order_seq_cst)`.

If the information area is accessed through a non-cacheable mapping or with the caches disabled, then it must be cleaned in the caches first. On a target that has virtually indexed caches or which does not support cleaning of caches through an uncached mapping, the hypervisor will clean the caches after writing to the information area.

**Errors:**

OK – The operation was successful, and the result is valid.

ERROR_ADDR_INVALID – The caller has no information area.

### Add an Information Area Entry

Allocates a new entry in an Address Space's information area, and copies the provided payload into it.

The information area is intended to be read-only for the guest VM and append-only for the hypervisor and management VM. The info area is intended for providing low-level hypervisor configuration to a VM. It is not intended to replace DTB or other mechanisms for conveying general static VM configuration.  It is not possible to remove or amend an entry after it has been added to the information area.

The hypervisor info area allocator may result in unused info_area data due to the use of large alignments. The hypervisor does not guarantee to use this unused area for subsequently added small entries. Care should be used with large alignments to prevent exhausting the available info area.

|    **Hypercall**:       |     `addrspace_info_area_add_entry`  |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x606b`                     |
|     Inputs:             |     X0: Address Space CapID          |
|                         |     X1: Entry Type                   |
|                         |     X2: Payload Buffer VMAddr        |
|                         |     X3: Data Info                    |
|                         |     X4: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Info entry IPA               |

*Entry Type:*

| Bits |Description |
|-|-----|
| 15:0 | Id |
| 31:16 | Owner |

| Owner | Value |
| OWNER_INVALID |  0 |
| OWNER_GUNYAH | 1 |
| OWNER_ROOTVM | 2 |
| OWNER_RM | 3 |
| OWNER_QCRM | 16 |
| OWNER_DEV | 65535 |

*Data Info:*

| Bits |Description |
|-|-----|
| 31:0 | Size |
| 63:32 | Alignment |

**Errors:**

OK – The operation was successful, and the result is valid.

ERROR_OBJECT_STATE – The Address Space object has not been activated.

ERROR_ARGUMENT_SIZE – The specified size is too large.

ERROR_ARGUMENT_ALIGNMENT – The specified alignment is too large.

ERROR_IDLE – The specified Address Space object has no info area.

ERROR_NOMEM – There is insufficient free space in the info area to allocate with the requested size and alignment.

ERROR_ADDR_INVALID – The specified payload buffer is not mapped in the caller's address space.

Also see: [capability errors](#capability-errors)

### Get an Information Area Entry

Get an entry in an Address Space's information area, and copies the data into the provided buffer.

If the Entry Type owner field is zero (INVALID), the Entry Type id is used as an index into the info area entry array instead of an entry match argument.

If successful, returns the entry's payload in the provided buffer, and the size of the payload as the Entry Size output. Also returns the Entry Type.

|    **Hypercall**:       |     `addrspace_info_area_get_entry`  |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6077`                     |
|     Inputs:             |     X0: Entry Type                   |
|                         |     X1: Buffer VMAddr                |
|                         |     X2: Buffer Size                  |
|                         |     X3: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Entry Size                   |
|                         |     X2: Entry Type                   |

*Entry Type:*

See `addrspace_info_area_add_entry`

**Errors:**

OK – The operation was successful, and the result is valid.

ERROR_ADDR_NOTFOUND – The specified Entry Type (or index) was not found.

ERROR_ARGUMENT_SIZE – The specified buffer size is too small.

ERROR_IDLE – The specified Address Space object has no info area.

ERROR_ADDR_INVALID – The specified payload buffer is not mapped in the caller's address space.

Also see: [capability errors](#capability-errors)

### Address Space to DMA-capable Object Attachment

Attaches an address space to any type of object that has a virtual DMA port which it can use to independently access memory in a VM address space. For types of object that have more than one virtual DMA port (e.g. a DMA-based IPC object), an index may be specified to indicate which port should be attached. Note that VCPUs do not access the VM address spaces through a virtual DMA port when executing VM code; they use a separate attachment call, described in [section](#address-space-to-thread-attachment) above.

Object types with virtual DMA ports generally require that this function is called before they are activated, unless use of the virtual DMA port is optional.

In the current implementation, the only object type with a virtual DMA port is the GICv3 compatible Virtual Interrupt Controller. The port is only present if the underlying physical interrupt controller is an GICv4, and GICv3 LPI support is enabled for the Virtual Interrupt Controller. If the port is present, it must be attached to an address space before the Virtual Interrupt Controller is activated.

|    **Hypercall**:       |      `addrspace_attach_vdma`                |
|-------------------------|---------------------------------------------|
|     Call number:        |     `hvc 0x602f`                            |
|     Inputs:             |     X0: Address Space CapID                 |
|                         |     X1: Virtual DMA-capable Object CapID    |
|                         |     X2: Virtual DMA Port Index              |
|                         |     X3: Reserved — Must be Zero             |
|     Outputs:            |     X0: Error Result                        |

**Errors:**

OK – the operation was successful.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_ARGUMENT_INVALID – the specified object is virtual DMA capable, but the port index is outside the valid range for the object.

ERROR_CSPACE_WRONG_OBJECT_TYPE – the specified virtual device object does not have any virtual DMA ports; or the specified address space object is not an address space.

ERROR_BUSY – the specified port already has an address space attached, and the object does not support changing an existing attachment.

ERROR_OBJECT_STATE – the Address Space object has not yet been activated.

Also see: [capability errors](#capability-errors)

### Address Space to Virtual Device Attachment

Attaches an address space to any type of object that presents a virtual memory-mapped device register interfaces. For types of object that have more than one virtual device interface, an index may be specified to indicate which interface should be attached. The meaning of this index depends on the object type.

After this call succeeds, accesses by any VCPU attached to the address space that lie within the specified IPA range and fault in the IPA translation will be forwarded to the specified virtual device for emulation. The addresses, access sizes, access types, and semantics of the emulated registers depend entirely on the device implementation. Also, the behaviour of any access that does not match an emulated register depends on the device implementation, and may include either faulting as if the virtual device was not attached, or returning a constant value (typically 0 or 0xff) for reads and ignoring writes.

Note that the register interface will not function correctly if any memory extent is mapped in the specified IPA range. The hypervisor will not check for such overlapping mappings.

The specified IPA range must be large enough to contain the selected register interface, and must not be attached to any other virtual device. If the specified range is undersized, some registers may not be accessible. If the specified range is oversized, any extra space will become unavailable to other virtual devices; the behaviour of an access to this extra space is unspecified.

|    **Hypercall**:       |      `addrspace_attach_vdevice`             |
|-------------------------|---------------------------------------------|
|     Call number:        |     `hvc 0x6062`                            |
|     Inputs:             |     X0: Address Space CapID                 |
|                         |     X1: Virtual Device Object CapID         |
|                         |     X2: Virtual Device Interface Index      |
|                         |     X3: Base IPA                            |
|                         |     X4: Size                                |
|                         |     X5: Reserved — Must be Zero             |
|     Outputs:            |     X0: Error Result                        |

**Index values:**

| **Type** | **Index** | **Size** | **Description** |
|--|-|--|-----|
| vGIC | 0 | 64KiB | GIC Distributor registers |
| vGIC | 1..N | 64KiB | GIC Redistributor registers for VCPUs 0..(N-1) |
| vITS | 0 | 64KiB | GIC ITS registers |

**Errors:**

OK – the operation was successful.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_ARGUMENT_INVALID – the specified object is a virtual device, but the interface index is outside the valid range for the object.

ERROR_CSPACE_WRONG_OBJECT_TYPE – the specified virtual device object does not support memory-mapped interfaces or is not a virtual device; or the specified address space object is not an address space.

ERROR_BUSY – the specified address range already contains a virtual device.

ERROR_OBJECT_STATE – the Address Space object has not yet been activated.

Also see: [capability errors](#capability-errors)

### Address Space Lookup

Lookup a memextent mapping in an address space. If successful, returns the offset and size within the memextent, as well as the attributes of the mapping.

|    **Hypercall**:       |      `addrspace_lookup`              |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x605a`                     |
|     Inputs:             |     X0: Address Space CapID          |
|                         |     X1: Memory Extent CapID          |
|                         |     X2: Base VMAddr                  |
|                         |     X3: Size                         |
|                         |     X4: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Offset                       |
|                         |     X2: Size                         |
|                         |     X3: Map Attributes               |

**Types:**

*Map Attributes:*

See: [Address Space Map](#address-space-map)

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – one of the given arguments is invalid. This could be due to an invalid Address Space.

ERROR_ARGUMENT_SIZE – the specified size is invalid.

ERROR_ARGUMENT_ALIGNMENT – the specified base address or size is not page size aligned.

ERROR_ADDR_OVERFLOW – the specified base address may cause an overflow.

ERROR_ADDR_INVALID – the specified base address is not mapped in the Address Space.

ERROR_MEMDB_NOT_OWNER – the memory mapped in the Address Space is not owned by the specified Memory Extent.

Also see: [capability errors](#capability-errors)

### Address Space Range Configuration

This hypercall configures modified behaviours for specific address ranges in the address space.

There are currently two modified behaviours defined: virtual MMIO regions, and dynamically paged regions.

A virtual MMIO device region is a region of the address space in which translation faults may be handled by an unprivileged VMM residing in another VM, by emulating the faulting access or mapping a non-executable page to the faulting region.
A fault in such a region will report the faulting address, access size, and written register contents to the unprivileged VMM, and block the VCPU until the access is handled.
Once the VMM acknowledges the fault, the VCPU state will be updated with the read register contents provided by the VMM, and the faulting instruction will be skipped.
This allows the unprivileged VMM to emulate memory-mapped I/O devices.
Note that other types of fault, such as permission or alignment faults, cannot be handled by this mechanism.
Also, depending on the architecture, this mechanism may only support translation faults generated by specific types of instruction.
On AArch64, it is limited to single-register load & store instructions without base register writeback, which are decoded by the CPU into the `ESR_EL2` syndrome bits.

A dynamically paged region is a region of the address space in which translation or permission faults may be handled by an unprivileged VMM residing in another VM, by mapping a page to the faulting region.
The faulting page will be reported to the unprivileged VMM, along with an indication of whether the failed access was a read or a write.
The VCPU will be blocked until the VMM has acknowledged the fault; once unblocked, the faulting instruction will be retried.
This allows the unprivileged VMM to allocate VM memory and map it prior to unblocking the VCPU, without emulating the accesses.
Unlike virtual MMIO, the access size, offset into the page, and register contents will not be communicated to the VMM.
Also, this mechanism is guaranteed to work for all memory access instructions.
Note that other types of fault, such as alignment faults, cannot be handled by this mechanism.

This call may be made before or after activation of the address space object.
This is to permit delegation of the right to call this API to the VM that runs in the address space, so it can explicitly acknowledge that the specified region should not be used for sensitive data.

An address range that is added must not overlap any existing range, and must not wrap around the end of the address space.
There are no other restrictions on the size or alignment of ranges added to the address space.
However, a limit may be imposed on the total number of ranges added to an address space.

A removed address range must exactly match a single previously added address range. Note that removal of a range will prevent the VMM receiving any new faults that occur in that range after the removal operation completes, but does not guarantee that the VMM has finished handling all faults in the removed range.

|    **Hypercall**:       |      `addrspace_configure_range`   |
|-------------------------|------------------------------------|
|     Call number:        |     `hvc 0x6060`                   |
|     Inputs:             |     X0: Address Space CapID        |
|                         |     X1: Base VMAddr                |
|                         |     X2: Size                       |
|                         |     X3: RangeConfigureOperation    |
|                         |     X4: Reserved   – Must be Zero  |
|     Outputs:            |     X0: Error Result               |

**Types:**

*RangeConfigureOperation:*

| Operation Enumerator | Integer Value |
|-----------------------------------------|---|
| VMMIO_CONFIGURE_OP_ADD_VMMIO_RANGE        | 0 |
| VMMIO_CONFIGURE_OP_REMOVE_VMMIO_RANGE     | 1 |
| VMMIO_CONFIGURE_OP_ADD_PRIVATE_RANGE      | 2 |
| VMMIO_CONFIGURE_OP_REMOVE_PRIVATE_RANGE   | 3 |

**Errors:**

OK – the operation was successful.

ERROR_ADDR_OVERFLOW – the specified range wraps around the end of the address space.

ERROR_ADDR_INVALID – the specified range is not completely within the input address range of the address space.

ERROR_BUSY — the specified range to be added is the same as or completely within an existing range of the same type.

ERROR_IDLE — the specified range to be removed is removed is already completely empty.

ERROR_ARGUMENT_INVALID – the specified range to be added partially overlaps a previously added range, or completely overlaps a previously added range of a different type; or the specified range to be removed is not empty, but does not exactly match a previously added range.

ERROR_NORESOURCES – the number of nominated ranges has reached an implementation-defined limit, or the hypervisor was unable to allocate memory for bookkeeping.

ERROR_UNIMPLEMENTED — unprivileged VMMs are unable to handle faults in this configuration, or an unknown operation was requested.

Also see: [capability errors](#capability-errors)

## Memory Extent Management

### Memory Extent Modify

Perform a modification on a memory extent, or synchronise previous operations on the memory extent.

For range operations, only the range of the memory extent specified by Offset and Size will be modified. For all other operations these arguments are ignored.

For operations that affect address space mappings, the hypervisor will automatically synchronise with other cores to ensure they have observed any successful changes in mappings. This behaviour is skipped if the NoSync flag is set. For other operations the NoSync flag must be set as specified below.

The Sanitise on Reset operation affects all contents of the memory extent, subject to any future donation to or from the extent. It is also inherited by any child extent derived from it. On platforms that sanitise fixed memory ranges at reset or do not sanitise any memory at reset, this operation will fail if the extent contains any memory that is not within a sanitised range, and will prohibit any future donation of unsanitised memory to the extent.

The Sync All operation is used to synchronise the effects of operations that were previously executed with the NoSync flag set. This includes the return of memory from a deleted child extent, which occurs as a delayed side-effect of deleting the child extent's last capability rather than an explicit Memory Extent hypercall, and always acts as if the NoSync flag was set.

|    **Hypercall**:       |      `memextent_modify`              |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6030`                     |
|     Inputs:             |     X0: Memory Extent CapID          |
|                         |     X1: Memextent Modify Flags       |
|                         |     X2: Offset                       |
|                         |     X3: Size                         |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*MemExtent Modify Flags:*

|     Bit Numbers     |      Mask         |     Description                 |
|---------------------|-------------------|---------------------------------|
|     7:0             |     `0xFF`        |     Memextent Modify Operation  |
|     31              |     `0x80000000`  |     NoSync                      |
|     30:8            |     `0x7FFFFF00`  |     Reserved, Must be Zero      |

*MemExtent Modify Operation:*

|   Modify Operation                        |   Integer Value   |   Description                                                                                          |
|-------------------------------------------|-------------------|--------------------------------------------------------------------------------------------------------|
|   MEMEXTENT_MODIFY_OP_UNMAP_ALL           |   0               |   Unmap the memory extent from all address spaces it was mapped into.                                  |
|   MEMEXTENT_MODIFY_OP_ZERO_RANGE          |   1               |   Zero the owned memory of an extent within the specified range. The NoSync flag must be set.          |
|   MEMEXTENT_MODIFY_OP_CACHE_CLEAN_RANGE   |   2               |   Cache clean the owned memory of an extent within the specified range. The NoSync flag must be set.   |
|   MEMEXTENT_MODIFY_OP_CACHE_FLUSH_RANGE   |   3               |   Cache flush the owned memory of an extent within the specified range. The NoSync flag must be set.   |
|   MEMEXTENT_MODIFY_OP_SANITISE_ON_RESET   |   4               |   Request sanitisation of the extent by the device firmware after warm reset. The NoSync flag must be set.  |
|   MEMEXTENT_MODIFY_OP_SYNC_ALL            |   255             |   Synchronise all previous memory extent operations. The NoSync flag must not be set.                  |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – the specified modify flags are invalid.

Also see: [Capability Errors](#capability-errors)

### Configure a Memory Extent

Configure a memory extent whose state is OBJECT_STATE_INIT.

|    **Hypercall**:       |      `memextent_configure`           |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6031`                     |
|     Inputs:             |     X0: Memory Extent CapID          |
|                         |     X1: Phys Base                    |
|                         |     X2: Size                         |
|                         |     X3: MemExtent Attributes         |
|                         |     X4: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*MemExtent Attributes:*

| Bits | Mask | Description |
|-|---|-----|
|     2..0             |     `0x7`         |     Access Rights               |
|     9:8              |     `0x300`       |     MemExtent MemType           |
|     17:16            |     `0x30000`     |     MemExtent Type              |
|     31:18,15:10,7:3  |     `0xFFFCFCF8`  |     Reserved,   Must be Zero    |

*Memextent Type*

|    Memextent Type     |   Integer Value     |   Description                                        |
|-----------------------|---------------------|------------------------------------------------------|
|     BASIC             |    0                |    Extent with basic functionality.                  |
|     SPARSE            |    1                |    Extent supporting donation and partial mappings.  |

*Memextent MemType*

|    Memextent MemType    |   Integer Value     |   Description                                      |
|-------------------------|---------------------|----------------------------------------------------|
|     ANY                 |    0                |    Allow mappings of any memory type.              |
|     DEVICE              |    1                |    Restrict mappings to device memory types only.  |
|     UNCACHED            |    2                |    Force mappings to be uncached.                  |
|     CACHED              |    3                |    Force mappings to be writeback cacheable.       |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid size or base address.

Also see: [Capability Errors](#capability-errors)

### Configure a Derived Memory Extent

Configure a derived memory extent whose state is OBJECT_STATE_INIT. The extent will be derived from the specified parent and its base address will be the base address of the parent plus the indicated offset.

|    **Hypercall**:       |      `memextent_configure_derive`     |
|-------------------------|---------------------------------------|
|     Call number:        |     `hvc 0x6032`                      |
|     Inputs:             |     X0: Memory Extent CapID           |
|                         |     X1: Parent Memory Extent CapID    |
|                         |     X2: Offset                        |
|                         |     X3: Size                          |
|                         |     X4: MemExtent Attributes          |
|                         |     X5: Reserved — Must be Zero       |
|     Outputs:            |     X0: Error Result                  |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid size or offset.

Also see: [Capability Errors](#capability-errors)

### Memory Extent Donate

Donate memory from one extent to another. This includes donations from parent to child, child to parent and between siblings.

For non-derived memory extents, the parent is considered to be the partition that was used to create the extent. Donation is only supported for sparse memory extents.

#### Protected Donation

The `TO_PROTECTED` and `FROM_PROTECTED` donation types will donate between Memory Extents, with some additional restrictions. These restrictions are intended to allow a host VM to dynamically assign or remove memory from a guest VM, without allowing the host VM to directly access the guest VM's data.

These two operations require distinct and asymmetric capability rights instead of the usual Memory Extent Donate right:

* The capability for the host VM's memory extent, which is the source for `TO_PROTECTED` and the destination for `FROM_PROTECTED`, must have the Memory Extent Protected Host right.
* The capability for the guest VM's memory extent, which is the source for `FROM_PROTECTED` and the destination for `TO_PROTECTED`, must have the Memory Extent Protected Guest right.
* The host VM's memory extent must be the parent of the guest VM's memory extent.

The `FROM_PROTECTED` donation type requires that the specified memory range is not mapped in any address space; it will not implicitly unmap the memory.
Typically the host VM will only be given the Map Protected right for the guest VM's memory extent, not the unrestricted Map right, which limits its ability to explicitly unmap the memory.
In combination, these restrictions mean the host VM can only unmap protected guest VM memory after the guest VM has explicitly released it by calling `addrspace_modify_pages`.

#### Synchronisation

If successful, the hypervisor will automatically synchronise with other cores to ensure they have completed any successful accesses to pages that were implicitly unmapped by this donate operation. Any subsequent accesses to the unmapped pages will fault. Note that in some cases, due to architectural limitations, concurrent accesses to pages mapped by the donate operation or pages adjacent to those mapped or unmapped by the donate operation may fault. If possible, the hypervisor will detect and retry such transiently faulting accesses.

Note that any page that is mapped by both the source extent and the destination extent may briefly be accessible via both mappings concurrently. If the caller needs to prevent this, it must use an explicit `addrspace_unmap` call to remove the source extent's mapping prior to the donation.

The implicit synchronisation is skipped if the NoSync flag is set. In this case, the caller can synchronise a batch of map and unmap operations either by making a `memextent_modify` call, or by making the final map or unmap call in the batch with the NoSync flag clear.

|    **Hypercall**:       |      `memextent_donate`               |
|-------------------------|---------------------------------------|
|     Call number:        |     `hvc 0x6033`                      |
|     Inputs:             |     X0: Memextent Donate Options      |
|                         |     X1: From CapID                    |
|                         |     X2: To CapID                      |
|                         |     X3: Offset                        |
|                         |     X4: Size                          |
|                         |     X5: Reserved — Must be Zero       |
|     Outputs:            |     X0: Error Result                  |

**Types:**

*Memextent Donate Options*

| Bits | Mask | Description |
|-|---|-----|
|     7:0             |     `0xFF`          |     Memextent Donate Type               |
|     31              |     `0x80000000`    |     NoSync                              |
|     30:8            |     `0x7FFFFF00`    |     Reserved — Must be Zero             |

*Memextent Donate Type*

| Memextent Donate Type | Integer Value | Description |
|---|-|-----|
| TO_CHILD       | 0 | Donate to a child extent from its parent.       |
| TO_PARENT      | 1 | Donate from a child extent to its parent.       |
| TO_SIBLING     | 2 | Donate from one sibling extent to another.      |
| TO_PROTECTED   | 3 | Protected donate from a host VM to a guest VM.  |
| FROM_PROTECTED | 4 | Protected reclaim from a guest VM by a host VM. |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid donate option, offset or size.

ERROR_ARGUMENT_SIZE – the Size provided is zero, or leads to an overflow.

ERROR_MEMDB_NOT_OWNER – the donating memory extent did not have ownership of the specified memory range.

Also see: [Capability Errors](#capability-errors)

## VCPU Management

### Configure a VCPU Thread

Configure a VCPU Thread whose state is OBJECT_STATE_INIT.

|    **Hypercall**:       |      `vcpu_configure`                |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6034`                     |
|     Inputs:             |     X0:   vCPU CapID                 |
|                         |     X1: vCPUOptionFlags              |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*vCPUOptionFlags:*

| Bits | Mask | Description |
|-|---|-----|
|     0                |     `0x1`                  |     AArch64 Self-hosted Debug Enable    |
|     1                |     `0x2`                  |     VCPU containing HLOS VM             |
|     63:2             |     `0xFFFFFFFF.FFFFFFFC`  |     Reserved,   Must be Zero            |

AArch64 Self-hosted Debug: give the VCPU access to use AArch64 Self-hosted debug functionality and registers.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_OBJECT_STATE – if the VCPU object is not in OBJECT_STATE_INIT state.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid VCPU or option flag.

Also see: [Capability Errors](#capability-errors)

### Set or Change the Physical CPU Affinity of a VCPU Thread

Set the physical CPU that will schedule the specified VCPU thread.

This may be called for any VCPU thread object whose state is OBJECT_STATE_INIT. If the scheduler implementation supports migration of active threads, it may also be called for a VCPU thread object whose state is OBJECT_STATE_ACTIVE.

If the scheduler supports directed yields and/or automatic migration of threads, calling this function on a VCPU thread object prior to activation is optional. Otherwise, it is mandatory, and the object_activate call will fail with an ERROR_OBJECT_CONFIG result if it has not been called.

If the call targets a VCPU that is currently running on a different physical CPU to the one making the call, the affinity change is asynchronous; that is, the VCPU may still be running on the same physical CPU when it returns. The hypervisor will signal the affected physical CPU to stop execution of the VCPU as soon as possible, but makes no guarantee that this will happen within any specific time period.

|    **Hypercall**:       |      `vcpu_set_affinity`           |
|-------------------------|------------------------------------|
|     Call number:        |     `hvc 0x603d`                   |
|     Inputs:             |     X0:   vCPU CapID               |
|                         |     X1: AffinityValue              |
|                         |     X2: AffinityType               |
|     Outputs:            |     X0: Error Result               |

**Types:**

*VCPU Affinity Type*

|    VCPU Affinity Type     |   Integer Value     |   Description                                         |
|---------------------------|---------------------|-------------------------------------------------------|
|     CPU_INDEX             |    -1               |    Affinity value is of type CPUIndex.                |
|     PLATFORM_CPU_INDEX    |    0                |    Affinity value is of type PlatformCPUIndex.        |

*CPUIndex* — an unsigned 16-bit number identifying the target physical CPU.

For hardware platforms with physical CPUs that are linearly numbered from 0, this is equal to the physical CPU number; for AArch64 platforms, this is the case if three of the four affinity fields in `MPIDR_EL1` have a zero value on every physical PE, and the CPUIndex corresponds to the value of the remaining `MPIDR_EL1` affinity field. Otherwise, the hypervisor’s platform driver defines the mapping between CPUIndex values and physical CPUs, and VMs may be informed of this mapping at boot time via the boot environment data.

The value 0xFFFF (`CPU_INDEX_INVALID`) may be used to indicate that the VCPU should not have affinity to any physical CPU. If the scheduler does not support automatic migration of threads, this will effectively disable the VCPU, so an additional object right (Thread Disable) is required in this case.

*PlatformCPUIndex* — a number identifying the target physical CPU using a platform-specific encoding.

For AArch64 platforms, the physical CPU is specified using the ARM multi-core `MPIDR_EL1` encoding, which embeds CPU topology information into the number.

For other platforms, the use of PlatformCPUIndex is currently reserved.

**Errors:**

OK – the operation was successful.

ERROR_OBJECT_STATE – the specified VCPU thread is active and the scheduler does not support migration of active threads.

ERROR_ARGUMENT_INVALID – the affinity value specified is out of range.

ERROR_DENIED – the specified VCPU is not permitted to change affinity because a physical-CPU-local resource, such as a private interrupt, has been assigned to it.

Also see: [Capability Errors](#capability-errors)

### Write to the Register Context of a VCPU Thread

Write a specified value to one of a VCPU's registers.

This may be called for any VCPU thread object that is currently in a virtual power-off state.
This includes VCPU objects that have not yet been activated.
Note that powering on a VCPU using a platform-specific power control API, such as `PSCI_CPU_ON`, might overwrite values set by this call.

The register to write is identified by an architecture-specific enumeration identifying the set or group of registers, and an index into that set or group.
The primary purpose of this hypercall is to set the initial state of a VCPU before it is powered on.
Therefore, the architecture will typically only define access to the general-purpose registers, excluding extended register sets such as system control registers and floating-point or vector registers.

|    **Hypercall**:       |      `vcpu_register_write`           |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6064`                     |
|     Inputs:             |     X0: vCPU CapID                   |
|                         |     X1: RegisterSet                  |
|                         |     X2: Index                        |
|                         |     X3: Value                        |
|                         |     X4: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*RegisterSet (AArch64)*

| **RegisterSet** | **Name** | **Indices** | **Description** |
|-|---|-|-----|
| 0 | `VCPU_REGISTER_SET_X` | 0–31 | 64-bit general purpose registers X0-X30 |
| 1 | `VCPU_REGISTER_SET_PC` | 0 | Program counter (4-byte aligned) |
| 2 | `VCPU_REGISTER_SET_SP_EL` | 0–1 | Stack pointers for EL0 and EL1 |

### Power on a VCPU Thread

Bring a VCPU Thread out of its initial virtual power-off state.

This call can also set the minimal initial execution state of the VCPU, including its entry point and a context pointer, avoiding the need to call `vcpu_register_write`.
The hypervisor does not dereference, check, or otherwise define any particular meaning for the context pointer.
It will be written to the first argument register in the VCPU's standard calling convention; for an AArch64 VCPU, this is X0.

The entry point and context pointer each have a corresponding flag in the flags argument which will cause this call to discard the provided value and preserve the current state of the respective VCPU register.

|    **Hypercall**:       |      `vcpu_poweron`                  |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6038`                     |
|     Inputs:             |     X0:   vCPU CapID                 |
|                         |     X1: EntryPointAddr VMPhysAddr    |
|                         |     X2: ContextPtr Register          |
|                         |     X3: vCPUPowerOnFlags             |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*vCPUPowerOnFlags:*

| Bits | Mask | Description |
|-|---|-----|
|     0                |     `0x1`                  |     Preserve entry point        |
|     1                |     `0x2`                  |     Preserve context            |
|     63:2             |     `0xFFFFFFFF.FFFFFFFC`  |     Reserved — Must be Zero     |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid VCPU.

ERROR_BUSY – the specified VCPU is currently busy and cannot be powered on at the moment.

Also see: [Capability Errors](#capability-errors)

### Power off a VCPU Thread

Halt execution of the calling VCPU, and apply architecture-defined reset values to its register context.
The effect of the reset is architecture-specific, but will typically disable the first stage of address translation, and may also disable caches, mask interrupts, etc.

This call will not return when successful.

The specified VCPU capability must refer to the calling VCPU. Specifying any other VCPU is invalid.

The last-VCPU bit in the flags argument must be set if, and only if, the caller is either the sole powered-on VCPU attached to a Virtual PM Group, or not attached to a Virtual PM Group at all. If this flag is not set correctly, the call may return ERROR_DENIED. This requirement prevents a VM inadvertently powering off all of its VCPUs, which is a state it cannot recover from without outside assistance.

|    **Hypercall**:       |      `vcpu_poweroff`                 |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6039`                     |
|     Inputs:             |     X0: vCPU CapID                   |
|                         |     X1: vCPUPowerOffFlags            |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*vCPUPowerOffFlags:*

| Bits | Mask | Description |
|-|---|-----|
|     0                |     `0x1`                  |     Last VCPU to power off in VM |
|     63:1             |     `0xFFFFFFFF.FFFFFFFE`  |     Reserved — Must be Zero      |

**Errors:**

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an unrecognised flag value, or specifying a VCPU that is not the caller.

ERROR_DENIED — the caller is the sole powered-on VCPU in a Virtual PM Group, and the last-VCPU flag was not set; or the caller is not the sole powered-on VCPU in a Virtual PM Group, and the last-VCPU flag was set.

Also see: [Capability Errors](#capability-errors)

### Set Priority of a VCPU Thread

Set a VCPU thread’s priority (if supported by the scheduler).

This may be called for any VCPU thread object whose state is OBJECT_STATE_INIT.

For the fixed priority round-robin scheduler, priorities range from 0 (lowest) to 63 (highest). If no priority is explicitly set, VCPU threads will default to a priority of 32.

|    **Hypercall**:       |      `vcpu_set_priority`   |
|-------------------------|----------------------------|
|     Call number:        |     `hvc 0x6046`           |
|     Inputs:             |     X0: VCPU CapID         |
|                         |     X1: Priority           |
|     Outputs:            |     X0: Error Result       |

**Errors:**

OK – The operation was successful.

ERROR_OBJECT_STATE – the specified VCPU thread is not in the init state.

ERROR_ARGUMENT_INVALID – the priority value specified is out of range.

Also see: [Capability Errors](#capability-errors)

### Set Timeslice of a VCPU Thread

Set a VCPU thread’s timeslice (if supported by the scheduler). Timeslices are specified in nanoseconds.

This may be called for any VCPU thread object whose state is OBJECT_STATE_INIT.

For the fixed priority round-robin scheduler, timeslices can range from 1ms to 100ms. If no timeslice is explicitly set, VCPU threads will default to a timeslice of 5ms.

|    **Hypercall**:       |      `vcpu_set_timeslice`   |
|-------------------------|-----------------------------|
|     Call number:        |     `hvc 0x6047`            |
|     Inputs:             |     X0: VCPU CapID          |
|                         |     X1: Timeslice           |
|     Outputs:            |     X0: Error Result        |

**Errors:**

OK – The operation was successful.

ERROR_OBJECT_STATE – the specified VCPU thread is not in the init state.

ERROR_ARGUMENT_INVALID – the timeslice value specified is out of range.

Also see: [Capability Errors](#capability-errors)

### VCPU vIRQ Bind

Each VCPU may have one or more associated virtual interrupt sources, depending on its configuration. This API binds one of those sources to a virtual IRQ number.

If the IRQ type is set to `VCPU_RUN_WAKEUP`, binding the IRQ will automatically place the VCPU into a state in which it can only be scheduled by calling `vcpu_run`. Refer to the [documentation](#run-a-proxy-scheduled-vcpu-thread) for that hypercall for further details.

|    **Hypercall**:       |      `vcpu_bind_virq`                |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x605c`                     |
|     Inputs:             |     X0: VCPU CapID                   |
|                         |     X1: Virtual IC CapID             |
|                         |     X2: Virtual IRQ Info             |
|                         |     X3: VCPU Virtual IRQ Type        |
|                         |     X4: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Types:**

|      VCPU Virtual IRQ Type     |      Integer Value     |
|--------------------------------|------------------------|
|     VCPU_RUN_WAKEUP            |     1                  |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_VIRQ_BOUND – the specified VCPU is already bound to a VIRQ number.

ERROR_BUSY – the specified VIRQ number is already bound to a source.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Virtual IRQ Info value.

Also see: [Capability Errors](#capability-errors)

### VCPU vIRQ Unbind

Unbinds a VCPU interrupt source from a virtual IRQ number.

If the IRQ type is set to `VCPU_RUN_WAKEUP`, unbinding the IRQ will allow the VCPU to run without a `vcpu_run` call, subject to its normal scheduling parameters and state. Note that in some cases this can cause incorrect execution in the VCPU. Refer to the [documentation](#run-a-proxy-scheduled-vcpu-thread) for that hypercall for further details.

|    **Hypercall**:       |      `vcpu_unbind_virq`              |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x605d`                     |
|     Inputs:             |     X0: VCPU CapID                   |
|                         |     X1: VCPU Virtual IRQ Type        |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Types:**

|      VCPU Virtual IRQ Type     |      Integer Value     |
|--------------------------------|------------------------|
|     VCPU_RUN_WAKEUP            |     1                  |

**Errors:**

OK – the operation was successful, or the VCPU interrupt was already unbound.

Also see: [Capability Errors](#capability-errors)

### Kill a VCPU thread

Places the VCPU thread in a killed state, forcing it to exit and end execution. The VCPU can no longer be scheduled once it has exited. If the calling VCPU is targeting itself, this call will not return if successful.

|    **Hypercall**:       |      `vcpu_kill`                     |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x603a`                     |
|     Inputs:             |     X0: VCPU CapID                   |
|                         |     X1: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid VCPU.

ERROR_OBJECT_STATE – the VCPU thread was not active, or has already been killed.

Also see: [Capability Errors](#capability-errors)

### Set Local VIRQ for a VCPU

Configure a per-VCPU local virtual IRQ (PPI) such as timer or PMU interrupts. This allows VMs to use different interrupt numbers than the hardware, which is necessary on platforms that don't follow ARM's recommended PPI numbering.

This hypercall sets the virtual IRQ number for a per-VCPU local interrupt source. The VCPU Local VIRQ Type specifies which local interrupt source to configure, and the VIRQ Number specifies the virtual interrupt number that will be used to deliver that interrupt to the VCPU.

The caller must have the appropriate capability to modify the VCPU's local VIRQ configuration. This is typically restricted to the resource manager. By allowing the resource manager to configure the VIRQ numbers, VMs can use consistent interrupt numbers regardless of the underlying hardware configuration.

|    **Hypercall**:       |      `vcpu_set_local_virq`           |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6085`                     |
|     Inputs:             |     X0: VCPU CapID                   |
|                         |     X1: VCPU Local VIRQ Type         |
|                         |     X2: VIRQ Number                  |
|                         |     X3: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Types:**

|      VCPU Local VIRQ Type     |      Integer Value     |
|-------------------------------|------------------------|
|     VIRTUAL_TIMER             |     0                  |
|     PHYSICAL_TIMER            |     1                  |

**Errors:**

OK – the operation was successful.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid VCPU capability or an unsupported VCPU Local VIRQ Type.

ERROR_DENIED – the caller does not have permission to configure VCPU local VIRQs.

Also see: [Capability Errors](#capability-errors)

### Run a Proxy-Scheduled VCPU thread

Donates CPU time to a VCPU that is configured for proxy scheduling. This is an optional mechanism that gives a privileged VM's scheduler limited control over the scheduling of another VM's VCPUs.

This call may only be used on a VCPU that has a VIRQ bound to its `VCPU_RUN_WAKEUP` interrupt source. A VCPU that is in that state cannot be scheduled normally by the hypervisor scheduler; it will only execute when this hypercall is used to give it CPU time.

If all arguments are valid, this hypercall will attempt to context-switch to the specified VCPU. It returns when the caller is preempted or when the specified VCPU is unable to continue running. The VCPU state result indicates the reason that it was unable to continue.

Some states may return additional state-specific data to allow the caller to take appropriate actions, and/or require additional data to resume execution which must be passed to the next `vcpu_run` call for the same VCPU. Also, some states may persist for some length of time that can't be directly predicted by the caller; when the VCPU leaves one of these states, it will assert the VIRQ bound to its `VCPU_RUN_WAKEUP` interrupt source.

For this call to behave as intended, the specified VCPU should have lower scheduling priority than the caller. Otherwise, the return from this call may be delayed until execution of the specified VCPU is blocked or its own timeslice expires. This rule is not enforced by the implementation.

|    **Hypercall**:       |      `vcpu_run`                      |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6065`                     |
|     Inputs:             |     X0: VCPU CapID                   |
|                         |     X1: State-specific Resume Data 1 |
|                         |     X2: State-specific Resume Data 2 |
|                         |     X3: State-specific Resume Data 3 |
|                         |     X4: Reserved —Must be Zero       |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: VCPU Run State               |
|                         |     X2: State-specific Data 1        |
|                         |     X3: State-specific Data 2        |
|                         |     X4: State-specific Data 3        |

**Types**:

*VCPU Run State*:

The following table shows the expected types of the state-specific data and resume data for each state. A 0 indicates that the argument or result is currently reserved and must be zero.

| State | Name | State Data 1 | State Data 2 | State Data 3 | Resume Data 1 | Resume Data 2
|-|--|--|--|--|--|--|
| 0x0 | `READY` | 0 | 0 | 0 | 0 | 0 |
| 0x1 | `EXPECTS_WAKEUP` | VCPU Sleep Type | 0 | 0 | 0 | 0 |
| 0x2 | `POWERED_OFF` | VCPU Poweroff Type | 0 | 0 | 0 | 0 |
| 0x3 | `BLOCKED` | 0 | 0 | 0 | 0 | 0 |
| 0x4 | `ADDRSPACE_VMMIO_READ` | VMPhysAddr | Size | 0 | Register | Action |
| 0x5 | `ADDRSPACE_VMMIO_WRITE` | VMPhysAddr | Size | Register | 0 | Action |
| 0x6 | `FAULT` | 0 | 0 | 0 | 0 | 0 |
| 0x7 | `ADDRSPACE_PAGE_FAULT` | VMPhysAddr | Access Type | 0 | Action | 0 |
| 0x100 | `PSCI_SYSTEM_RESET` | PSCI Reset Type | 0 | 0 | 0 | 0 |

The Resume Data 3 argument is currently unused and must be zero for all states.

0x0 `READY`
:The caller's hypervisor timeslice ended, or the caller received an interrupt. The caller should retry after handling any pending interrupts.

0x1 `EXPECTS_WAKEUP`
:The VCPU is waiting to receive an interrupt; for example, it may have executed a WFI instruction, or made a firmware call requesting entry into a low-power state. In the latter case, the state-specific data in X2 will be a platform-specific nonzero value indicating the requested power state. For a platform that implements Arm's PSCI standard, it is in the same format as the state argument to a `PSCI_CPU_SUSPEND` call. The `VCPU_RUN_WAKEUP` VIRQ will be asserted when the VCPU leaves this state.

0x2 `POWERED_OFF`
:The VCPU has not yet been started by calling `vcpu_poweron`, or has stopped itself by calling `vcpu_poweroff`, or has been terminated due to a reset request from another VM. If PSCI is implemented, this state is also reachable via PSCI calls. The `VCPU_RUN_WAKEUP` VIRQ will be asserted when the VCPU leaves this state. The first state data word contains a VCPU Poweroff Type value (defined below).

0x3 `BLOCKED`
:The VCPU is temporarily unable to run due to a hypervisor operation. This may include a hypercall made by the VCPU that transiently blocks it, or an incomplete migration from another physical CPU. The caller should retry after yielding to the calling VM's scheduler.

0x4 `ADDRSPACE_VMMIO_READ`
:The VCPU has attempted to read an unmapped stage 2 address inside a range previously nominated as a VMMIO range by a call to `addrspace_configure_range`. The first two state data words contain the base IPA and the access size, respectively. The VCPU will be automatically resumed by the next `vcpu_run` call. The second resume data word is a resume action; if it is set to `DEFAULT` (0), the faulting instruction will be skipped when the CPU resumes, and the first resume data word for that call should be set to the value that will be returned by the read access.

0x5 `ADDRSPACE_VMMIO_WRITE`
:The VCPU has attempted to write an unmapped stage 2 address inside a range previously nominated as a VMMIO range by a call to `addrspace_configure_range`. The three state data words contain the base IPA, access size, and the value written by the access, respectively. The VCPU will be automatically resumed by the next `vcpu_run` call. The second resume data word is a resume action; if it is set to `DEFAULT` (0), the faulting instruction will be skipped when the CPU resumes.

0x6 `FAULT`
:The VCPU has encountered an unrecoverable fault.

0x7 `ADDRSPACE_PAGE_FAULT`
:The VCPU has attempted to access an unmapped stage 2 address, or performed an access that it does not have permissions for, inside a range previously nominated as a paged range by a call to `addrspace_configure_range`. The first three state data words contain the base IPA of the access rounded down to the nearest page boundary, an enumeration value indicating the type of access, and a boolean value indicating whether the access was a permission fault on a mapped page. The VCPU will be automatically resumed by the next `vcpu_run` call. The first resume data word is a resume action; if it is set to `DEFAULT` (0), the faulting instruction will be retried.

:This state is also used when the VCPU has attempted to access an unmapped stage 2 address inside a range previously nominated as a VMMIO range by a call to `addrspace_configure_range`, and the hypervisor is unable to emulate the faulting instruction. For example, this may occur for an instruction that updates its address register, performs an atomic read and write, or reads or writes multiple registers.

0x100 `PSCI_SYSTEM_RESET`
:On a platform that implements PSCI, the VCPU has made a call to `PSCI_SYSTEM_RESET` or `PSCI_SYSTEM_RESET2`. The first state data word contains a PSCI Reset Type value (defined below). For a `PSCI_SYSTEM_RESET2` call, the second state data word contains the cookie value.

*VCPU Resume Action:*

When resuming a VCPU that has been halted by a fault, one of the resume data words can specify an alternate action to take, rather than the state-specific default action.

0x0 `DEFAULT`
:The default action for the VCPU state. See above for details.

0x1 `RETRY`
:Retry the faulting instruction.

0x2 `FAULT`
:Emulate a synchronous external data abort to EL1 from the faulting instruction.

*VCPU Sleep Type:*

This is a platform-specific unsigned word indicating a low-power suspend state. The value 0 is reserved for a trapped wait-for-interrupt or halt instruction, such as the AArch64 `WFI` instruction.

If the platform implements PSCI, nonzero values are power state values as passed to `PSCI_CPU_SUSPEND`.

*VCPU Poweroff Type*:

| Value | Description |
|-|-----|
| 0 | Recoverable power-off state, e.g. `vcpu_poweroff` called. |
| 1 | Terminated; cannot run until the VM resets. |
| >1 | Reserved. |

*PSCI Reset Type:*

| Bits | Mask | Description |
|-|---|-----|
| 31:0  | `0xffffffff` | Reset type for `PSCI_SYSTEM_RESET2`; 0 for `PSCI_SYSTEM_RESET` |
| 61:32 | `0x3FFFFFFF.00000000` | Reserved — Must be Zero    |
| 62    | `0x40000000.00000000` | 1: `PSCI_SYSTEM_RESET2` SMC64 call, 0: SMC32 call |
| 63    | `0x80000000.00000000` | 1: `PSCI_SYSTEM_RESET` call, 0: `PSCI_SYSTEM_RESET2` |

*Access Type*:

| Value | Description |
|-|-----|
| 0 | Read-only access. |
| 1 | Write access, or atomic read and write access. |
| 2 | Execute access. |
| >2 | Reserved. |

**Errors:**

OK – the operation was successful.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid VCPU.

ERROR_BUSY – the specified VCPU does not have a bound `VCPU_RUN_WAKEUP` VIRQ.

ERROR_OBJECT_STATE – the VCPU thread was not active, or has already been killed.

Also see: [Capability Errors](#capability-errors)

### Check the State of a Halted VCPU

Query the state of a VCPU that has generated a halt VIRQ to determine why it halted. The state is described the same way as for `vcpu_run`, but the VCPU is not required to be proxy-scheduled.

|    **Hypercall**:       |      `vcpu_run_check`                      |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6068`                     |
|     Inputs:             |     X0: VCPU CapID                   |
|                         |     X4: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: VCPU Run State               |
|                         |     X2: State-specific Data          |
|                         |     X3: State-specific Data          |
|                         |     X4: State-specific Data          |

**Types**:

Refer to the [documentation for `vcpu_run_thread`](#run-a-proxy-scheduled-vcpu-thread).

**Errors:**

OK – the operation was successful.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid VCPU.

ERROR_BUSY — the specified VCPU is not halted.

ERROR_OBJECT_STATE – the VCPU thread was not active, or has already been killed.

Also see: [Capability Errors](#capability-errors)

## Scheduler Management

### Scheduler Yield

Informs the hypervisor scheduler that the caller is executing a low priority task or waiting for a non-wakeup event to occur, and wants to give other VCPUs a chance to run. A hint argument may be provided to suggest to the scheduler that a particular VCPU or class of VCPUs should be run instead.

|    **Hypercall**:       |      `scheduler_yield`               |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x603b`                     |
|     Inputs:             |     X0: control                      |
|                         |     X1: arg1                         |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

*Control:*

| Bits | Mask | Description |
|-|---|-----|
|     15:0             |     `0xffff`      |     hint: Yield type hint.                                                                                                             |
|     31               |     `0x80000000`  |     imp_def:   Implementation defined flag. If set, the hint value specifies a scheduler   implementation specific yield operation.    |

Generic yield hints (imp_def flag = 0):
0x0	generic yield.
0x1	yield to target thread. arg1 = cap_id
0x2	yield to lower priority. arg1 = priority level

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – an unsupported or invalid control/hint value was provided.

Also see: [Capability Errors](#capability-errors)

## Virtual PM Group Management

A Virtual PM Group is a collection of VCPUs which share a virtual power management state. This state may be accessible via a virtualised platform-specific interface; on AArch64 this is the Arm PSCI (Platform State Configuration Interface) API. Attachment to this object type is optional for VCPUs in single-processor VMs that do not participate in power management decisions.

### Configure a Virtual PM Group

Set configuration options for a Virtual PM Group whose state is `OBJECT_STATE_INIT`. Making this call is optional.

When bit 0 is set, this VPM Group is excluded from physical power-state aggregation. The hypervisor might enter a deeper physical suspend state than requested by the VM. For example when a VCPU requests a shallow CPU_SUSPEND level, or an ARM WFI was trapped, the CPU might be put into a deeper sleep state.

When bit 1 is set, this VPM Group requires an explicit call to `vpm_group_wakeup` to wake it up from a system-level suspend state. The VM will not be woken up by any interrupts, and there is no guarantee that any interrupts signalled to the VM while in the system level suspend state will be received when the VM is woken up.

|    **Hypercall**:       |      `vpm_group_configure`           |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6066`                     |
|     Inputs:             |     X0: VPMGroup CapID               |
|                         |     X1: VPMGroupOptionFlags          |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*VPMGroupOptionFlags:*

|      Bit Numbers     |      Mask                  |      Description                                  |
|----------------------|----------------------------|---------------------------------------------------|
|     0                |     `0x1`                  |     Exclude from aggregation                      |
|     1                |     `0x2`                  |     Require explicit wake-up from SYSTEM_SUSPEND  |
|     63:2             |     `0xFFFFFFFF.FFFFFFFC`  |     Reserved — Must be Zero                       |

**Errors:**

OK – the operation was successful.

ERROR_ARGUMENT_INVALID – an unsupported or invalid configuration option was specified.

Also see: [Capability Errors](#capability-errors)

#### Power State Aggregation

If the flags argument's "exclude from aggregation" bit is clear, which is the default configuration, the Virtual PM Group will collect power state votes from its attached VCPUs. These votes will be used when determining what power state the physical device should enter when one or more physical CPUs becomes idle. In general, a physical CPU will enter the shallowest available idle state permitted by the votes of its VCPUs, i.e. a state with wakeup latency no higher than the acceptable limit for each of the VCPUs.

If the "exclude from aggregation" bit is set, the platform-specific power management API calls will still be available, but their effect on the physical power state may be limited. Also, validation of the power management API calls may be relaxed; e.g. for Arm PSCI implementations, the power state argument to `PSCI_CPU_SUSPEND` will not be validated against the states supported by the physical device.

### Virtual PM Group to VCPU Attachment

Attaches a VCPU to a Virtual PM Group. The Virtual PM Group object must have been activated before this function is called. The VCPU object must not have been activated. An attachment index must be specified which must be a non-negative integer less than the maximum number of attachments supported by this Virtual PM Group object.

|    **Hypercall**:       |      `vpm_group_attach_vcpu`         |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x603c`                     |
|     Inputs:             |     X0: VPMGroup CapID               |
|                         |     X1: VCPU CapID                   |
|                         |     X2: Index                        |
|                         |     X3: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_ARGUMENT_INVALID – the specified attachment index is outside the range supported by this Virtual PM Group.

ERROR_OBJECT_STATE – The VCPU object has already been activated, or the Virtual PM Group object has not yet been activated.

Also see: [Capability Errors](#capability-errors)

### Virtual PM Group vIRQ Bind

Binds a Virtual PM Group to a virtual interrupt.

|    **Hypercall**:       |      `vpm_group_bind_virq`           |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6043`                     |
|     Inputs:             |     X0: VPMGroup CapID               |
|                         |     X1: Virtual IC CapID             |
|                         |     X2: Virtual IRQ Info             |
|                         |     X3: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_VIRQ_BOUND – the specified virtual PM group is already bound to a VIRQ number.

ERROR_BUSY – the specified VIRQ number is already bound to a source.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Virtual IRQ Info value.

Also see: [Capability Errors](#capability-errors)

### Virtual PM Group vIRQ Unbind

Unbinds a Virtual PM Group from a virtual IRQ number.

|    **Hypercall**:       |      `vpm_group_unbind_virq`         |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6044`                     |
|     Inputs:             |     X0: VPMGroup CapID               |
|                         |     X1: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, or the virtual PM group interrupt was already unbound.

Also see: [Capability Errors](#capability-errors)

### Virtual PM Group Get State

Gets the state of the Virtual PM Group.

|    **Hypercall**:       |      `vpm_group_get_state`           |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6045`                     |
|     Inputs:             |     X0: VPMGroup CapID               |
|                         |     X1: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: VPMState                     |

**Types:**

*VPMState:*

|      VPMState                                                        |      Integer Value     |
|----------------------------------------------------------------------|------------------------|
|     NO_STATE (invalid / non existant)                                |     0                  |
|     RUNNING (System is active)                                       |     1                  |
|     CPUS_SUSPENDED (System suspended after CPU_SUSPEND call)         |     2                  |
|     SYSTEM_SUSPENDED (System suspended after SYSTEM_SUSPEND call)    |     3                  |

**Errors:**

OK – the operation was successful, the result is valid.

Also see: [Capability Errors](#capability-errors)

### Virtual PM Group Bind to Power object

Bind a VPM Group to the system power object. This is required to permit a VPM group controlling entering a physical system-level suspend.

|    **Hypercall**:       |      `vpm_group_bind_power`         |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6074`                     |
|     Inputs:             |     X0: VPMGroup CapID               |
|                         |     X1: Power CapID                  |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, the result is valid.

Also see: [Capability Errors](#capability-errors)

### Virtual PM Group Wakeup VM

Wake a VM that has used a platform-specific mechanism to enter a virtual system-level suspend state. For example, if PSCI is implemented, this will wake a VM that has called `PSCI_SYSTEM_SUSPEND`.

|    **Hypercall**:       |      `vpm_group_wakeup`         |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x606f`                     |
|     Inputs:             |     X0: VPMGroup CapID               |
|                         |     X1: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, the result is valid.

Also see: [Capability Errors](#capability-errors)

### Virtual PM Group Set Managed Wakeup Threshold

Configure a system-level virtual idle state beyond which a VM's power state is expected to be handled by a separate power management VM.

A virtual idle state is one that is intended to be used by a running VM to reduce its power consumption, as opposed to a long-term deep sleep or suspend state. In the terms of the Arm PSCI specification, this is a state entered with a `CPU_SUSPEND` call, rather than `SYSTEM_SUSPEND`.

If the VPM group enters a system-level state exceeding this threshold, then:
* The VPM group IRQ will be asserted, if it is registered.
* If the VPM group has explicit wakeups enabled, it must be explicitly woken with the `vpm_group_wakeup()` hypercall.

If this threshold is not set, or is set to 0, then any enabled virtual interrupt will be able to wake the VM from any idle state even if explicit wakeups are enabled for the VPM group, assuming that the physical device is awake or can be woken by the interrupt. Also, if the VPM group IRQ is registered, it will not be asserted on idle state entry.

The argument must either be a valid system-level power state (not core- or cluster-level), or 0.


|    **Hypercall**:       |      `vpm_group_set_threshold`       |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6083`                     |
|     Inputs:             |     X0: VPMGroup CapID               |
|                         |     X1: Power State                  |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, the result is valid.

Also see: [Capability Errors](#capability-errors)

## System Suspend Management

### Power System Suspend

Request immediate entry into a physical system-level deep sleep state, such as Suspend-to-RAM or hibernate. This hypercall requires that the caller has system-suspend rights on the system power object. This power object is created at boot and provided to the root VM.

The power state of all vcpus in the system is saved, all other physical CPUs are put into power-off state, and the last remaining CPU requests entry to the system suspend state. On external wakeup, the system vcpu power states are restored, including powering on any physical CPUs that were online prior to the system suspend.

|    **Hypercall**:       |      `power_system_suspend`          |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6070`                     |
|     Inputs:             |     X0: Power CapID                  |
|                         |     X1: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, the result is valid.

ERROR_DENIED – the operation failed because the firmware rejected the suspend request.

ERROR_ARGUMENT_INVALID – the Virtual PM Group is not Controller Virtual PM Group.

ERROR_BUSY – this operation failed because other core already in System Suspend.

ERROR_FAILURE – core state polling fialed as the core power off failed.

Also see: [Capability Errors](#capability-errors)

### Power CPU Suspend

Request immediate entry into physical system-level suspend state. This hypercall requires that the caller has system-suspend rights on the system power object. This power object is created at boot and provided to the root VM.

The specified power state is a platform-specific power state descriptor that will be forwarded to the firmware. The hypervisor and firmware will both validate the requested state against the set of states implemented by the hardware, and the firmware may also validate the request based on the current states of other physical CPUs.

|    **Hypercall**:       |      `power_cpu_suspend`             |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6084`                     |
|     Inputs:             |     X0: Power CapID                  |
|                         |     X1: Power State                  |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, the result is valid.

ERROR_DENIED – The operation failed because the firmware rejected the suspend request. Typically this occurs because the request would require powering off another core which is currently active.

ERROR_ARGUMENT_INVALID – The requested power state failed validation by either the hypervisor or the firmware.

Also see: [Capability Errors](#capability-errors)

## Trace Buffer Management

### Configure trace buffer

|    **Hypercall**:       |      `trace_configure`               |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x603f`                     |
|     Inputs:             |     X0: Arg1                         |
|                         |     X1: Arg2                         |
|                         |     X2: Trace Param                  |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Ret1                         |

**Types:**

*Trace Param*:

|      Operation Enumerator               |      Integer Value     |
|-----------------------------------------|------------------------|
|     CLASS_FLAGS                         |     0                  |
|     NOTIFY_ENABLE                       |     1                  |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to invalid Trace Param or arguments.

ERROR_DENIED – the operation was denied.

#### Update trace class flags

When Trace Param is set to `CLASS_FLAGS`.

Update the trace class flags values by specifying which flags to set and clear. The internal enabled hypervisor trace flags are first masked with `clear_flags` and then ORed with `set_flags`. Some flags are internal to the hypervisor, so their masks passed in this hypercall will be ignored.

The operation returns the resulting mask of enabled trace flags in Ret1.

|      Field       |      Argument          |
|------------------|------------------------|
|   set_flags      |     Arg1               |
|   clear_flags    |     Arg2               |


#### Configure trace buffer notifications

When Trace Param is set to `NOTIFY_ENABLE`.

Enables or disables trace notifications. Trace notifications are signalled via a doorbell interrupt. The `enable` argument must be `0` or `1`.

|      Field       |      Argument          |
|------------------|------------------------|
|   enable         |     (bool)Arg1         |

## Watchdog Management

### Configure a Watchdog

Configure a Watchdog whose state is OBJECT_STATE_INIT.

|    **Hypercall**:       |      `watchdog_configure`            |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6058`                     |
|     Inputs:             |     X0:   Watchdog CapID             |
|                         |     X1: WatchdogOptionFlags          |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*WatchdogOptionFlags:*

| Bits | Mask | Description |
|-|---|-----|
|     0                |     `0x1`                  |     Critical bite               |
|     63:1             |     `0xFFFFFFFF.FFFFFFFE`  |     Reserved,   Must be Zero    |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_OBJECT_STATE – if the Watchdog object is not in OBJECT_STATE_INIT state.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to invalid option flags.

Also see: [Capability Errors](#capability-errors)

### Watchdog to vCPU Attachment

Attaches a Watchdog object to a vCPU. The Watchdog object must have been activated before this function is called. The VCPU object must not have been activated.

|    **Hypercall**:       |      `watchdog_attach_vcpu`          |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6040`                     |
|     Inputs:             |     X0: Watchdog CapID               |
|                         |     X1: vCPU CapID                   |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due a thread of kind different from VCPU or if the thread belongs to a HLOS VM.

ERROR_OBJECT_STATE – The thread object has already been activated or the watchdog has not been activated yet.

Also see: [Capability Errors](#capability-errors)

### Watchdog vIRQ Bind

Binds a Watchgdog (bark or bite) interface to a virtual IRQ number.

|    **Hypercall**:       |      `watchdog_bind_virq`          |
|-------------------------|------------------------------------|
|     Call number:        |     `hvc 0x6041`                   |
|     Inputs:             |     X0: Watchdog CapID             |
|                         |     X1: Virtual IC CapID           |
|                         |     X2: Virtual IRQ Info           |
|                         |     X3: WatchdogBindOptionFlags    |
|     Outputs:            |     X0: Error Result               |

**Types:**

*WatchdogBindOptionFlags:*

| Bits | Mask | Description |
|-|---|-----|
|     0                |     `0x1`                  |     Bite virq (If unset, bark virq)    |
|     63:1             |     `0xFFFFFFFF.FFFFFFFE`  |     Reserved,   Must be Zero           |

Bite virq: If set to 0x1, this flag indicates that it binds the bite virq, otherwise it binds the bark virq.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_VIRQ_BOUND – the specified watchdog is already bound to a VIRQ number.

ERROR_BUSY – the specified VIRQ number is already bound to a source.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Virtual IRQ Info value, or invalid Watchdog object.

Also see: [Capability Errors](#capability-errors)

### Watchdog vIRQ Unbind

Unbinds a Watchdog (bark or bite) interface virtual IRQ number.

|    **Hypercall**:       |      `watchdog_unbind_virq`        |
|-------------------------|------------------------------------|
|     Call number:        |     `hvc 0x6042`                   |
|     Inputs:             |     X0: Watchdog CapID             |
|                         |     X1: WatchdogBindOptionFlags    |
|     Outputs:            |     X0: Error Result               |

**Types:**

*WatchdogBindOptionFlags:*

| Bits | Mask | Description |
|-|---|-----|
|     0                |     `0x1`                  |     Bite virq (If unset, bark virq)    |
|     63:1             |     `0xFFFFFFFF.FFFFFFFE`  |     Reserved,   Must be Zero           |

Bite virq: If set to 0x1, this flag indicates that it unbinds the bite virq, otherwise it unbinds the bark virq.

**Errors:**

OK – the operation was successful, or the watchdog interrupt was already unbound.

Also see: [Capability Errors](#capability-errors)

### Watchdog Runtime Management

Performs miscellaneous management operations on an arbitrary watchdog object (not necessarily the calling VM's watchdog). Currently, three operations are defined:

1. Freeze a watchdog's counter, preventing a bark or bite occurring (if no such event has already occurred).
1. Freeze a watchdog's counter as above, and also reset the counter to 0.
1. Unfreeze a watchdog's counter.

This is intended primarily for use by the manager of a proxy-scheduled VM, to prevent watchdog events occurring in the VM if the proxy threads cannot be scheduled.

Note that freeze and unfreeze operations are counted, and the watchdog counter will only progress while the the freeze count is zero (i.e. freeze and unfreeze operations are balanced). Also, freeze and unfreeze operations may be performed automatically by the hypervisor in some cases.

|    **Hypercall**:       |      `watchdog_manage`             |
|-------------------------|------------------------------------|
|     Call number:        |     `hvc 0x6063`                   |
|     Inputs:             |     X0: Watchdog CapID             |
|                         |     X1: WatchdogManageOperation    |
|     Outputs:            |     X0: Error Result               |

**Types:**

*WatchdogManageOperation*:

|      Operation Enumerator               |      Integer Value     |
|-----------------------------------------|------------------------|
|     WATCHDOG_MANAGE_OP_FREEZE           |     0                  |
|     WATCHDOG_MANAGE_OP_FREEZE_AND_RESET |     1                  |
|     WATCHDOG_MANAGE_OP_UNFREEZE         |     2                  |

**Errors:**

OK – the operation was successful, or the watchdog interrupt was already unbound.

ERROR_BUSY – the operation failed because it would otherwise have overflowed or underflowed the watchdog's freeze count.

Also see: [Capability Errors](#capability-errors)


## Virtual IO Management

### Configure a Virtual IO Backend Object

Configure a Virtual IO Backend Object whose state is OBJECT_STATE_INIT.

If the type valid flag is set in the flags argument, then the specified device and transport types must be known to the hypervisor, and any appropriate type-specific hypercalls must be made before the device is permitted to exit its reset state. If the flag is not set, the transport type is presumed to be MMIO, and the device type is presumed to be 0 (i.e. invalid, to be set later by a configuration space write).

If the transport type is MMIO, a Memory Extent object must be provided, which must be 4KiB in size. It is presumed to have been mapped with write permissions in the backend VM's address space and with read-only permissions in the frontend VM's address space. Its layout matches the register layout specified for MMIO devices in section 4.2.2 of the Virtual I/O Device (VIRTIO) 1.1 specification, followed by optional device-specific configuration starting at offset 0x100. The caller must also bind the MMIO frontend VIRQ source to the frontend VM's interrupt controller.

If the transport type is not MMIO, the Memory Extent is optional. It contains only the device-specific configuration registers, if the protocol defines any. If provided, it is presumed to have been mapped with write permissions in the backend VM's address space, but not mapped in the frontend VM.

If the transport type is PCI, the backend must be attached to the frontend VM's Virtual PCI Bus, which will take care of mapping and IRQ routing for the frontend VM.

The maximum number of queues presented by the device must be specified at configuration time, because it may determine the size of memory allocations made in the backend implementation.

If the device configuration size valid flag is not set in the flags argument, then the size argument is ignored, and the configuration area size is calculated from the size of the specified Memory Extent. If the size argument is valid, it must not cause the configuration to extend past the end of the Memory Extent. Note that this size is explicitly reported to the frontend VM when the PCI interface is active, but other transports may not report it.

If the ignore configuration writes flag is set in the flags argument, all writes to the device configuration registers will be ignored. Depending on the transport type, failures of the writes may or may not be directly reported to the frontend VM. If the ignore configuration writes flag is not set, and the device type valid flag is set, then the device type must be one that has specific backend configuration APIs supported by the hypervisor.

If the synchronous reset flag is set in the flags argument, a reset requested by the frontend will not complete until the backend has acknowledged it. Otherwise, the reset will complete immediately and the backend will be asynchronously notified that it has happened. Note that the MMIO transport's specification does not require the frontend to wait for reset completion, so setting this flag may be ineffective for the MMIO transport unless the frontend driver has a non-standard patch to poll the status after resetting.

If the per-queue interrupts flag is set in the flags argument, the frontend may provide a separate buffer-used notification for each queue, if possible; for example, if the frontend is using the PCI transport, an MSI-X capability will be presented. If the per-queue interrupts flag is not set, calls to the `virtio_backend_notify` hypercall are permitted to clear the per-queue flag in that call's flags argument, for backwards compatibility with the legacy `virtio_mmio_backend_assert_virq` hypercall. If the maximum number of queues is set to one, both per-queue frontend notifications and legacy backend hypercalls will be enabled regardless of the value of the per-queue interrupts flag.

The newly created interface will initially have the `DEVICE_NEEDS_RESET` status flag set, to prevent the device being probed by the frontend before the backend has initialised it.

|    **Hypercall**:       |      `virtio_backend_configure`      |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6049`                     |
|     Inputs:             |     X0: Virtio CapID                 |
|                         |     X1: Memextent CapID              |
|                         |     X2: VQsNum Integer               |
|                         |     X3: VirtioOptionFlags            |
|                         |     X4: VirtioInterfaceType          |
|                         |     X5: DeviceConfigSize Integer     |
|     Outputs:            |     X0: Error Result                 |

This hypercall was formerly named `virtio_mmio_configure`.

**Types:**

*VirtioOptionFlags:*

| Bit Numbers |  Mask                 | Description                             |
|-------------|-----------------------|-----------------------------------------|
| 0           | `0x1`                 | Synchronous reset completion |
| 1           | `0x2`                 | Enable per-queue interrupts to the frontend |
| 2           | `0x4`                 | Ignore writes to the device config area |
| 3           | `0x8`                 | Device config area size is valid |
| 6           | `0x40`                | Device type argument is valid |
| 63:7,5:4    | `0xFFFFFFFF.FFFFFFB0` | Reserved — Must be Zero |

*VirtioInterfaceType:*

| Bit Numbers |  Mask                 | Description                             |
|-------------|-----------------------|-----------------------------------------|
| 7:0         | `0xFF`                | Device type |
| 23:16       | `0xFF0000`            | Transport type |
| 63:24,15:8  | `0xFFFFFFFF.FF00FF00` | Reserved — Must be Zero |

*Virtio Transport Type*:

| Value | Name | Description |
| 0x0 | `VIRTIO_BACKEND_TRANSPORT_MMIO` | Simple MMIO (platform) device |
| 0x1 | `VIRTIO_BACKEND_TRANSPORT_PCI`  | PCI device |

**Errors:**

OK – the operation was successful.

ERROR_OBJECT_STATE – if the Virtual IO object is not in OBJECT_STATE_INIT state.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to VQsNum being larger than the maximum, or the specified Memory Extent object being of an unsupported type.

Also see: [Capability Errors](#capability-errors)

### Virtual IO MMIO Transport vIRQ Bind

Binds a Virtual IO MMIO Transport's interrupt source to a virtual IRQ in the frontend VM. This is only applicable when the MMIO transport is being used.

|    **Hypercall**:       |      `virtio_mmio_frontend_bind_virq`   |
|-------------------------|----------------------------------------|
|     Call number:        |     `hvc 0x604a`                       |
|     Inputs:             |     X0: Virtio CapID                   |
|                         |     X1: Virtual IC CapID               |
|                         |     X2: Virtual IRQ Info               |
|                         |     X3: Reserved — Must be Zero        |
|     Outputs:            |     X0: Error Result                   |

This hypercall was formerly named `virtio_mmio_bind_backend_virq`. Note that the new name reflects the recipient of the IRQ rather than the source of the interrupts.

**Errors:**

OK – the operation was successful.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_VIRQ_BOUND – the specified Virtual IO MMIO Transport is already bound to a frontend VIRQ number.

ERROR_BUSY – the specified VIRQ number is already bound to a source.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Virtual IRQ Info value.

Also see: [Capability Errors](#capability-errors)

### Virtual IO MMIO Transport vIRQ Unbind

Unbinds a Virtual IO MMIO Transport's interrupt source from a virtual IRQ in the frontend VM. This is only applicable when the MMIO transport is being used.

|    **Hypercall**:       |      `virtio_mmio_frontend_unbind_virq`  |
|-------------------------|------------------------------------------|
|     Call number:        |     `hvc 0x604b`                         |
|     Inputs:             |     X0: Virtio CapID                     |
|                         |     X1: Reserved   – Must be Zero        |
|     Outputs:            |     X0: Error Result                     |

This hypercall was formerly named `virtio_mmio_unbind_backend_virq`. Note that the new name reflects the recipient of the IRQ rather than the source of the interrupts.

**Errors:**

OK – the operation was successful, or the Virtual IO MMIO Transport interrupt was already unbound.

Also see: [Capability Errors](#capability-errors)

### Virtual IO Backend vIRQ Bind

Binds a Virtual IO Backend's interrupt source to a virtual IRQ in the backend VM.

|    **Hypercall**:       |      `virtio_backend_bind_virq`         |
|-------------------------|-----------------------------------------|
|     Call number:        |     `hvc 0x604c`                        |
|     Inputs:             |     X0: Virtio CapID                    |
|                         |     X1: Virtual IC CapID                |
|                         |     X2: Virtual IRQ Info                |
|                         |     X3: Reserved — Must be Zero         |
|     Outputs:            |     X0: Error Result                    |

This hypercall was formerly named `virtio_mmio_bind_frontend_virq`. Note that the new name reflects the recipient of the IRQ rather than the source of the interrupts.

**Errors:**

OK – the operation was successful.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_VIRQ_BOUND – the specified Virtual IO Backend is already bound to a VIRQ number.

ERROR_BUSY – the specified VIRQ number is already bound to a source.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Virtual IRQ Info value.

Also see: [Capability Errors](#capability-errors)

### Virtual IO Backend vIRQ Unbind

Unbinds a Virtual IO Backend's interrupt source from a virtual IRQ in the backend VM.


|    **Hypercall**:       |      `virtio_backend_unbind_virq`         |
|-------------------------|-------------------------------------------|
|     Call number:        |     `hvc 0x604d`                          |
|     Inputs:             |     X0: Virtio CapID                      |
|                         |     X1: Reserved   – Must be Zero         |
|     Outputs:            |     X0: Error Result                      |

This hypercall was formerly named `virtio_mmio_unbind_frontend_virq`. Note that the new name reflects the recipient of the IRQ rather than the source of the interrupts.

**Errors:**

OK – the operation was successful, or the Virtual IO MMIO Backend interrupt was already unbound.

Also see: [Capability Errors](#capability-errors)

### Virtual IO Backend Send Notification

The backend makes this call to signal to the frontend that a configuration change has occurred or a queue has had buffers used.

The interpretation of the NotifyStatus argument depends on whether per-queue status is enabled in the NotifyFlags argument.

If per-queue status is disabled, then NotifyStatus has the same content as the level-triggered interrupt status register presented to the frontend: bit 0 is set to indicate that the backend has consumed at least one buffer from at least one queue, and bit 1 is set to indicate that the device configuration has changed. Note that disabling per-queue status is not permitted if the object was configured with per-queue IRQs enabled and a maximum number of queues greater than one.

If per-queue status is enabled, then bits 62:0 of NotifyStatus form a bitmap indicating which queues have had at least one buffer used, and bit 63 indicates that the device configuration has changed.

If the NotifyFlags argument has the Config Update flag set, the hypervisor will start incrementing the configuration generation field in the common registers every time the frontend traps a read of that field. This flag must be set before the start of any non-atomic update to the device configuration registers; it need not be set before atomic updates, i.e. those which change only a single size-aligned 32-bit or smaller field. It is implicitly cleared by notification of a configuration change.

|    **Hypercall**:       |      `virtio_backend_notify`    |
|-------------------------|---------------------------------|
|     Call number:        |     `hvc 0x604e`                |
|     Inputs:             |     X0: Virtio CapID            |
|                         |     X1: NotifyStatus            |
|                         |     X2: NotifyFlags             |
|     Outputs:            |     X0: Error Result            |

**Types:**

*NotifyStatus:*

When per-queue status is not enabled in NotifyFlags:

| Bit Numbers | Mask                  | Description            |
|-------------|-----------------------|------------------------|
| 0           | `0x1`                 | Queue ready            |
| 1           | `0x2`                 | Config updated         |
| 63:2        | `0xFFFFFFFF.FFFFFFFC` | Reserved, Must be Zero |

When per-queue status is enabled in NotifyFlags:

| Bit Numbers | Mask                  | Description            |
|-------------|-----------------------|------------------------|
| 62:0        | `0x7FFFFFFF.FFFFFFFF` | Per-queue ready bitmap |
| 63          | `0x80000000.00000000` | Config updated         |

*NotifyFlags:*

| Bit Numbers | Mask                  | Description              |
|-------------|-----------------------|--------------------------|
| 0           | `0x1`                 | Per-queue NotifyStatus   |
| 1           | `0x2`                 | Non-atomic config update |
| 63:1        | `0xFFFFFFFF.FFFFFFFE` | Reserved, Must be Zero   |

This hypercall was formerly named `virtio_mmio_backend_assert_virq`.

**Errors:**

OK – the operation was successful.

ERROR_BUSY – Cannot assert the IRQ since there is a reset currently pending.

ERROR_DENIED – The device is multi-queue and has per-queue IRQs enabled, but the caller did not specify per-queue status.

ERROR_ARGUMENT_INVALID – A value passed in an argument was invalid.

Also see: [Capability Errors](#capability-errors)

### Virtual IO Backend Set Device Features

Set the device feature flags for the specified device feature selector. The device features specified must comply with the features enforced by the hypervisor (`VIRTIO_F_VERSION_1`, `VIRTIO_F_ACCESS_PLATFORM`, `!VIRTIO_F_NOTIFICATION_DATA`).

The device features can only be modified while a reset is pending.

|    **Hypercall**:       |      `virtio_backend_set_dev_features`   |
|-------------------------|-----------------------------------------------|
|     Call number:        |     `hvc 0x604f`                              |
|     Inputs:             |     X0: Virtio CapID                          |
|                         |     X1: DeviceFeaturesSel                     |
|                         |     X2: DeviceFeatures                        |
|                         |     X3: Reserved — Must be Zero               |
|     Outputs:            |     X0: Error Result                          |

This hypercall was formerly named `virtio_mmio_backend_set_dev_features`.

**Errors:**

OK – The operation was successful.

ERROR_ARGUMENT_INVALID – A value passed in an argument was invalid.

ERROR_BUSY – The device features cannot be modified because there is no reset pending.

ERROR_DENIED – Device features passed do not comply with the features enforced by the hypervisor.

Also see: [Capability Errors](#capability-errors)

### Virtual IO Backend Set Max Queue Size

Set maximum queue size (in buffers) of the queue specified by the queue selector. This size is presented to the frontend as a 16-bit unsigned integer, and therefore must not exceed 65535.

The maximum queue size can only be modified while a reset is pending.

|    **Hypercall**:       |      `virtio_backend_set_queue_size_max`       |
|-------------------------|------------------------------------------------|
|     Call number:        |     `hvc 0x6050`                               |
|     Inputs:             |     X0: Virtio CapID                           |
|                         |     X1: QueueSel                               |
|                         |     X2: QueueNumMax                            |
|                         |     X3: Reserved — Must be Zero                |
|     Outputs:            |     X0: Error Result                           |

This hypercall was formerly named `virtio_mmio_backend_set_queue_num_max`.

**Errors:**

OK – The operation was successful.

ERROR_BUSY – The maximum queue size cannot be modified because there is no reset pending.

ERROR_ARGUMENT_INVALID – A value passed in an argument was invalid.

Also see: [Capability Errors](#capability-errors)

### Virtual IO Backend Get Driver Features

Get the driver features flags based on the specified driver features selector.

|    **Hypercall**:       |      `virtio_backend_get_drv_features`        |
|-------------------------|-----------------------------------------------|
|     Call number:        |     `hvc 0x6051`                              |
|     Inputs:             |     X0: Virtio CapID                          |
|                         |     X1: DriverFeaturesSel                     |
|                         |     X2: Reserved — Must be Zero               |
|     Outputs:            |     X0: Error Result                          |
|                         |     X1: DriverFeatures                        |

This hypercall was formerly named `virtio_mmio_backend_get_drv_features`.

**Errors:**

OK – The operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – A value passed in an argument was invalid.

Also see: [Capability Errors](#capability-errors)

### Virtual IO Backend Get Queue Info

Fetch the current configuration of the queue specified by the queue selector.

|    **Hypercall**:       |      `virtio_backend_get_queue_info`        |
|-------------------------|---------------------------------------------|
|     Call number:        |     `hvc 0x6052`                            |
|     Inputs:             |     X0: Virtio CapID                        |
|                         |     X1: QueueSel                            |
|                         |     X2: Reserved — Must be Zero             |
|     Outputs:            |     X0: Error Result                        |
|                         |     X1: QueueSize                           |
|                         |     X2: QueueReady                          |
|                         |     X3: QueueDesc (low and high)            |
|                         |     X4: QueueDriver (low and high)          |
|                         |     X5: QueueDevice (low and high)          |

This hypercall was formerly named `virtio_mmio_backend_get_queue_info`.

**Errors:**

OK – The operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – A value passed in an argument was invalid.

Also see: [Capability Errors](#capability-errors)

### Virtual IO Backend Get Notification

The backend should make this call from its VIRQ handler, to obtain a bitmap of the virtual queues that need to be notified and a bitmap of the reasons why the VIRQ was asserted. Making this call acknowledges the returned notifications, so the VIRQ will be deasserted if it is configured as level-triggered.

|    **Hypercall**:       |      `virtio_backend_get_notification`        |
|-------------------------|-----------------------------------------------|
|     Call number:        |     `hvc 0x6053`                              |
|     Inputs:             |     X0: Virtio CapID                          |
|                         |     X1: Reserved   – Must be Zero             |
|     Outputs:            |     X0: Error Result                          |
|                         |     X1: VQs Bitmap                            |
|                         |     X2: NotifyReason Bitmap                   |

This hypercall was formerly named `virtio_mmio_backend_set_dev_features`.

**Types:**

*NotifyReason:*

| Bits | Mask | Description |
|-|---|-----|
|     0                |     `0x1`                  |     1 = `NEW_BUFFER`: notifies the device that there are new buffers to process in a queue.                       |
|     1                |     `0x2`                  |     1 = `RESET_RQST`: notifies the device that a device reset has been requested.                                 |
|     3                |     `0x8`                  |     1 = `DRIVER_OK`: notifies the device that the frontend has set the DRIVER_OK bit of the Status register.      |
|     4                |     `0x10`                 |     1 = `FAILED`: notifies the device that the frontend has set the FAILED bit of the Status register.            |
|     63:5,2           |     `0xFFFFFFFF.FFFFFFE4`  |     Reserved |

**Errors:**

OK – The operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – A value passed in an argument was invalid.

Also see: [Capability Errors](#capability-errors)

### Virtual IO Backend Acknowledge Reset

The backend should make this call after a device reset is completed. This call will reset the status field to 0 to allow the frontend driver to begin probing.

Before calling this function, the backend must initialise all interface state that might be accessed by the frontend during probe, including the device features, maximum queue length, ID registers (for MMIO interfaces), and device-specific configuration registers.

|    **Hypercall**:       |      `virtio_backend_acknowledge_reset`        |
|-------------------------|------------------------------------------------|
|     Call number:        |     `hvc 0x6054`                               |
|     Inputs:             |     X0: Virtio CapID                           |
|                         |     X1: Reserved   – Must be Zero              |
|     Outputs:            |     X0: Error Result                           |

This hypercall was formerly named `virtio_mmio_backend_acknowledge_reset`.

**Errors:**

OK – The operation was successful, and the result is valid.

ERROR_BUSY – There was no pending device reset.

ERROR_ARGUMENT_INVALID – A value passed in an argument was invalid.

Also see: [Capability Errors](#capability-errors)

### Virtual IO Backend Update Status

This calls sets bits in the status register. Once a bit has been set, it can only be cleared by acknowledging a reset request.

The only bits that are permitted to be set by this call are the `FEATURES_OK` bit (after receiving a notification that the frontend has attempted to set it), and the `DEVICE_NEEDS_RESET` bit (if the status register is currently non-zero). All other status bits are ignored.

|    **Hypercall**:       |      `virtio_backend_update_status`     |
|-------------------------|-----------------------------------------|
|     Call number:        |     `hvc 0x6055`                        |
|     Inputs:             |     X0: Virtio CapID                    |
|                         |     X1: Status                          |
|                         |     X2: Reserved — Must be Zero         |
|     Outputs:            |     X0: Error Result                    |

This hypercall was formerly named `virtio_mmio_backend_set_status`.

**Errors:**

OK – The operation was successful, and the result is valid.

ERROR_DENIED — A status bit was specified that can be set by this call, but setting it is currently not allowed.

Also see: [Capability Errors](#capability-errors)

### Configure a VirtIO IOMMU

Configure a VirtIO IOMMU object whose state is OBJECT_STATE_INIT. This hypercall configures the VirtIO IOMMU device with the associated SMMU hardware and sets the maximum number of streams that can be managed.

|    **Hypercall**:       |      `virtio_iommu_configure`         |
|-------------------------|---------------------------------------|
|     Call number:        |     `hvc 0x6081`                      |
|     Inputs:             |     X0: VirtIO IOMMU CapID            |
|                         |     X1: SMMU Handle                   |
|                         |     X2: Max Streams                   |
|                         |     X3: Reserved — Must be Zero       |
|     Outputs:            |     X0: Error Result                  |

The SMMU Handle parameter identifies the physical SMMU hardware that this VirtIO IOMMU will virtualize. This handle is used by the Resource Manager to match the VirtIO IOMMU with the appropriate physical SMMU base addresses and configuration.

The Max Streams parameter limits the maximum number of stream IDs that can be managed by this VirtIO IOMMU instance. This is used to control memory usage and prevent the VirtIO IOMMU from consuming excessive hypervisor memory. The actual limit may be further constrained by platform-specific factors such as the number of PCI buses or SoC devices.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_OBJECT_STATE – if the VirtIO IOMMU object is not in OBJECT_STATE_INIT state.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid SMMU Handle (must be non-zero) or Max Streams value.

Also see: [Capability Errors](#capability-errors)

## Virtio Input Config Hypercalls

The following hypercalls are used to define the device configuration area for a Virtual I/O Backend object that has been configured with a device type of Input. The configuration area for this device type contains banked read-only registers that require immediate updates when the write-only bank select registers are written, so it is necessary to provide the contents of each bank to the hypervisor.

### Virtio Input Configure

Allocate storage for the large data items and set the values of the small data items (`dev_ids` and `propbits`, which each fit in a single machine register). For the two types that support `subsel`, this call will specify the number of distinct valid `subsel` values (which may be sparse).

The `NumEVTypes` value must be between 0 and 32 inclusive. The `NumAbsAxes` value must be between 0 and 64 inclusive. If these limits are exceeded, the call will return `ERROR_ARGUMENT_SIZE`.



|    **Hypercall**:       |      `virtio_input_configure`        |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x605e`                      |
|     Inputs:             |     X0: Virtio CapID                 |
|                         |     X1: DevIDs                       |
|                         |     X2: PropBits                     |
|                         |     X3: NumEVTypes                   |
|                         |     X4: NumAbsAxes                   |
|                         |     X5: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Types**:

_DevIDs_:

| Bits | Mask | Description |
|-|---|-----|
| 15:0 | `0xFFFF` | BusType |
| 31:16 | `0xFFFF0000` | Vendor |
| 47:32 | `0xFFFF.00000000` | Product |
| 63:48 | `0xFFFF0000.00000000` | Version |

**Errors:**

OK – The operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – A value passed in an argument was invalid.

Also see: [Capability Errors](#capability-errors)

### Virtio Input Set Data

Copy data into the hypervisor's storage for one of the large data items, given its `sel` and `subsel` values, size, and the virtual address of a buffer in the caller's stage 1 address space. The data must not already have been configured for the given `sel` and `subsel` values.

|    **Hypercall**:       |      `virtio_input_set_data`         |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x605f`                      |
|     Inputs:             |     X0: Virtio CapID                 |
|                         |     X1: Sel                          |
|                         |     X2: SubSel                       |
|                         |     X3: Size                         |
|                         |     X4: Data VMAddr                  |
|                         |     X5: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

The specified `VMAddr` must point to a buffer of the specified size that is mapped in the caller's stage 1 and stage 2 address spaces.

The `Sel`, `SubSel` and `Size` arguments must fall within one of the following ranges:

| Sel  | SubSel | Size  |
|------|--------|-------|
| 1    | 0      | 0–128 |
| 2    | 0      | 0–128 |
| 0x11 | 0–31   | 0–128 |
| 0x12 | 0–63   | 20    |

All other combinations are invalid. The call will return `ERROR_ARGUMENT_INVALID` if `Sel` or `SubSel` is invalid or out of range, and `ERROR_ARGUMENT_SIZE` if `Size` is out of range for the specified combination of `Sel` and `SubSel`.

Also, the call must not be repeated with `Sel` set to 0x11 or 0x12 and `Size` set to a nonzero value for more distinct values of `SubSel` than were specified with the `NumEVTypes` and `NumAbsAxes` arguments, respectively, of the most recent `virtio_input_configure` call. The call will return `ERROR_NORESOURCES` if these limits are exceeded.

**Errors:**

OK – The operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – A value passed in an argument was invalid.

Also see: [Capability Errors](#capability-errors)

## Virtual PCIe Root Complex Management

The hypervisor implements a Virtual PCIe Root Complex which can host PCIe functions that are either implemented in software by the hypervisor, or passed through from a Physical PCIe Root Complex.

### Configure a Virtual PCIe Root Complex

Configure a Virtual PCIe Root Complex whose state is OBJECT_STATE_INIT.

A Virtual PCIe Root Complex requires attachments to an Address Space object and a Virtual Interrupt Controller object before it can be activated. It also requires at least two specified address ranges, with two further optional address ranges, all of which are input address ranges for the specified Address Space object.

The mandatory address ranges are for the Configuration Access Mechanism (CAM) aperture (or Enhanced Configuration Access Mechanism, ECAM, for PCIe buses), and the Non-prefetchable Memory aperture. The optional ranges are for the Prefetchable Memory aperture and the I/O aperture.

Prefetchable Memory BARs are permitted to be mapped into the Non-Prefetchable Memory aperture. This allows them to be mapped even if no Prefetchable Memory aperture is defined.

Each of the specified apertures must be kept clear of other mappings by the address space manager. If the apertures overlap with each other, with the apertures of another Virtual PCIe Root Complex, or with mappings in the address space created by other mechanisms, the behaviour of accesses to those address ranges is unpredictable. Furthermore, the VM can cause mappings in the specified Memory and I/O apertures to be created or destroyed at any time, by writing to a device's BARs; for this reason, the specified Address Space CapID is required to have the Address Space Map right.

The size of each aperture is specified as a base-two exponent; that is, as the number of low address bits required to index it. For the two optional apertures, a size of 0 means the aperture is absent. For the memory and I/O apertures, the minimum size is the address space's page size (typically 12 bits, for 4KiB pages), and the maximum size is half of the address space (i.e. one bit less than its configured address size).

For the CAM / ECAM aperture, the PCIe specification mandates that the aperture is large enough to map between 2 and 256 buses. Each bus has 32 slots, and each slot has 8 functions. Therefore, a CAM aperture with 256B per function must cover between 17 and 24 address bits (128KiB to 16MiB of IPA space), and an ECAM aperture with 4KiB per function must cover between 21 and 28 address bits (2MiB to 256MiB of IPA space). Note that PCI-PCI bridges are not implemented, so all devices will be on bus 0 and the minimum size is always sufficient.

The two memory-typed apertures will be given the same base address in the PCI Memory address space as the specified base IPA, so the guest can directly write physical addresses into the BAR configuration registers. However, if a Memory BAR is assigned an address below the 4GiB boundary, the assigned address is not within either aperture, and the base address of the Non-prefetchable Memory aperture is above 4GiB, the BAR will be treated as if the high 32 bits of its base address match those of the base address of the Non-prefetchable Memory aperture. This effectively aliases the first 4GiB of the Non-prefetchable Memory aperture in the PCI Memory address space, allowing 32-bit Memory BARs to be mapped without reserving any of the low 4GiB of the IPA space (though the same behaviour is applied to 64-bit Memory BARs for consistency).

The I/O aperture always starts at address 0 in the PCI I/O address space.

The option flags argument specifies the attributes of the bus. The only defined option flag is bit 0, which must be set to indicate that the Virtual PCIe Root Complex is a PCIe bus, and therefore that the CAM aperture is used for ECAM. Conventional PCI is currently not implemented.

|    **Hypercall**:       |      `vpci_configure`                    |
|-------------------------|------------------------------------------|
|     Call number:        |     `hvc 0x606D`                         |
|     Inputs:             |     X0: VPCI CapID                       |
|                         |     X1: Address Space CapID              |
|                         |     X2: VIC CapID                        |
|                         |     X3: CAM Aperture                     |
|                         |     X4: Non-prefetchable Memory Aperture |
|                         |     X5: Prefetchable Memory Aperture     |
|                         |     X6: I/O Aperture                     |
|                         |     X7: vPCIOptionFlags                  |
|     Outputs:            |     X0: Error Result                     |

**Types:**

*vPCIOptionFlags:*

|      Bit Numbers     |      Mask                  |      Description                        |
|----------------------|----------------------------|-----------------------------------------|
|     0                |     `0x1`                  |     1: PCIe Root Complex; 0: PCI Bus    |
|     63:1             |     `0xFFFFFFFF.FFFFFFFE`  |     Reserved,   Must be Zero            |

*Aperture:*

| Bit Numbers | Mask                  | Description                                     |
|-------------|-----------------------|-------------------------------------------------|
| 5:0         | `0x0000003F`          | Size of the aperture, measured in address bits. |
| 11:6        | `0x00000FC0`          | Reserved — Must be Zero                         |
| 63:12       | `0xFFFFFFFF.FFFFF000` | Base address of the aperture (VMAddr)           |

**Errors:**

OK – the operation was successful.

ERROR_OBJECT_STATE – if the Virtual PCIe Root Complex object is not in OBJECT_STATE_INIT state.

ERROR_ADDR_INVALID – One of the specified address ranges was not within the address space's valid address range.

ERROR_ARGUMENT_SIZE — One of the specified apertures was too small or too large.

ERROR_ARGUMENT_INVALID — A zero size was specified for the Non-prefetchable Memory aperture.

Also see: [Capability Errors](#capability-errors)

### Attach a Virtual PCI Device

Attach a Virtual PCI device object to a bus. The device may be any hypervisor object that supports presenting a PCI interface to a VM.

This operation can only be performed prior to activating the Virtual PCIe Root Complex object. Attaching a device to a Virtual PCI Bus that is already active is akin to hot-plugging a physical device, which may be supported in future releases.

The slot index is a value between 0 and 31 inclusive that is not already used by a device on the virtual PCI bus. If an index of -1 is specified, the bus will allocate a slot index automatically; the allocated index will be returned. Note that slot 0 of bus 0 is reserved for the host bridge.

Any other requirements are specific to the virtual device type.

|    **Hypercall**:       |      `vpci_attach`                |
|-------------------------|-----------------------------------|
|     Call number:        |     `hvc 0x606E`                  |
|     Inputs:             |     X0: VPCI CapID                |
|                         |     X1: Slot Index                |
|                         |     X2: Device CapID              |
|                         |     X3: Reserved   – Must be Zero |
|     Outputs:            |     X0: Error Result              |
|                         |     X1: Slot Index                |

**Errors:**

OK – the operation was successful.

ERROR_BUSY – The specified device is already attached to a bus.

ERROR_NORESOURCES – The specified slot has a device in it, or there is no free slot if none was specified.

ERROR_FAILURE – The device is misconfigured or invalid in some way, such as not having a function 0.

ERROR_ARGUMENT_INVALID — An out-of-range slot index was specified.

Also see: [Capability Errors](#capability-errors)

## PRNG Management

### PRNG Get Entropy

Gets random numbers from a DRBG that is seeded by a TRNG. Typically this API will source randomness from a NIST/FIPS compliant hardware device.

|    **Hypercall**:   |  `prng_get_entropy`            |
|---------------------|--------------------------------|
|     Call number:    |  `hvc 0x6057`                  |
|     Inputs:         |  X0: NumBytes                  |
|                     |  X1: Reserved — Must be Zero   |
|     Outputs:        |  X0: Error Result              |
|                     |  X1: Data0                     |
|                     |  X2: Data1                     |
|                     |  X3: Data2                     |
|                     |  X4: Data3                     |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_SIZE – the NumBytes provided is zero, or exceeds the possible bytes to be returned in the Data output registers.

ERROR_BUSY – Called within the read rate-limit window.

ERROR_UNIMPLEMENTED – if functionality not implemented.

Also see: [Capability Errors](#capability-errors)

## SDEI

### SDEI Get Error Flags

Gets the error flags for the running SDEI error event. This should be called from the error event handler.

|    **Hypercall**:   |  `sdei_get_error_flags`        |
|---------------------|--------------------------------|
|     Call number:    |  `hvc 0x6080`                  |
|     Inputs:         |  X0: Reserved — Must be zero   |
|     Outputs:        |  X0: Error Result              |
|                     |  X1: SDEI Error Flags          |

**Types:**

*SDEI Error Flags*

| Bit Numbers |  Mask                 |  Description                          |
|-------------|-----------------------|---------------------------------------|
| 31:0        | `0xFFFFFFFF`          | SDEI Error Reason                     |
| 62:32       | `0x7FFFFFFF.00000000` | Reserved,   Must be Zero              |
| 63          | `0x80000000.00000000` | 1: System error; 0: VM-specific error |

*SDEI Error Reason*

| Reason                | Integer Value | Description                                             |
|-----------------------|---------------|---------------------------------------------------------|
| UNKNOWN               | 0             | Unknown reason.                                         |
| SENT_BY_USER          | 1             | The event was sent by the user across VMs.              |
| VIRTUAL_WATCHDOG_BITE | 2             | The event was triggered due to a virtual watchdog bite. |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_DENIED – if not called from the error event handler.

ERROR_UNIMPLEMENTED – if functionality not implemented.

## Virtual IOMMU

### Virtual IOMMU Bind Streams

Bind streams to a virtual IOMMU.

|    **Hypercall**:   |  `viommu_bind_streams`                       |
|---------------------|----------------------------------------------|
|     Call number:    |  `hvc 0x6090`                                |
|     Inputs:         |  X0: vIOMMU CapID                            |
|                     |  X1: IOMMU CapID                             |
|                     |  X2: First Stream ID                         |
|                     |  X3: Number of Consecutive Streams           |
|                     |  X4: Reserved — Must be Zero                 |
|     Outputs:        |  X0: Error Result                            |
|                     |  X1: Number of Successfully Bound Streams    |

**Stream ID format:**

*Virtual SMMUv2:*

| Bits  | Mask                  | Description             |
|-------|-----------------------|-------------------------|
| 63:32 | `0xFFFFFFFF.00000000` | Reserved — Must be Zero |
| 31:16 | `0x00000000.FFFF0000` | Mask                    |
| 15:0  | `0x00000000.0000FFFF` | ID                      |

**Errors:**

OK – the operation was successful, or the streams were already bound.

ERROR_ARGUMENT_SIZE – the number of consecutive streams is invalid.

ERROR_ARGUMENT_INVALID – the specified devices or streams were not found.

ERROR_ADDR_INVALID – an address space is not attached to the virtual IOMMU.

ERROR_DENIED – the provided streams are not compatible with the virtual IOMMU.

ERROR_BUSY – the provided streams are currently in use.

ERROR_UNIMPLEMENTED – if functionality not implemented.

Also see: [Capability Errors](#capability-errors)

### Virtual IOMMU Unbind Streams

Unbind streams from a virtual IOMMU.

|    **Hypercall**:   |  `viommu_unbind_streams`                     |
|---------------------|----------------------------------------------|
|     Call number:    |  `hvc 0x6091`                                |
|     Inputs:         |  X0: vIOMMU CapID                            |
|                     |  X1: First Stream ID                         |
|                     |  X2: Number of Consecutive Streams           |
|                     |  X3: Reserved — Must be Zero                 |
|     Outputs:        |  X0: Error Result                            |
|                     |  X1: Number of Successfully Unbound Streams  |

**Errors:**

OK – the operation was successful, or the streams were already unbound.

ERROR_ARGUMENT_SIZE – the number of consecutive streams is invalid.

ERROR_ARGUMENT_INVALID – the specified devices or streams were not found.

ERROR_ADDR_INVALID – an address space is not attached to the virtual IOMMU.

ERROR_BUSY – the provided streams are currently in use.

ERROR_UNIMPLEMENTED – if functionality not implemented.

Also see: [Capability Errors](#capability-errors)

## Physical PCIe Root Complex Management

A Physical PCIe Root Complex object may be used to instruct the hypervisor to take control of an enumerated physical PCIe Root Complex. The hypervisor will restrict access to the bus by the VM that owns it, so that lending of individual functions to other VMs can be accomplished safely without trusting the owner VM.

### Configure a Physical PCIe Root Complex

This hypercall provides the mandatory basic configuration for a Physical PCIe Root Complex: the address space of the owner VM, a Memory Extent object covering the configuration space MMIO aperture, the base and size of the configuration space aperture, and a set of flags to determine the runtime behaviour of the object.

The specified Memory Extent object must be a basic extent that represents the physical address range of the Enhanced Configuration Access Mechanism (ECAM) MMIO aperture. This extent must be size-aligned with a power-of-two size between 2MB and 256MB as per the PCIe specification. Root Complex implementations that use alternative configuration space access mechanisms are not supported. The extent may already be mapped in the address space at the specified base address. If not, it will be automatically be mapped when the object is activated.

Bit 0 of the option flags ("PCIe Enable") must be set to indicate that the Physical PCIe Root Complex is a PCIe rather than conventional PCI interface. If this bit is set, the configuration aperture is expected to have ECAM layout, with 4KB of address space per function and a dummy host bridge at address 0. Conventional PCI, which has a configuration aperture with CAM layout (256 bytes per function) and a type 1 configuration header in the host bridge, is currently not supported.

Bit 1 of the option flags ("Lockdown Enable") may be set to indicate that the donor VM is untrusted and therefore that the Root Complex must be in Restricted state before function passthrough objects may be derived from it. If this bit is not set, the hypervisor trusts the donor VM to maintain the memory resource assignments of functions that have been assigned as passthrough.

|    **Hypercall**:       |      `pci_host_configure`                |
|-------------------------|------------------------------------------|
|     Call number:        |     `hvc 0x607B`                         |
|     Inputs:             |     X0: PCI Root Complex CapID           |
|                         |     X1: Address Space CapID              |
|                         |     X2: CAM Memory Extent CapID          |
|                         |     X3: CAM Base VMAddr                  |
|                         |     X4: PCIOptionFlags                   |
|                         |     X5: Reserved — Must be Zero          |
|     Outputs:            |     X0: Error Result                     |

**Types:**

*PCIOptionFlags:*

|      Bit Numbers     |      Mask                  |      Description                         |
|----------------------|----------------------------|------------------------------------------|
|     0                |     `0x1`                  |     1: PCIe Root Complex; 0: PCI Bus     |
|     1                |     `0x2`                  |     Enable lockdoon (untrusted donor VM) |
|     63:2             |     `0xFFFFFFFF.FFFFFFFC`  |     Reserved,   Must be Zero             |

**Errors:**

OK – the operation was successful.

ERROR_OBJECT_STATE – if the Physical PCIe Root Complex object is not in OBJECT_STATE_INIT state.

ERROR_ARGUMENT_SIZE – the specified CAM Aperture's size is not in the valid range (21 to 28 address bits).

ERROR_ARGUMENT_ALIGNMENT – the specified CAM Aperture is not size-aligned.

### Configure a Physical PCIe Memory Aperture

This hypercall adds a PCI Memory aperture to a Physical PCIe Root Complex object. This should be called at least once before activating a Root Complex object.

This call is used to inform the hypervisor of the physical address ranges of a PCIe Root Complex's PCI Memory apertures. For each aperture, the caller must specify a basic Memory Extent object that covers the aperture's entire address range. The size and base address of the aperture must be page-aligned. The extent may already be mapped in the owner VM's address space at the specified base address; if not, it will be automatically mapped when the object is activated.

|    **Hypercall**:       |      `pci_host_add_aperture`             |
|-------------------------|------------------------------------------|
|     Call number:        |     `hvc 0x607C`                         |
|     Inputs:             |     X0: PCI Root Complex CapID           |
|                         |     X1: Memory Extent CapID              |
|                         |     X2: Memory Base VMAddr               |
|                         |     X3: Reserved — Must be Zero          |
|     Outputs:            |     X0: Error Result                     |

**Errors:**

OK – the operation was successful.

ERROR_OBJECT_STATE – if the Physical PCIe Root Complex object is not in OBJECT_STATE_INIT state.

### Physical PCIe Lockdown State

This hypercall sets the lockdown state of a Physical PCIe Root Complex that was configured with lockdown support enabled.

There are three possible states:

Permissive
: The ECAM and Memory apertures are fully accessible to the owner VM. No derived PCIe Function objects exist. This is the initial state.

Scanning
: The ECAM aperture is fully read-only to the owner VM. Any VCPU that attempts to write to the ECAM aperture will be blocked until this state is exited. Any VCPU access to a Memory aperture may be trapped and briefly blocked in order to serialise it against BAR probes. Derived PCIe Function objects may be created and activated, but passthrough cannot be enabled.

Restricted
: The ECAM aperture is partially read-only to the owner VM; attempts to write to any register that has not been explicitly marked as read-only may generate emulated bus errors in the VM. Existing derived PCIe Function objects may have passthrough enabled, but new PCIe Function objects cannot be created.

Transitions are permitted from Permissive to Scanning state, from Scanning to Restricted state, and from Scanning or Restricted to Permissive state. Transitions to Permissive state will fail if any PCIe Function object exists that has been derived from this Root Complex; the caller is expected to ensure that all such objects have been destroyed first. Transitions to Restricted state will fail if any PCIe Function object exists that is not in `OBJECT_STATE_ACTIVE` state.

In order to enable function passthrough, the caller is expected to move the Root Complex into Scanning state, map the ECAM into its own address space, scan it to identify the functions that are present and create and activate PCIe Function objects for each of them (including for those that will not be used for passthrough), and finally move the Root Complex into Restricted state.

|    **Hypercall**:       |      `pci_host_set_lockdown`             |
|-------------------------|------------------------------------------|
|     Call number:        |     `hvc 0x607D`                         |
|     Inputs:             |     X0: PCI Root Complex CapID           |
|                         |     X1: PCILockdownState                 |
|                         |     X2: Reserved — Must be Zero          |
|     Outputs:            |     X0: Error Result                     |

**Types:**

*PCILockdownState:*

| State Enumerator | Integer Value |
|-----------------------------------------|---|
| PCI_HOST_LOCKDOWN_STATE_PERMISSIVE | 0 |
| PCI_HOST_LOCKDOWN_STATE_SCANNING | 1 |
| PCI_HOST_LOCKDOWN_STATE_RESTRICTED | 2 |

**Errors:**

OK – the operation was successful.

ERROR_OBJECT_CONFIG — the specified Physical PCIe Root Complex object was not configured with bus lockdown enabled.

ERROR_ARGUMENT_INVALID — the requested state was not valid.

ERROR_DENIED — the requested state transition was not one of the permitted transitions described above.

ERROR_BUSY — a transition into Permissive state from Scanning or Restricted state could not proceed because at least one derived PCIe Function object exists, or a transition into Restricted state from Scanning state could not proceed because at least one derived PCIe Function object has been created but not yet activated.

### Physical PCIe Function Configuration

This hypercall may be called to configure a PCIe Function object to track a specified physical function attached to a specified PCIe Root Complex object.

The specified Root Complex must either have lockdown disabled, or be in Scanning state. If lockdown is enabled, the Root Complex will be forced to remain in Scanning state until the PCIe Function object is activated or destroyed. Once activated, the PCIe Function object will prevent the Root Complex entering Permissive state for as long as the object continues to exist.

The specified Responder ID must not have already been assigned to another PCIe Function object and must have a bus ID that is within the valid range determined by the size of the ECAM aperture.

If the "Enable Passthrough" flag is set, then the function will be configured to permit use as a passthrough device attached to a Virtual PCIe Root Complex. Note that this may allocate additional hypervisor memory, so it should not be set for functions that don't require passthrough support.

If the "Is SR-IOV VF" flag is set, the hypervisor will not scan the device's BARs to generate Memory Extent objects for them. Instead, the caller must derive a Memory Extent object for each VF BAR and provide it using a separate configuration hypercall. That hypercall is currently not implemented, so passthrough of SR-IOV devices is not yet supported.

|    **Hypercall**:       |      `pci_function_configure`            |
|-------------------------|------------------------------------------|
|     Call number:        |     `hvc 0x607F`                         |
|     Inputs:             |     X0: PCI Function CapID               |
|                         |     X1: PCI Root Complex CapID           |
|                         |     X2: Address PCIFunctionID            |
|                         |     X3: Flags PCIFunctionFlags           |
|                         |     X4: Reserved — Must be Zero          |
|     Outputs:            |     X0: Error Result                     |

**Types:**

*PCIFunctionID:*

| Bit Numbers | Mask                  | Description                      |
|-------------|-----------------------|----------------------------------|
| 2:0         | `0x00000007`          | Function number |
| 3:7         | `0x000000F8`          | Device (slot) number |
| 15:8        | `0x0000FF00`          | Bus number |
| 63:16       | `0xFFFFFFFF.FFFF0000` | Reserved — Must be Zero |

*PCIFunctionFlags:*

| Bit Numbers | Mask                  | Description                      |
|-------------|-----------------------|----------------------------------|
| 0 | `0x00000001` | Enable Passthrough |
| 1 | `0x00000002` | Is SR-IOV VF |
| 63:2 | `0xFFFFFFFF.FFFFFFFC` | Reserved — Must be Zero |

**Errors:**

OK – the operation was successful.

ERROR_BUSY — the specified address is already owned by another PCIe Function object.

### Physical PCIe Function Capability Access Control

This hypercall may be used to configure the parts of a PCIe Function's configuration space that are accessible to VCPUs accessing it either as a passthrough device, or through a Physical PCIe Root Complex in Restricted mode. It can only be called before placing the function into passthrough mode by calling `pci_function_set_passthrough`. If the parent Root Complex has lockdown enabled, it can only be called when that object is in Scanning mode.

The restrictions on the Type 0 or Type 1 configuration header registers depend only on whether the function is a Type 1 (bridge) function, a Type 0 SR-IOV Virtual Function, or a non-VF Type 0 function. Only the capability structures have per-function permissions, hence the name of this hypercall.

The specified offset and size must be within the bounds of either the capability region (0x40–0xFF) or the extended capability region (0x100–0xFFF). The class of capability (conventional or extended) will be inferred from the offset. The hypervisor may not validate the location and type of the capability, though it may check that registered capabilities do not overlap.

If the "Passthrough Visible" flag is set, then the capability will be accessible with read permissions when the function is attached as a passthrough function on a Virtual PCIe Bus. If the "Passthrough Writable" flag is also set, then the capability will be accessible with read and write permissions. Note that the capability's offset may not be the same in the virtual ECAM as it is in the physical ECAM.

If the "Lockdown Visible" flag is set, then the capability will be accessible with read permissions when the function has passthrough disabled and its Physical PCIe Root Complex is in Restricted state. If the "Lockdown Writable" flag is also set, then the capability will be accessible with read and write permissions. Capabilities that are not registered in this way will be replaced with a virtualised Null Extended Capability when they are in Restricted state.

|    **Hypercall**:       |      `pci_function_add_capability`       |
|-------------------------|------------------------------------------|
|     Call number:        |     `hvc 0x6086`                         |
|     Inputs:             |     X0: PCI Function CapID               |
|                         |     X1: Offset Size                      |
|                         |     X2: Size                             |
|                         |     X3: PCIAccessFlags                   |
|                         |     X4: Reserved — Must be Zero          |
|     Outputs:            |     X0: Error Result                     |

**Types:**

*PCIAccessFlags:*

| Bit Numbers | Mask                  | Description                      |
|-------------|-----------------------|----------------------------------|
| 0 | `0x00000001` | Passthrough Visible |
| 1 | `0x00000002` | Passthrough Writable |
| 2 | `0x00000004` | Lockdown Visible |
| 3 | `0x00000008` | Lockdown Writable |
| 63:4 | `0xFFFFFFFF.FFFFFFF0` | Reserved — Must be Zero |

**Errors:**

OK – the operation was successful.

ERROR_ARGUMENT_INVALID — The specified offset and size were not within the range 0x40–0xFF, nor within the range 0x100–0xFFF.

ERROR_ARGUMENT_ALIGNMENT — The specified offset was not a multiple of 4.

ERROR_OBJECT_CONFIG — The Lockdown Visible flag was set, but the Physical PCIe Function's parent Root Complex object was not configured with lockdown enabled; or the Passthrough Visible flag was set, but the PCIe Function object was not configured with passthrough enabled.

ERROR_OBJECT_STATE — The PCIe Function already has passthrough enabled, or is attached to a Root Complex that has lockdown enabled and is not in Scanning state.

### Physical PCIe Function Passthrough Enable

This hypercall may be used to enable or disable function passthrough for a PCIe Function object that has been configured to support it.

If this call is made with the Enable flag set to true, the hypervisor will unmap the function's BAR ranges from the Physical PCIe Root Complex's associated address space, and stop emulating trapped writes to the function's ECAM range. Afterwards, the caller may bind the function to a Virtual PCIe Root Complex by calling `vpci_attach`.

If the Enable flag is true and the associated Physical PCIe Root Complex has lockdown support enabled, it must be in Restricted state. If the Enable flag is false, the PCIe Function must not currently be bound to a Virtual PCIe slot.

#### Usage

For secure lending of a PCIe Function between mutually distrusting VMs, the management VM is expected to follow this sequence of operations:

1. Call this hypercall to enable passthrough.
2. Unmap any platform-specific resources associated with the function, such as MSI and IOMMU endpoints, from the donor VM.
3. Access the function's configuration registers directly to:
    * Save the state of the BARs (this may be done earlier, during scanning).
    * Issue a function-level reset, then wait for up to 100ms for the reset to complete.
    * Restore the BARs to their pre-reset values.
4. Map the platform-specific resources to the recipient VM.
5. Call `vpci_attach` to expose the function to the recipient VM.

Note that if a function does not implement the generic Function Level Reset operation specified in the PCIe standard, it is only possible to safely move it between VMs that do not completely trust each other if the management VM has specific knowledge of the device's behaviour and is able to use device-specific mechanisms to place it into a quiescent state.

Securely reclaiming the PCIe function for use in the donor VM, or to move the Root Complex back to permissive state, requires a similar, but reversed, sequence.


|    **Hypercall**:       |      `pci_function_set_passthrough`      |
|-------------------------|------------------------------------------|
|     Call number:        |     `hvc 0x6087`                         |
|     Inputs:             |     X0: PCI Function CapID               |
|                         |     X1: Enable Boolean                   |
|                         |     X2: Reserved — Must be Zero          |
|     Outputs:            |     X0: Error Result                     |

Also see: [Capability Errors](#capability-errors)

## Error Results

### Error Code Enumeration

|      Error Enumerator                   |      Integer Value     |
|-----------------------------------------|------------------------|
|     OK                                  |     0                  |
|     ERROR_UNIMPLEMENTED                 |     -1                 |
|     ERROR_RETRY                         |     -2                 |
|                                         |                        |
|     ERROR_ARGUMENT_INVALID              |     1                  |
|     ERROR_ARGUMENT_SIZE                 |     2                  |
|     ERROR_ARGUMENT_ALIGNMENT            |     3                  |
|                                         |                        |
|     ERROR_NOMEM                         |     10                 |
|     ERROR_NORESOURCES                   |     11                 |
|     ERROR_ADDR_OVERFLOW                 |     20                 |
|     ERROR_ADDR_UNDERFLOW                |     21                 |
|     ERROR_ADDR_INVALID                  |     22                 |
|     ERROR_DENIED                        |     30                 |
|     ERROR_BUSY                          |     31                 |
|     ERROR_IDLE                          |     32                 |
|     ERROR_OBJECT_STATE                  |     33                 |
|     ERROR_OBJECT_CONFIG                 |     34                 |
|     ERROR_OBJECT_CONFIGURED             |     35                 |
|     ERROR_FAILURE                       |     36                 |
|                                         |                        |
|     ERROR_VIRQ_BOUND                    |     40                 |
|     ERROR_VIRQ_NOT_BOUND                |     41                 |
|                                         |                        |
|     ERROR_CSPACE_CAP_NULL               |     50                 |
|     ERROR_CSPACE_CAP_REVOKED            |     51                 |
|     ERROR_CSPACE_WRONG_OBJECT_TYPE      |     52                 |
|     ERROR_CSPACE_INSUFFICIENT_RIGHTS    |     53                 |
|     ERROR_CSPACE_FULL                   |     54                 |
|                                         |                        |
|     ERROR_MSGQUEUE_EMPTY                |     60                 |
|     ERROR_MSGQUEUE_FULL                 |     61                 |
|                                         |                        |
|     ERROR_MEMDB_NOT_OWNER               |     111                |
|     ERROR_MEMEXTENT_MAPPINGS_FULL       |     120                |
|     ERROR_MEMEXTENT_TYPE                |     121                |
|     ERROR_EXISTING_MAPPING              |     200                |

### Capability Errors

ERROR_CSPACE_CAP_NULL – invalid CapID.

ERROR_CSPACE_CAP_REVOKED – CapID no longer valid since it has already been revoked.

ERROR_CSPACE_WRONG_OBJECT_TYPE – CapID does not correspond with the specified object type.

ERROR_CSPACE_INSUFFICIENT_RIGHTS – CapID has not enough rights to execute operation.

ERROR_CSPACE_FULL – CSpace has reached maximum number of capabilities allowed.
