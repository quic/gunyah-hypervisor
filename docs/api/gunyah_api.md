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

### Memory Extent Rights

| Right             |  Value            |
|-------------------|-------------------|
| Memory Extent Map    | `0x00000001` |
| Memory Extent Derive | `0x00000002` |
| Memory Extent Attach | `0x00000004` |
| Memory Extent Lookup | `0x00000008` |
| Memory Extent Donate | `0x00000010` |

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

### Virtual IO MMIO Rights

| Right             |  Value            |
|-------------------|-------------------|
| Virtual IO MMIO Bind Backend VIRQ  | `0x00000001` |
| Virtual IO MMIO Bind Frontend VIRQ | `0x00000002` |
| Virtual IO MMIO Assert VIRQ   | `0x00000004` |
| Virtual IO MMIO Config        | `0x00000008` |

### Virtual GIC ITS

| Right             |  Value            |
|-------------------|-------------------|
| Virtual GIC ITS Bind VIC | `0x00000001` |


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
|     0                |     `0x1`                  |     1 = ARM v8.2 SVE support    |
|     63:1             |     `0xFFFFFFFF.FFFFFFFF ` |     Reserved = 0                |

*API Flags 2:*

| Bits | Mask | Description |
|-|---|-----|
|     63:0             |     `0xFFFFFFFF.FFFFFFFF`  |     Reserved = 0     |


## Partitions

### Partition Object Creation

Allocates a new Partition object from the Partition and allocates a Capability ID from the CSpace.

|    **Hypercall**:       |      `partition_create_partition`    |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6001`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Partition CapID              |

On successful creation, the new Partition object is created, and its state is OBJECT_STATE_INIT.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Capability Space Object Creation

Allocates a new CSpace object from the Partition and allocates a Capability ID from the CSpace.

|    **Hypercall**:       |      `partition_create_cspace`       |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6002`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Cspace CapID                 |

On successful creation, the new CSpace object is created and its state is OBJECT_STATE_INIT.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)


### Address Space Object Creation

Allocates a new Address Space object from the Partition and allocates a Capability ID from the CSpace.

|    **Hypercall**:       |      `partition_create_addrspace`    |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6003`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

On successful creation, the new Address Space object is created and its state is OBJECT_STATE_INIT.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Memory Extent Object Creation

Allocates a new Memory Extent object from the Partition and allocates a Capability ID from the CSpace.

|    **Hypercall**:       |      `partition_create_memextent`    |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6004`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: MemExtent CapID              |

On successful creation, the new Memory Extent object is created and its state is OBJECT_STATE_INIT.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Thread (Virtual CPU) Object Creation

Allocates a new Thread object from the Partition and allocates a Capability ID from the CSpace.

|    **Hypercall**:       |      `partition_create_thread`       |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6005`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Thread CapID                 |

On successful creation, the new Thread object is created and its state is OBJECT_STATE_INIT.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Doorbell Object Creation

Allocates a new Doorbell object from the Partition and allocates a Capability ID from the CSpace.

|    **Hypercall**:       |      `partition_create_doorbell`     |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6006`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Doorbell CapID               |

On successful creation, the new Doorbell object is created and its state is OBJECT_STATE_INIT.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Message Queue Object Creation

Allocates a new Message Queue object from the Partition and allocates a Capability ID from the CSpace.

|    **Hypercall**:       |      `partition_create_msgqueue`       |
|-------------------------|----------------------------------------|
|     Call number:        |     `hvc 0x6007`                       |
|     Inputs:             |     X0: Partition CapID                |
|                         |     X1: CSpace CapID                   |
|                         |     X2: Reserved — Must be Zero        |
|     Outputs:            |     X0: Error Result                   |
|                         |     X1: MessageQueue CapID             |

On successful creation, the new Message Queue object is created and its state is OBJECT_STATE_INIT.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Watchdog object creation

Allocates a new Watchdog object from the Partition and allocates a Capability ID from the CSpace.

