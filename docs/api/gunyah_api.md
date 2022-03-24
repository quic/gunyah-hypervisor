# Gunyah Hypercall API

1. [AARCH64 HVC ABI](#aarch64-hvc-abi) <br>
1. [Common Types](#common-types) <br>
1. [Object Rights](#object-rights) <br>
1. [Hypervisor Identification](#hypervisor-identification) <br>
1. [Partitions](#partitions)
1. [Object Management](#object-management)
1. [Communication APIs](#communication-apis) <br>
	7.1 [Doorbell Management](#doorbell-management) <br>
	7.2 [Message Queue Management](#message-queue-management) <br>
1. [Capability Management](#capability-management)
1. [Interrupt Management](#interrupt-management)
1. [Address Space Management](#address-space-management)
1. [Memory Extent Management](#memory-extent-management)
1. [VCPU Management](#vcpu-management)
1. [Scheduler Management](#scheduler-management)
1. [Virtual PM Group Management](#virtual-pm-group-management)
1. [Trace Buffer Management](#trace-buffer-management)
1. [Watchdog Management](#watchdog-management)
1. [Error Results](#error-results)

## AARCH64 HVC ABI

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

|      Register                |      Role in AAPCS64                                                                                                                                                 |      Role in Gunyah HVC                                                                  |
|------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------|
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

### Virtual IRQ Info

A bitfield type that identifies a virtual IRQ within a virtual interrupt controller.

*Virtual IRQ Info:*

|      Bit Numbers     |      Mask                  |      Description                                                                                                                                                                                                                                                                                                                                            |
|----------------------|----------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
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

### Memory Extent Rights

| Right             |  Value            |
|-------------------|-------------------|
| Memory Extent Map    | `0x00000001` |
| Memory Extent Derive | `0x00000002` |
| Memory Extent Attach | `0x00000004` |

### Thread Rights

| Right             |  Value            |
|-------------------|-------------------|
| Thread Power On/Off  | `0x00000001` |
| Thread Set Affinity  | `0x00000002` |
| Thread Set Priority  | `0x00000004` |
| Thread Set Timeslice | `0x00000008` |
| Thread Yield To      | `0x00000010` |
| Thread Lifecycle     | `0x00000080` |

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

| Bit Numbers     | Mask                   | Description                                                   |
|-----------------|------------------------|---------------------------------------------------------------|
| 13:0            | `0x00001FFF`           | API Version = “1”                                             |
| 14              | `0x00004000`           | 0 = API is Little Endian.   <br>1 = API is Big Endian.        |
| 15              | `0x00008000`           | If set to 1, the API is 64-bit, otherwise 32-bit.             |
| 55:16           | `0xFFFFFF.FFFF0000`    | Reserved                                                      |
| 63:56           | `0xFF000000.00000000`  | Hypervisor   variant.<br>- Unknown = 0x0<br>- Haven = 0x48    |

*API Flags 0:*

|      Bit Numbers     |      Mask                  |      Description                                                         |
|----------------------|----------------------------|--------------------------------------------------------------------------|
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

|      Bit Numbers     |      Mask                  |      Description                |
|----------------------|----------------------------|---------------------------------|
|     0                |     `0x1`                  |     1 = ARM v8.2 SVE support    |
|     63:1             |     `0xFFFFFFFF.FFFFFFFF ` |     Reserved = 0                |

*API Flags 2:*

|      Bit Numbers     |      Mask                  |      Description     |
|----------------------|----------------------------|----------------------|
|     63:0             |     `0xFFFFFFFF.FFFFFFFF`  |     Reserved = 0     |


## Partitions

### Partition Object Creation

Allocates a new Partition object from the Partition and allocates a Capability ID from the CSpace.

|    **Hypercall**:       |      `partition_create_partition`    |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6001`                     |
|     Inputs:             |     X0: Partition CapID              |
|                         |     X1: CSpace CapID                 |
|                         |     X2: Reserved   – Must be Zero    |
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
|                         |     X2: Reserved   – Must be Zero    |
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
|                         |     X2: Reserved   – Must be Zero    |
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
|                         |     X2: Reserved   – Must be Zero    |
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
|                         |     X2: Reserved   – Must be Zero    |
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
|                         |     X2: Reserved   – Must be Zero    |
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
|                         |     X2: Reserved   – Must be Zero      |
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
|                         |     X2: Reserved   – Must be Zero    |
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
|                         |     X2: Reserved   – Must be Zero    |
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
|                         |     X2: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: VPMGroup CapID               |

On successful creation, the new Virtual PM Group object is created and its state is OBJECT_STATE_INIT.

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
|                         |     X1: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_OBJECT_STATE - if the object is not in OBJECT_STATE_INIT state.

Additional [error codes](#error-code-enumeration) can be returned depending on the type of object to be activated.

Also see: [Capability Errors](#capability-errors)

### Activate an Object from a CSpace

Activate an object from a Cspace.

|    **Hypercall**:       |      `object_activate_from`          |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x600D`                     |
|     Inputs:             |     X0: CSpace CapID                 |
|                         |     X1: Cap CapID                    |
|                         |     X2: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_OBJECT_STATE - if the object is not in OBJECT_STATE_INIT state.

Additional [error codes](#error-code-enumeration) can be returned depending on the type of object to be activated.

Also see: [Capability Errors](#capability-errors)

### Reset an Object

Reset an object to its initial state.

|    **Hypercall**:       |      `object_reset`                  |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x600E`                     |
|     Inputs:             |     X0: Cap CapID                    |
|                         |     X1: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_UNIMPLEMENTED - if functionality not implemented.

Additional [error codes](#error-code-enumeration) can be returned depending on the type of object to be reset.

Also see: [Capability Errors](#capability-errors)

### Reset an Object from a CSpace

Reset an object from a Cspace to its initial state.

|    **Hypercall**:       |      `object_reset_from`             |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x600F`                     |
|     Inputs:             |     X0: CSpace CapID                 |
|                         |     X1: Cap CapID                    |
|                         |     X2: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_UNIMPLEMENTED - if functionality not implemented.

Additional [error codes](#error-code-enumeration) can be returned depending on the type of object to be reset.

Also see: [Capability Errors](#capability-errors)

### TBD object create / Partition

<!-- TODO: -->

0x6008 - `Reserved`

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
|                         |     X3: Reserved   – Must be Zero    |
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
|                         |     X1: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, or the Doorbell’s interrupt was already unbound.

Also see: [Capability Errors](#capability-errors)

#### Doorbell Send

Sets flags in the Doorbell. If following the send, the set of enabled flags as defined by the bitwise-AND of the DoorBell flags with the EnableMask, is non-zero, any bound virtual interrupt will be asserted.

|    **Hypercall**:       |      `doorbell_send`                              |
|-------------------------|---------------------------------------------------|
|     Call number:        |     `hvc 0x6012`                                  |
|     Inputs:             |     X0: Doorbell CapID                            |
|                         |     X1: NewFlags FlagsBitmap  – Must be non-zero. |
|                         |     X2: Reserved   – Must be Zero                 |
|     Outputs:            |     X0: Error Result                              |
|                         |     X1: OldFlags FlagsBitmap                      |

The returned OldFlags result contains the Doorbell’s previous unmasked flags before the NewFlags were added.

**Types:**

FlagsBitmap: unsigned 64-bit bitmap

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – if a zero NewFlags value is passed in.

Also see: [Capability Errors](#capability-errors)

#### Doorbell Receive

Reads and clears the flags of the Doorbell. If there is a pending bound virtual interrupt, it will be de-asserted.

|    **Hypercall**:       |      `doorbell_receive`                               |
|-------------------------|-------------------------------------------------------|
|     Call number:        |     `hvc 0x6013`                                      |
|     Inputs:             |     X0: Doorbell CapID                                |
|                         |     X1: ClearFlags FlagsBitmap - Must be non-zero.    |
|                         |     X2: Reserved   – Must be Zero                     |
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
|                         |     X1: Reserved   – Must be Zero    |
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
|                         |     X3: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Types:**

FlagsBitmap: unsigned 64-bit bitmap of Boolean flags.

**Errors:**

OK – the operation was successful, and the result is valid.

Also see: [Capability Errors](#capability-errors)

#### Doorbell Halt

<!-- TODO: -->

0x6016 - `Reserved`

### Message Queue Management

#### Message Queue Bind Send vIRQ

Binds a Message Queue send interface to a virtual IRQ number.

|    **Hypercall**:       |      `msgqueue_bind_send_virq`       |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6017`                     |
|     Inputs:             |     X0: Message Queue CapID          |
|                         |     X1: Virtual IC CapID             |
|                         |     X2: Virtual IRQ Info             |
|                         |     X3: Reserved   – Must be Zero    |
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
|                         |     X3: Reserved   – Must be Zero    |
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
|                         |     X1: Reserved   – Must be Zero    |
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
|                         |     X1: Reserved   – Must be Zero    |
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
|                         |     X1: Size Size     Must be non-zero.    |
|                         |     X2: Data VMAddr                        |
|                         |     X3: MsgQSendFlags                      |
|                         |     X4: Reserved   – Must be Zero          |
|     Outputs:            |     X0: Error Result                       |
|                         |     X1: NotFull Boolean                    |

**Types:**

*MsgQSendFlags:*

|      Bit Numbers     |      Mask                  |      Description                |
|----------------------|----------------------------|---------------------------------|
|     0                |     `0x1`                  |     Message Push                |
|     63:1             |     `0xFFFFFFFF.FFFFFFFE`  |     Reserved,   Must be Zero    |

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
|                         |     X3: Reserved   – Must be Zero    |
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
|                         |     X1: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful.

Also see: [Capability Errors](#capability-errors)

#### Messsage Queue Halt

<!-- TODO: -->

0x601E - `Reserved`

#### Message Queue Configure Send

Modify configuration of a Message Queue send interface. The interface allows for configuring of a Message Queue, including setting interrupt thresholds and timeouts.

|    **Hypercall**:       |      `msgqueue_configure_send`                    |
|-------------------------|---------------------------------------------------|
|     Call number:        |     `hvc 0x601F`                                  |
|     Inputs:             |     X0: Message Queue CapID                       |
|                         |     X1: NotFull interrupt threshold               |
|                         |     X2: NotFull threshold delay (microseconds)    |
|                         |     X3: Reserved   – Must be -1                   |
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
|                         |     X3: Reserved   – Must be -1                    |
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
|                         |     X2: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*MessageQueueCreateInfo:*

|      Bit Numbers     |      Mask                  |      Description                |
|----------------------|----------------------------|---------------------------------|
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
|                         |     X2: Reserved   – Must be Zero    |
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
|                         |     X4: Reserved   – Must be Zero    |
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
|                         |     X2: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_UNIMPLEMENTED - if functionality not implemented.

`TODO: TBD. Currently unimplemented`

Also see: [Capability Errors](#capability-errors)

### Configure a CSpace

Configure a CSpace whose state is OBJECT_STATE_INIT.

|    **Hypercall**:       |      `cspace_configure`              |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6025`                     |
|     Inputs:             |     X0: CSpace CapID                 |
|                         |     X1: MaxCaps                      |
|                         |     X2: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_OBJECT_STATE – if the Cspace is not in OBJECT_STATE_INIT state.

ERROR_ARGUMENT_INVALID - a value passed in an argument was invalid. This could be due to an invalid Max Caps value.

Also see: [Capability Errors](#capability-errors)

### CSpace to Thread Attachment

Configure a CSpace whose state is OBJECT_STATE_INIT.

Attaches a thread to a CSpace. The Cspace object must have been activated before this function is called. The thread object must not have been activated.

|    **Hypercall**:       |      `cspace_attach_thread`          |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x603e`                     |
|     Inputs:             |     X0: CSpace CapID                 |
|                         |     X1: Thread CapID                 |
|                         |     X2: Reserved   – Must be Zero    |
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
|                         |     X2: Reserved   – Must be Zero    |
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
|                         |     X3: Reserved   – Must be Zero    |
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
|                         |     X1: Reserved   – Must be Zero    |
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
|                         |     X3: Reserved   – Must be Zero    |
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
|                         |     X3: Reserved   – Must be Zero    |
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
|                         |     X3: Reserved   – Must be Zero                             |
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
|                         |     X2: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to a thread of kind different from VCPU.

ERROR_OBJECT_STATE – The Thread object has already been activated, or the Address Space object has not yet been activated.

Also see: [capability errors](#capability-errors)

### Address Space Map

Maps a memory extent into a specified address space. The entire memory extent range is mapped, except for any carveouts contained within the extent.

|    **Hypercall**:       |      `addrspace_map`                 |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x602b`                     |
|     Inputs:             |     X0: Address Space CapID          |
|                         |     X1: Memory Extent CapID          |
|                         |     X2: Base VMAddr                  |
|                         |     X3: Map Attributes               |
|                         |     X4:   Reserved – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*Map Atrributes:*

|      Bit Numbers     |      Mask                  |      Description                  |
|----------------------|----------------------------|-----------------------------------|
|     2..0             |     `0x7`                  |     User Access (if Supported)    |
|     6..4             |     `0x70`                 |     Kernel Access                 |
|     23:16            |     `0xFF0000`             |     Memory Type                   |
|     63:24,15:7,3     |     `0xFFFFFFFF.0000FF88`  |     Reserved,   Must be Zero      |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Address Space.

ERROR_MEMEXTENT_MAPPINGS_FULL– the memory extent has exceeded its mappings capacity. Currently it can have up to 4 mappings.

ERROR_DENIED - the specified Address Space is not allowed to execute map operations.

ERROR_ARGUMENT_ALIGNMENT - the specificied base address is not page size aligned.

ERROR_ADDR_OVERFLOW - the specified base address may cause an overflow.

Also see: [capability errors](#capability-errors)

### Address Space Unmap

Unmaps a memory extent from a specified address space. The entire memory extent range is unmapped, except for any carveouts contained within the extent.

|    **Hypercall**:       |      `addrspace_unmap`               |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x602c`                     |
|     Inputs:             |     X0: Address Space CapID          |
|                         |     X1: Memory Extent CapID          |
|                         |     X2: Base VMAddr                  |
|                         |     X3: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Address Space or a non-existing mapping.

ERROR_DENIED - the specified Address Space is not allowed to execute map operations.

ERROR_ARGUMENT_ALIGNMENT - the specificied base address is not page size aligned.

Also see: [capability errors](#capability-errors)

### Address Space Update Access

Update access rights on an existing mapping.

|    **Hypercall**:       |      `addrspace_update_access`       |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x602d`                     |
|     Inputs:             |     X0: Address Space CapID          |
|                         |     X1: Memory Extent CapID          |
|                         |     X2: Base VMAddr                  |
|                         |     X3:   Update Attributes          |
|                         |     X5:   Reserved – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*Update Attributes:*

|      Bit Numbers     |      Mask                  |      Description                  |
|----------------------|----------------------------|-----------------------------------|
|     2..0             |     `0x7`                  |     User Access (if Supported)    |
|     6..4             |     `0x70`                 |     Kernel Access                 |
|     63:7,3           |     `0xFFFFFFFF.FFFFFF88`  |     Reserved,   Must be Zero      |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Address Space or a non-existing mapping.

ERROR_ARGUMENT_ALIGNMENT - the specificied base address is not page size aligned.

ERROR_DENIED - the specified Address Space is not allowed to update access of mappings.

Also see: [capability errors](#capability-errors)

### Configure an Address Space

Configure a address space whose state is OBJECT_STATE_INIT.

|    **Hypercall**:       |      `addrspace_configure`           |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x602e`                     |
|     Inputs:             |     X0: Address Space CapID          |
|                         |     X1: VMID                         |
|                         |     X2: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Types:**

16-Bit VMID, upper bits reserved and must be zero.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_OBJECT_STATE – the Address Space object has already been activated.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid VMID.

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
|                         |     X3: Reserved   – Must be Zero           |
|     Outputs:            |     X0: Error Result                        |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_ARGUMENT_INVALID – the specified object is virtual DMA capable, but the port index is outside the valid range for the object.

ERROR_BUSY – the specified port already has an address space attached, and the object does not support changing an existing attachment.

ERROR_OBJECT_STATE – the Address Space object has not yet been activated.

Also see: [capability errors](#capability-errors)

## Memory Extent Management

### Memory Extent Unmap All

Unmaps a memory extent from all address spaces it was mapped into. The entire range is unmapped, except for any carveouts contained within the extent.

|    **Hypercall**:       |      `memextent_unmap_all`           |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6030`                     |
|     Inputs:             |     X0: Memory Extent CapID          |
|                         |     X1: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

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
|                         |     X4: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*MemExtent Attributes:*

|      Bit Numbers     |      Mask         |      Description                |
|----------------------|-------------------|---------------------------------|
|     2..0             |     `0x7`         |     Access Rights               |
|     9:8              |     `0x300`       |     MemExtent MemType           |
|     31               |     `0x80000000`  |     List Append                 |
|     30:19,7:3        |     `0x7FFFFCF8`  |     Reserved,   Must be Zero    |

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
|                         |     X5: Reserved   – Must be Zero     |
|     Outputs:            |     X0: Error Result                  |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid size or offset.

Also see: [Capability Errors](#capability-errors)

### Memory Extent Reserved

<!-- TODO: -->

0x6033 - `Reserved`

## VCPU Management

### Configure a VCPU Thread

Configure a VCPU Thread whose state is OBJECT_STATE_INIT.

|    **Hypercall**:       |      `vcpu_configure`                |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6034`                     |
|     Inputs:             |     X0:   vCPU CapID                 |
|                         |     X1: vCPUOptionFlags              |
|                         |     X2: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*vCPUOptionFlags:*

|      Bit Numbers     |      Mask                  |      Description                        |
|----------------------|----------------------------|-----------------------------------------|
|     0                |     `0x1`                  |     AArch64 Self-hosted Debug Enable    |
|     1                |     `0x2`                  |     VCPU containing HLOS VM             |
|     63:2             |     `0xFFFFFFFF.FFFFFFFE`  |     Reserved,   Must be Zero            |

AArch64 Self-hosted Debug: give the VCPU access to use AArch64 Self-hosted debug functionality and registers.

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_OBJECT_STATE – if the VCPU object is not in OBJECT_STATE_INIT state.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid VCPU or option flags.

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
|                         |     X2: Reserved   – Must be -1    |
|     Outputs:            |     X0: Error Result               |

**Types:**

CPUIndex — a number identifying the target physical CPU. For hardware platforms with physical CPUs that are linearly numbered from 0, this is equal to the physical CPU number; for AArch64 platforms, this is the case if three of the four affinity fields in MPIDR_EL1 have a zero value on every physical PE, and the CPUIndex corresponds to the value of the remaining MPIDR_EL1 affinity field. Otherwise, the hypervisor’s platform driver defines the mapping between CPUIndex values and physical CPUs, and VMs may be informed of this mapping at boot time via the boot environment data. If the scheduler supports directed yields and/or automatic migration of threads, the value -1 may be used to indicate that the VCPU should not have affinity to any physical CPU.

**Errors:**

OK – the operation was successful.

ERROR_OBJECT_STATE – the specified VCPU thread is active and the scheduler does not support migration of active threads.

ERROR_ARGUMENT_INVALID – the affinity value specified is out of range.

ERROR_DENIED – the specified VCPU is not permitted to change affinity because a physical-CPU-local resource, such as a private interrupt, has been assigned to it.

Also see: [Capability Errors](#capability-errors)

### Power on a VCPU Thread

Set a VCPU Thread’s initial execution state, including its entry point and context.

|    **Hypercall**:       |      `vcpu_poweron`                  |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6038`                     |
|     Inputs:             |     X0:   vCPU CapID                 |
|                         |     X1: vCPU EntryPointAddr          |
|                         |     X2: vCPU Context                 |
|                         |     X3: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid VCPU.

ERROR_BUSY - the specified VCPU is currently busy and cannot be powered on at the moment.

Also see: [Capability Errors](#capability-errors)

### Power off a VCPU Thread

Tear down the current thread’s VCPU execution state. This call will not return when successful.

|    **Hypercall**:       |      `vcpu_poweroff`                 |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x6039`                     |
|     Inputs:             |     X0:   vCPU CapID                 |
|                         |     X1: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid VCPU.

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

Binds a VCPU interface to a virtual interrupt.

|    **Hypercall**:       |      `vcpu_bind_virq`                |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x605c`                     |
|     Inputs:             |     X0: VCPU CapID                   |
|                         |     X1: Virtual IC CapID             |
|                         |     X2: Virtual IRQ Info             |
|                         |     X3: VCPU Virtual IRQ Type        |
|                         |     X4: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Types:**

|      VCPU Virtual IRQ Type     |      Integer Value     |
|--------------------------------|------------------------|
|     HALT                       |     0                  |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_NOMEM – the operation failed due to memory allocation error.

ERROR_VIRQ_BOUND – the specified VCPU is already bound to a VIRQ number.

ERROR_BUSY – the specified VIRQ number is already bound to a source.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid Virtual IRQ Info value.

Also see: [Capability Errors](#capability-errors)

### VCPU vIRQ Unbind

Unbinds a VCPU interface from a virtual IRQ number.

|    **Hypercall**:       |      `vcpu_unbind_virq`              |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x605d`                     |
|     Inputs:             |     X0: VCPU CapID                   |
|                         |     X1: VCPU Virtual IRQ Type        |
|                         |     X2: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful, or the VCPU interrupt was already unbound.

Also see: [Capability Errors](#capability-errors)

### VCPU Write State

It updates the VCPU information related to the VCPUState specified.

|    **Hypercall**:       |      `vcpu_write_state`                    |
|-------------------------|--------------------------------------------|
|     Call number:        |     `hvc 0x605e`                           |
|     Inputs:             |     X0: VCPU CapID                         |
|                         |     X1: VCPUState                          |
|                         |     X2: Data VMAddr                        |
|                         |     X3: Size Size     Must be non-zero.    |
|                         |     X4: Reserved   – Must be Zero          |
|     Outputs:            |     X0: Error Result                       |


**Types:**

*VCPUState:*

|      VCPUState     |      Integer Value     |
|--------------------|------------------------|
|     HALT           |     0                  |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_UNIMPLEMENTED - if functionality not implemented.

`TODO: TBD. Currently unimplemented`

Also see: [Capability Errors](#capability-errors)

### VCPU Read State

It fetches the VCPU information related to the VCPUState specified.

|    **Hypercall**:       |      `vcpu_read_state`               |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x605f`                     |
|     Inputs:             |     X0: VCPU CapID                   |
|                         |     X1: VCPUState                    |
|                         |     X2: Buffer VMAddr                |
|                         |     X3: MaximumSize Size             |
|                         |     X4: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |
|                         |     X1: Size Size                    |

Size: is the number of bytes received.

**Errors:**

OK – the operation was successful.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid VCPU State value.

ERROR_DENIED – VCPU State passed does not comply with current VCPU state.

ERROR_ARGUMENT_SIZE – the MaximumSize provided is smaller than the information to be fetched.

ERROR_ADDR_OVERFLOW – the information to be fetched is larger than the provided buffer, and could not be received.

ERROR_ADDR_INVALID – some, or the whole of the provided buffer is not mapped.

Also see: [Capability Errors](#capability-errors)

### Kill a VCPU thread

Places the VCPU thread in a killed state, forcing it to exit and end execution. The VCPU can no longer be scheduled once it has exited. If the calling VCPU is targeting itself, this call will not return if successful.

|    **Hypercall**:       |      `vcpu_kill`                     |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x603a`                     |
|     Inputs:             |     X0: VCPU CapID                   |
|                         |     X1: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Errors:**

OK – the operation was successful.

ERROR_ARGUMENT_INVALID – a value passed in an argument was invalid. This could be due to an invalid VCPU.

ERROR_OBJECT_STATE – the VCPU thread was not active, or has already been killed.

Also see: [Capability Errors](#capability-errors)

## Scheduler Management

### Scheduler Yield

Attaches a VCPU to a Virtual PM Group. The Virtual PM Group object must have been activated before this function is called. The VCPU object must not have been activated. An attachment index must be specified which must be a non-negative integer less than the maximum number of attachments supported by this Virtual PM Group object.

|    **Hypercall**:       |      `scheduler_yield`               |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x603b`                     |
|     Inputs:             |     X0: control                      |
|                         |     X1: arg1                         |
|                         |     X2: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

*Control:*

|      Bit Numbers     |      Mask         |      Description                                                                                                                       |
|----------------------|-------------------|----------------------------------------------------------------------------------------------------------------------------------------|
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

A Virtual PM Group is a collection of VCPUs which share a virtual power management state. This state may be accessible via a virtualised platform-specific interface; on AArch64 this is the ARM PSCI (Platform State Configuration Interface) API. Attachment to this object type is optional for VCPUs in single-processor VMs that do not participate in power management decisions.

### Virtual PM Group to VCPU Attachment

Attaches a VCPU to a Virtual PM Group. The Virtual PM Group object must have been activated before this function is called. The VCPU object must not have been activated. An attachment index must be specified which must be a non-negative integer less than the maximum number of attachments supported by this Virtual PM Group object.

|    **Hypercall**:       |      `vpm_group_attach_vcpu`         |
|-------------------------|--------------------------------------|
|     Call number:        |     `hvc 0x603c`                     |
|     Inputs:             |     X0: VPMGroup CapID               |
|                         |     X1: VCPU CapID                   |
|                         |     X2: Index                        |
|                         |     X3: Reserved   – Must be Zero    |
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
|                         |     X3: Reserved   – Must be Zero    |
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
|                         |     X1: Reserved   – Must be Zero    |
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
|                         |     X1: Reserved   – Must be Zero    |
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
|                         |     X2: Reserved   – Must be Zero    |
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
|                         |     X2: Reserved   – Must be Zero    |
|     Outputs:            |     X0: Error Result                 |

**Types:**

*WatchdogOptionFlags:*

|      Bit Numbers     |      Mask                  |      Description                |
|----------------------|----------------------------|---------------------------------|
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
|                         |     X2: Reserved   – Must be Zero    |
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

|      Bit Numbers     |      Mask                  |      Description                       |
|----------------------|----------------------------|----------------------------------------|
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

|      Bit Numbers     |      Mask                  |      Description                       |
|----------------------|----------------------------|----------------------------------------|
|     0                |     `0x1`                  |     Bite virq (If unset, bark virq)    |
|     63:1             |     `0xFFFFFFFF.FFFFFFFE`  |     Reserved,   Must be Zero           |

Bite virq: If set to 0x1, this flag indicates that it unbinds the bite virq, otherwise it unbinds the bark virq.

**Errors:**

OK – the operation was successful, or the watchdog interrupt was already unbound.

Also see: [Capability Errors](#capability-errors)

## PRNG Management

### PRNG Get Entropy

Gets random numbers from a DRBG that is seeded by a TRNG. Typically this API will source randomness from a NIST/FIPS compliant hardware device.

|    **Hypercall**:   |  `prng_get_entropy`            |
|---------------------|--------------------------------|
|     Call number:    |  `hvc 0x6057`                  |
|     Inputs:         |  X0: NumBytes                  |
|                     |  X1: Reserved   – Must be Zero |
|     Outputs:        |  X0: Error Result              |
|                     |  X1: Data0                     |
|                     |  X2: Data1                     |
|                     |  X3: Data2                     |
|                     |  X4: Data3                     |

**Errors:**

OK – the operation was successful, and the result is valid.

ERROR_ARGUMENT_SIZE – the NumBytes provided is zero, or exceeds the possible bytes to be returned in the Data output registers.

ERROR_BUSY - Called within the read rate-limit window.

ERROR_UNIMPLEMENTED - if functionality not implemented.

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
|     ERROR_MEMEXTENT_MAPPINGS_FULL       |     120                |
|     ERROR_EXISTING_MAPPING              |     200                |

### Capability Errors

ERROR_CSPACE_CAP_NULL - invalid CapID.

ERROR_CSPACE_CAP_REVOKED - CapID no longer valid since it has already been revoked.

ERROR_CSPACE_WRONG_OBJECT_TYPE - CapID does not correspond with the specified object type.

ERROR_CSPACE_INSUFFICIENT_RIGHTS - CapID has not enough rights to execute operation.

ERROR_CSPACE_FULL - CSpace has reached maximum number of capabilities allowed.