|    **Hypercall**:       |      `partition_create_watchdog`     |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6009`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Watchdog CapID               |

On successful creation, the new Watchdog object is created and its state is OBJECT_STATE_INIT.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Virtual Interrupt Controller Object Creation

Allocates a new Virtual Interrupt Controller object from the Partition and allocates a Capability ID from the CSpace.

|    **Hypercall**:       |      `partition_create_vic`          |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x600A`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Virtual IC CapID             |

On successful creation, the new Virtual Interrupt Controller object is created and its state is OBJECT_STATE_INIT.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Virtual PM Group Object Creation

Allocates a new Virtual PM Group object from the Partition and allocates a Capability ID from the CSpace.

|    **Hypercall**:       |      `partition_create_vpm_group`    |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x600B`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: VPMGroup CapID               |

On successful creation, the new Virtual PM Group object is created and its state is OBJECT_STATE_INIT.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

Also see: [Capability Errors](#capability-errors)

### Virtual IO MMIO object creation

Allocates a new Virtual IO MMIO object from the Partition and allocates a Capability ID from the CSpace.

|    **Hypercall**:       |      `partition_create_virtio_mmio`   |
|-------------------------|---------------------------------------|
|     Call number:        |     `hvc 0x6048`                      |
|     Inputs:             |     X0: Partition CapID               |
|                         |     X1: CSpace CapID                  |
|                         |     X2: Reserved — Must be Zero       |
|     Outputs:            |     X0: Error Result                  |
|                         |     X1: VirtioMMIO CapID              |

On successful creation, the new Virtual IO MMIO object is created and its state is OBJECT_STATE_INIT.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the creation failed due to memory allocation error.

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

### Hardware IRQ Bind

Binds a hardware IRQ number to a virtual IRQ number.

|    **Hypercall**:       |      `hwirq_bind_virq`               |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6026`                     |
|     Inputs:             |     X0: HW IRQ CapID                 |
|                         |     X1: Virtual IC CapID             |
|                         |     X2: Virtual IRQ Info             |
|                         |     X3: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_VIRQ_BOUND – the specified hardware IRQ is already bound to a VIRQ number.

ERROR_BUSY – the specified VIRQ number is already bound to a source.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Virtual IRQ Info value.

Also see: [Capability Errors](#capability-errors)

### Hardware IRQ Unbind

Unbinds a hardware IRQ number from a virtual IRQ number.

|    **Hypercall**:       |      `hwirq_unbind_virq`             |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6027`                     |
|     Inputs:             |     X0: HW IRQ CapID                 |
|                         |     X1: Reserved — Must be Zero      |
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

Attaches a message-signalled interrupt (MSI) source object to a Virtual Interrupt Controller, permitting interrupt messages from the source to be routed to virtual interrupts. The Virtual Interrupt Controller object must have been activated before this function is called. An attachment index must be specified which is unique among the MSI source attachments to the controller. If the MSI source has a memory-mapped interface, the attachment index may be used to determine its address in the VM address space.

Each MSI source capability represents one or more physical devices or buses. Capabilities are provided to the root VM at boot time and cannot be created dynamically, though some MSI source objects may permit capabilities to be derived with restricted rights. The number of MSI sources available depends on the target platform, and may be zero.

In the current implementation, the only type of MSI source supported is a GICv4 ITS. One MSI source capability is provided to the root VM for each physical ITS present in the system.

|    **Hypercall**:       |      `vic_bind_msi_source`                                    |
|-------------------------|---------------------------------------------------------------|
|     Call number:        |     `hvc 0x6056`                                              |
|     Inputs:             |     X0: Virtual IC CapID                                      |
|                         |     X1: MSI Source (platform-specific object type)   CapID    |
|                         |     X2: Index                                                 |
|                         |     X3: Reserved — Must be Zero                               |
|     Outputs:            |     X0: Error Result                                          |


**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_ARGUMENT_INVALID – the specified attachment index is outside the range supported by this Virtual Interrupt Controller.

ERROR_OBJECT_STATE – The VCPU object has already been activated, or the Virtual Interrupt Controller object has not yet been activated.

Also see: [Capability Errors](#capability-errors)

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

If the Partial flag is set in Map Flags, only the range of the memory extent specified by Offset and Size will be mapped. If not set, these arguments are ignored. Partial mappings are only supported by sparse memory extents.

If successful, the hypervisor will automatically synchronise with other cores to ensure they have observed the map operation. This behaviour is skipped if the NoSync flag is set.

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
|     31               |     `0x80000000`           |     NoSync                        |
|     30:1             |     `0x7FFFFFFE`           |     Reserved, Must be Zero        |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Address Space.

ERROR_MEMEXTENT_MAPPINGS_FULL – the memory extent has exceeded its mappings capacity. Currently it can have up to 4 mappings.

ERROR_DENIED – the specified Address Space is not allowed to execute map operations.

ERROR_ARGUMENT_ALIGNMENT – the specified base address is not page size aligned.

ERROR_ADDR_OVERFLOW – the specified base address may cause an overflow.

Also see: [capability errors](#capability-errors)

### Address Space Unmap

Unmaps a memory extent from a specified address space. By default, the entire memory extent range is unmapped, except for any carveouts contained within the extent.

If the Partial flag is set in Map Flags, only the range of the Memory Extent specified by Offset and Size will be unmapped. If not set, these arguments are ignored. Partial unmappings are only supported by sparse memory memextents.

If successful, the hypervisor will automatically synchronise with other cores to ensure they have observed the unmap operation. This behaviour is skipped if the NoSync flag is set.

|    **Hypercall**:       |      `addrspace_unmap`               |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x602c`                     |
|     Inputs:             |     X0: Address Space CapID          |
|                         |     X1: Memory Extent CapID          |
|                         |     X2: Base VMAddr                  |
|                         |     X3: Map Flags                    |
|                         |     X4: Offset                       |
|                         |     X5: Size                         |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Address Space or a non-existing mapping.

ERROR_DENIED – the specified Address Space is not allowed to execute map operations.

ERROR_ARGUMENT_ALIGNMENT – the specified base address is not page size aligned.

Also see: [capability errors](#capability-errors)

### Address Space Update Access

Update access rights on an existing mapping.

If the Partial flag is set in Map Flags, only the range of the Memory Extent specified by Offset and Size will be updated. If not set, these arguments are ignored. Partial access updates are only supported by sparse memory extents.

If successful, the hypervisor will automatically synchronise with other cores to ensure they have observed the mapping update. This behaviour is skipped if the NoSync flag is set.

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

Configure the information area of an address space whose state is OBJECT_STATE_INIT.

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

ERROR_ADDR_INVALID – The provided IPA is invalid.

ERROR_ARGUMENT_INVALID – A value passed in an argument was invalid.

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

### Address Space Virtual MMIO Area Configuration

Configure the virtual MMIO device regions for the address space.

A virtual MMIO device region is a region of the address space in which translation faults may be handled by an unprivileged VMM residing in another VM.
This allows the unprivileged VMM to emulate memory-mapped I/O devices.
Note that other types of fault, such as permission or alignment faults, cannot be handled by this mechanism.
Also, depending on the architecture, this mechanism may only support translation faults generated by specific types of instruction.
On AArch64, it is limited to single-register load & store instructions without base register writeback, which are decoded by the CPU into the `ESR_EL2` syndrome bits.

This call may be made before or after activation of the address space object.
This is to permit delegation of the right to call this API to the VM that runs in the address space, so it can explicitly acknowledge that the specified region should not be used for sensitive data.

An address range that is added must not overlap any existing range, and must not wrap around the end of the address space.
There are no other restrictions on the size or alignment of ranges added to the address space.
However, a limit may be imposed on the total number of ranges added to an address space.

A removed address range must exactly match a single previously added address range. Note that removal of a range will prevent the VMM receiving any new faults that occur in that range after the removal operation completes, but does not guarantee that the VMM has finished handling all faults in the removed range.

|    **Hypercall**:       |      `addrspace_configure_vmmio`   |
|-------------------------|------------------------------------|
|     Call number:        |     `hvc 0x6060`                   |
|     Inputs:             |     X0: Address Space CapID        |
|                         |     X1: Base VMAddr                |
|                         |     X2: Size                       |
|                         |     X3: VMMIOConfigureOperation    |
|                         |     X4: Reserved   – Must be Zero  |
|     Outputs:            |     X0: Error Result               |

**Types:**

*VMMIOConfigureOperation:*

|      Operation Enumerator               |      Integer Value     |
|-----------------------------------------|------------------------|
|     VMMIO_CONFIGURE_OP_ADD_RANGE        |     0                  |
|     VMMIO_CONFIGURE_OP_REMOVE_RANGE     |     1                  |

**Errors:**

OK – the operation was successful.

ERROR_ADDR_OVERFLOW – the specified range wraps around the end of the address space.

ERROR_ADDR_INVALID – the specified range is not completely within the input address range of the address space.

ERROR_ARGUMENT_INVALID – the specified range to be added overlaps a previously added range, or the specified range to be removed does not match a previously added range.

ERROR_NORESOURCES – the number of nominated ranges has reached an implementation-defined limit, or the hypervisor was unable to allocate memory for bookkeeping.

ERROR_UNIMPLEMENTED — unprivileged VMMs are unable to handle faults in this configuration, or an unknown operation was requested.

Also see: [capability errors](#capability-errors)

## Memory Extent Management

### Memory Extent Modify

Perform a modification on a memory extent.

For range operations, only the range of the memory extent specified by Offset and Size will be modified. For all other operations these arguments are ignored.

For operations that affect address space mappings, the hypervisor will automatically synchronise with other cores to ensure they have observed any successful changes in mappings. This behaviour is skipped if the NoSync flag is set. For other operations the NoSync flag must be set as specified below.

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
|     31               |     `0x80000000`  |     List Append                 |
|     30:18,15:10,7:3  |     `0x7FFCFCF8`  |     Reserved,   Must be Zero    |

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

If successful, the hypervisor will automatically synchronise with other cores to ensure they have observed the donation and any mapping changes that may have occurred. This behaviour is skipped if the NoSync flag is set.

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

|    Memextent Donate Type     |   Integer Value     |   Description                                   |
|------------------------------|---------------------|-------------------------------------------------|
|     TO_CHILD                 |    0                |    Donate to a child extent from its parent.    |
|     TO_PARENT                |    1                |    Donate from a child extent to its parent.    |
|     TO_SIBLING               |    2                |    Donate from one sibling extent to another.   |

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
|     63:2             |     `0xFFFFFFFF.FFFFFFFE`  |     Reserved,   Must be Zero            |

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
|                         |     X1: Affinity CPUIndex          |
|                         |     X2: Reserved — Must be -1      |
|     Outputs:            |     X0: Error Result               |

**Types:**

CPUIndex — a number identifying the target physical CPU.

For hardware platforms with physical CPUs that are linearly numbered from 0, this is equal to the physical CPU number; for AArch64 platforms, this is the case if three of the four affinity fields in `MPIDR_EL1` have a zero value on every physical PE, and the CPUIndex corresponds to the value of the remaining `MPIDR_EL1` affinity field. Otherwise, the hypervisor’s platform driver defines the mapping between CPUIndex values and physical CPUs, and VMs may be informed of this mapping at boot time via the boot environment data.

The value -1 (`CPU_INDEX_INVALID`) may be used to indicate that the VCPU should not have affinity to any physical CPU. If the scheduler does not support automatic migration of threads, this will effectively disable the VCPU, so an additional object right (Thread Disable) is required in this case.

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
|     Inputs:             |     X0:   vCPU CapID                 |
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

| State | Name | State Data 1 | State Data 2 | State Data 3 | Resume Data 1 |
|-|--|--|--|--|--|
| 0x0 | `READY` | 0 | 0 | 0 | 0 |
| 0x1 | `EXPECTS_WAKEUP` | VCPU Sleep Type | 0 | 0 | 0 |
| 0x2 | `POWERED_OFF` | VCPU Poweroff Type | 0 | 0 | 0 |
| 0x3 | `BLOCKED` | 0 | 0 | 0 | 0 |
| 0x4 | `ADDRSPACE_VMMIO_READ` | VMPhysAddr | Size | 0 | Register |
| 0x5 | `ADDRSPACE_VMMIO_WRITE` | VMPhysAddr | Size | Register | 0 |
| 0x100 | `PSCI_SYSTEM_RESET` | PSCI Reset Type | 0 | 0 | 0 |

The Resume Data 2 and 3 arguments are currently unused and must be zero for all states.

0x0 `READY`
:The caller's hypervisor timeslice ended, or the caller received an interrupt. The caller should retry after handling any pending interrupts.

0x1 `EXPECTS_WAKEUP`
:The VCPU is waiting to receive an interrupt; for example, it may have executed a WFI instruction, or made a firmware call requesting entry into a low-power state. In the latter case, the state-specific data in X2 will be a platform-specific nonzero value indicating the requested power state. For a platform that implements Arm's PSCI standard, it is in the same format as the state argument to a `PSCI_CPU_SUSPEND` call. The `VCPU_RUN_WAKEUP` VIRQ will be asserted when the VCPU leaves this state.

0x2 `POWERED_OFF`
:The VCPU has not yet been started by calling `vcpu_poweron`, or has stopped itself by calling `vcpu_poweroff`, or has been terminated due to a reset request from another VM. If PSCI is implemented, this state is also reachable via PSCI calls. The `VCPU_RUN_WAKEUP` VIRQ will be asserted when the VCPU leaves this state. The first state data word contains a VCPU Poweroff Type value (defined below).

0x3 `BLOCKED`
:The VCPU is temporarily unable to run due to a hypervisor operation. This may include a hypercall made by the VCPU that transiently blocks it, or by an incomplete migration from another physical CPU. The caller should retry after yielding to the calling VM's scheduler.

0x4 `ADDRSPACE_VMMIO_READ`
:The VCPU has performed a read access to an unmapped stage 2 address inside a range previously nominated by a call to `addrspace_configure_vmmio`. The first two state data words contain the base IPA and the access size, respectively. The VCPU will be automatically resumed by the next `vcpu_run` call. The first resume data word for that call should be set to the value that will be returned by the read access.

0x5 `ADDRSPACE_VMMIO_WRITE`
:The VCPU has performed a write access to an unmapped stage 2 address inside a range previously nominated by a call to `addrspace_configure_vmmio`. The three state data words contain the base IPA, access size, and the value written by the access, respectively. The VCPU will be automatically resumed by the next `vcpu_run` call.

0x6 `FAULT`
: The VCPU has an unrecoverable fault.

0x100 `PSCI_SYSTEM_RESET`
:On a platform that implements PSCI, the VCPU has made a call to `PSCI_SYSTEM_RESET` or `PSCI_SYSTEM_RESET2`. The first state data word contains a PSCI Reset Type value (defined below). For a `PSCI_SYSTEM_RESET2` call, the second state data word contains the cookie value.

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

|    **Hypercall**:       |      `vpm_group_configure`           |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6066`                     |
|     Inputs:             |     X0: VPMGroup CapID               |
|                         |     X1: VPMGroupOptionFlags          |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*VPMGroupOptionFlags:*

|      Bit Numbers     |      Mask                  |      Description                |
|----------------------|----------------------------|---------------------------------|
|     0                |     `0x1`                  |     Exclude from aggregation    |
|     63:1             |     `0xFFFFFFFF.FFFFFFFE`  |     Reserved — Must be Zero     |

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

## Trace Buffer Management

### Update trace class flags

Update the trace class flags values by specifying which flags to set and clear. Some bits are internal to the hypervisor, so their values passed in this hypercall will be ignored. This call will return the values of the flags after being updated.

|    **Hypercall**:       |      `trace_update_class_flags`      |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x603f`                     |
|     Inputs:             |     X0: SetFlags                     |
|                         |     X1: ClearFlags                   |
|                         |     X2: Reserved — Must be Zero      |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: SetFlags                     |

**Errors:**

OK – the operation was successful, and the result is valid.

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

## Virtual IO MMIO Management

### Configure a Virtual IO Interface Object

Configure a Virtual IO Interface Object whose state is OBJECT_STATE_INIT.

Every Virtual IO device must be attached to a Memory Extent Object that contains its common registers and assumed to be mapped with write permissions into the backend VM's address space. The caller must also bind the backend IRQs to the backend VM's Virtual Interrupt Controller.

The number of queues presented by the device must be set at configuration time, so the hypervisor can allocate memory for tracking the queue states.

The Memory Extent must be 4KiB in size. Its layout matches the register layout specified for MMIO devices in section 4.2.2 of the Virtual I/O Device (VIRTIO) 1.1 specification, followed by optional device-specific configuration starting at offset 0x100. The caller must map it with read-only permissions into the frontend VM's address space, and bind the device's frontend IRQs to the frontend VM's Virtual Interrupt Controller.

If the device type valid flag is set, then the specified device type must be one that is known to the hypervisor, and any appropriate type-specific hypercalls must be made before the device is permitted to exit its reset state. Otherwise, the device type argument is ignored.

|    **Hypercall**:       |      `virtio_mmio_configure`         |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6049`                     |
|     Inputs:             |     X0: VirtioMMIO CapID             |
|                         |     X1: Memextent CapID              |
|                         |     X2: VQsNum Integer               |
|                         |     X3: VirtioOptionFlags            |
|                         |     X4: DeviceType Integer           |
|                         |     X5: DeviceConfigSize Integer     |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*VirtioOptionFlags:*

| Bit Numbers |  Mask                 | Description                             |
|-------------|-----------------------|-----------------------------------------|
| 6           | `0x40`                | Device type argument is valid           |
| 63:7,5:0    | `0xFFFFFFFF.FFFFFFBF` | Reserved — Must be Zero                 |

**Errors:**

OK – the operation was successful.

ERROR_OBJECT_STATE – if the Virtual IO MMIO object is not in OBJECT_STATE_INIT state.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to VQsNum being larger than the maximum, or the specified Memory Extent object being of an unsupported type.

Also see: [Capability Errors](#capability-errors)

### Virtual IO MMIO Backend vIRQ Bind

Binds a Virtual IO MMIO backend interface to a virtual interrupt.

|    **Hypercall**:       |      `virtio_mmio_bind_backend_virq`   |
|-------------------------|----------------------------------------|
|     Call number:        |     `hvc 0x604a`                       |
|     Inputs:             |     X0: VirtioMMIO CapID               |
|                         |     X1: Virtual IC CapID               |
|                         |     X2: Virtual IRQ Info               |
|                         |     X3: Reserved — Must be Zero        |
|     Outputs:            |     X0: Error Result                   |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_VIRQ_BOUND – the specified virtual IO MMIO is already bound to a VIRQ number.

ERROR_BUSY – the specified VIRQ number is already bound to a source.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Virtual IRQ Info value.

Also see: [Capability Errors](#capability-errors)

### Virtual IO MMIO Backend vIRQ Unbind

Unbinds a Virtual IO MMIO backend interface from a virtual IRQ number.

|    **Hypercall**:       |      `virtio_mmio_unbind_backend_virq`   |
|-------------------------|------------------------------------------|
|     Call number:        |     `hvc 0x604b`                         |
|     Inputs:             |     X0: VirtioMMIO CapID                 |
|                         |     X1: Reserved — Must be Zero          |
|     Outputs:            |     X0: Error Result                     |

**Errors:**

OK – the operation was successful, or the virtual IO MMIO interrupt was already unbound.

Also see: [Capability Errors](#capability-errors)

### Virtual IO MMIO Frontend vIRQ Bind

Binds a Virtual IO MMIO frontend interface to a virtual interrupt.

|    **Hypercall**:       |      `virtio_mmio_bind_frontend_virq`   |
|-------------------------|-----------------------------------------|
|     Call number:        |     `hvc 0x604c`                        |
|     Inputs:             |     X0: VirtioMMIO CapID                |
|                         |     X1: Virtual IC CapID                |
|                         |     X2: Virtual IRQ Info                |
|                         |     X3: Reserved — Must be Zero         |
|     Outputs:            |     X0: Error Result                    |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_VIRQ_BOUND – the specified virtual IO MMIO is already bound to a VIRQ number.

ERROR_BUSY – the specified VIRQ number is already bound to a source.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Virtual IRQ Info value.

Also see: [Capability Errors](#capability-errors)

### Virtual IO MMIO Frontend vIRQ Unbind

Unbinds a Virtual IO MMIO backend interface from a virtual IRQ number.

|    **Hypercall**:       |      `virtio_mmio_unbind_frontend_virq`   |
|-------------------------|-------------------------------------------|
|     Call number:        |     `hvc 0x604d`                          |
|     Inputs:             |     X0: VirtioMMIO CapID                  |
|                         |     X1: Reserved — Must be Zero           |
|     Outputs:            |     X0: Error Result                      |

**Errors:**

OK – the operation was successful, or the virtual IO MMIO interrupt was already unbound.

Also see: [Capability Errors](#capability-errors)

### Virtual IO MMIO Backend Assert vIRQ

The backend makes this call to assert the virtual IRQ directed to the frontend and writes a bit mask of events that caused the assertion.

|    **Hypercall**:       |      `virtio_mmio_backend_assert_virq`   |
|-------------------------|------------------------------------------|
|     Call number:        |     `hvc 0x604e`                         |
|     Inputs:             |     X0: VirtioMMIO CapID                 |
|                         |     X1: InterruptStatus                  |
|                         |     X2: Reserved — Must be Zero          |
|     Outputs:            |     X0: Error Result                     |

**Errors:**

OK – the operation was successful, or the virtual IO MMIO interrupt was already unbound.

ERROR_DENIED – Cannot assert irq since there is a reset currently pending.

ERROR_ARGUMENT_INVALID – A value passed in an argument was invalid.

Also see: [Capability Errors](#capability-errors)

### Virtual IO MMIO Backend Set DeviceFeatures

Set the device features flags based on the specified device features selector. The device features specified must comply with the features enforced by the hypervisor (VIRTIO_F_VERSION_1, VIRTIO_F_ACCESS_PLATFORM, !VIRTIO_F_NOTIFICATION_DATA).

|    **Hypercall**:       |      `virtio_mmio_backend_set_dev_features`   |
|-------------------------|-----------------------------------------------|
|     Call number:        |     `hvc 0x604f`                              |
|     Inputs:             |     X0: VirtioMMIO CapID                      |
|                         |     X1: DeviceFeaturesSel                     |
|                         |     X2: DeviceFeatures                        |
|                         |     X3: Reserved — Must be Zero               |
|     Outputs:            |     X0: Error Result                          |

**Errors:**

OK – The operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – A value passed in an argument was invalid.

ERROR_DENIED – Device features passed do not comply with the features enforced by the hypervisor.

Also see: [Capability Errors](#capability-errors)

### Virtual IO MMIO Backend Set QueueNumMax

Set maximum virtual queue size of the queue specified by the queue selector.

|    **Hypercall**:       |      `virtio_mmio_backend_set_queue_num_max`   |
|-------------------------|------------------------------------------------|
|     Call number:        |     `hvc 0x6050`                               |
|     Inputs:             |     X0: VirtioMMIO CapID                       |
|                         |     X1: QueueSel                               |
|                         |     X2: QueueNumMax                            |
|                         |     X3: Reserved — Must be Zero                |
|     Outputs:            |     X0: Error Result                           |

**Errors:**

OK – The operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – A value passed in an argument was invalid.

Also see: [Capability Errors](#capability-errors)

### Virtual IO MMIO Backend Get DriverFeatures

Get the driver features flags based on the specified driver features selector.

|    **Hypercall**:       |      `virtio_mmio_backend_get_drv_features`   |
|-------------------------|-----------------------------------------------|
|     Call number:        |     `hvc 0x6051`                              |
|     Inputs:             |     X0: VirtioMMIO CapID                      |
|                         |     X1: DriverFeaturesSel                     |
|                         |     X2: Reserved — Must be Zero               |
|     Outputs:            |     X0: Error Result                          |
|                         |     X1: DriverFeatures                        |

**Errors:**

OK – The operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – A value passed in an argument was invalid.

Also see: [Capability Errors](#capability-errors)

### Virtual IO MMIO Backend Get Queue Info

Get information from the queue specified by the queue selector.

|    **Hypercall**:       |      `virtio_mmio_backend_get_queue_info`   |
|-------------------------|---------------------------------------------|
|     Call number:        |     `hvc 0x6052`                            |
|     Inputs:             |     X0: VirtioMMIO CapID                    |
|                         |     X1: QueueSel                            |
|                         |     X2: Reserved — Must be Zero             |
|     Outputs:            |     X0: Error Result                        |
|                         |     X1: QueueNum                            |
|                         |     X2: QueueReady                          |
|                         |     X3: QueueDesc (low and high)            |
|                         |     X4: QueueDriver (low and high)          |
|                         |     X5: QueueDevice (low and high)          |

**Errors:**

OK – The operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – A value passed in an argument was invalid.

Also see: [Capability Errors](#capability-errors)

### Virtual IO MMIO Backend Get Notification

The backend should make this call, when its VIRQ is asserted, to get a bitmap of the virtual queues that need to be notified and a bitmap of the reasons why the VIRQ was asserted. This calls also deasserts the backend’s VIRQ.

|    **Hypercall**:       |      `virtio_mmio_backend_get_notification`   |
|-------------------------|-----------------------------------------------|
|     Call number:        |     `hvc 0x6053`                              |
|     Inputs:             |     X0: VirtioMMIO CapID                      |
|                         |     X1: Reserved — Must be Zero               |
|     Outputs:            |     X0: Error Result                          |
|                         |     X1: VQs Bitmap                            |
|                         |     X2: NotifyReason Bitmap                   |

**Types:**

*NotifyReason:*

| Bits | Mask | Description |
|-|---|-----|
|     0                |     `0x1`                  |     1 = NEW_BUFFER: notifies the device that there are new buffers to process in a queue.                       |
|     1                |     `0x2`                  |     1 = RESET_RQST: notifies the device that a device reset has been requested.                                 |
|     3                |     `0x8`                  |     1 = DRIVER_OK: notifies the device that the frontend has set the DRIVER_OK bit of the Status register.      |
|     4                |     `0x10`                 |     1 = FAILED: notifies the device that the frontend has set the FAILED bit of the Status register.            |
|     63:5,2           |     `0xFFFFFFFF.FFFFFFE4`  |     Reserved = 0 [TBD notify reasons]                                                                           |

**Errors:**

OK – The operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – A value passed in an argument was invalid.

Also see: [Capability Errors](#capability-errors)

### Virtual IO MMIO Backend Acknowledge Reset

The backend should make this call after a device reset is completed. This call will clear all bits in QueueReady for all queues in the device.

|    **Hypercall**:       |      `virtio_mmio_backend_acknowledge_reset`   |
|-------------------------|------------------------------------------------|
|     Call number:        |     `hvc 0x6054`                               |
|     Inputs:             |     X0: VirtioMMIO CapID                       |
|                         |     X1: Reserved — Must be Zero                |
|     Outputs:            |     X0: Error Result                           |

**Errors:**

OK – The operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – A value passed in an argument was invalid.

Also see: [Capability Errors](#capability-errors)

### Virtual IO MMIO Backend Set Status

This calls sets status register.

|    **Hypercall**:       |      `virtio_mmio_backend_set_status`   |
|-------------------------|-----------------------------------------|
|     Call number:        |     `hvc 0x6055`                        |
|     Inputs:             |     X0: VirtioMMIO CapID                |
|                         |     X1: Status                          |
|                         |     X2: Reserved — Must be Zero         |
|     Outputs:            |     X0: Error Result                    |

**Errors:**

OK – The operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – A value passed in an argument was invalid.

Also see: [Capability Errors](#capability-errors)

## Virtio Input Config Hypercalls

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
