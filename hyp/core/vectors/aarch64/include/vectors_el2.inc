// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Size of the per-CPU memory reserved for nested fault context buffer and stack
#if !defined(NDEBUG)
#define NESTED_FAULT_STACK_SIZE_BITS 10		// 1024 bytes
#else
#define NESTED_FAULT_STACK_SIZE_BITS 9		// 512 bytes
#endif
#define NESTED_FAULT_STACK_SIZE (1 << NESTED_FAULT_STACK_SIZE_BITS)

#if (((OFS_KERNEL_TRAP_FRAME_X(18) + 8) != OFS_KERNEL_TRAP_FRAME_X29) ||  \
     ((OFS_KERNEL_TRAP_FRAME_X30 + 8) != OFS_KERNEL_TRAP_FRAME_PC))
#error The layout of kernel_trap_frame structure has changed
#endif

#if (((OFS_KERNEL_TRAP_FRAME_FULL_X(30) + 8) !=				\
       OFS_KERNEL_TRAP_FRAME_FULL_SP_EL2) ||                            \
     ((OFS_KERNEL_TRAP_FRAME_FULL_PC + 8) !=				\
       OFS_KERNEL_TRAP_FRAME_FULL_SPSR_EL2))
#error The layout of kernel_trap_frame_full structure has changed
#endif

.macro	disable_phys_access
#if ARCH_AARCH64_USE_PAN
	// Ensure that accesses to 1:1 physical mappings are disabled. Note
	// that there's no way to have the CPU do this automatically for
	// traps from EL2 or a VCPU to EL2 (though it can be done with VHE for
	// traps from EL0 processes to EL2, when E2H=1 and TGE=1)
	msr	PAN, 1
#else
	// Phys access isn't controlled by the PAN bit; nothing to do here.
#endif
.endm

.macro	save_kernel_context
	disable_phys_access

	// SP (SP_EL2) points to the kernel stack in the thread structure
	sub	sp, sp, KERNEL_TRAP_FRAME_SIZE

	stp	x0, x1, [sp, OFS_KERNEL_TRAP_FRAME_X(0)]
	stp	x2, x3, [sp, OFS_KERNEL_TRAP_FRAME_X(2)]
	stp	x4, x5, [sp, OFS_KERNEL_TRAP_FRAME_X(4)]
	stp	x6, x7, [sp, OFS_KERNEL_TRAP_FRAME_X(6)]
	stp	x8, x9, [sp, OFS_KERNEL_TRAP_FRAME_X(8)]
	mrs	x0, ELR_EL2
	stp	x10, x11, [sp, OFS_KERNEL_TRAP_FRAME_X(10)]
	stp	x12, x13, [sp, OFS_KERNEL_TRAP_FRAME_X(12)]
	stp	x14, x15, [sp, OFS_KERNEL_TRAP_FRAME_X(14)]
	mrs	x1, SPSR_EL2
	stp	x16, x17, [sp, OFS_KERNEL_TRAP_FRAME_X(16)]
	stp	x18, x29, [sp, OFS_KERNEL_TRAP_FRAME_X(18)]
	stp	x30, x0, [sp, OFS_KERNEL_TRAP_FRAME_X30]
	str	x1, [sp, OFS_KERNEL_TRAP_FRAME_SPSR_EL2]

	// The callee-saved registers (X19-X28) are not saved here. If any
	// assembly code after this point wants to modify any of these
	// registers, it will need to save them first.
.endm

// Use this macro ONLY if jumping to panic or the kernel debugger afterwards.
.macro	save_kernel_context_full
	disable_phys_access

	// SP (SP_EL2) points to the kernel stack
	sub	sp, sp, KERNEL_TRAP_FRAME_FULL_SIZE

	stp	x0, x1, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(0)]
	stp	x2, x3, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(2)]
	stp	x4, x5, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(4)]
	stp	x6, x7, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(6)]
	stp	x8, x9, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(8)]
	mov	x0, sp
	stp	x10, x11, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(10)]
	stp	x12, x13, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(12)]
	mrs	x1, ELR_EL2
	stp	x14, x15, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(14)]
	stp	x16, x17, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(16)]
	stp	x18, x19, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(18)]
	mrs	x2, SPSR_EL2
	stp	x20, x21, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(20)]
	stp	x22, x23, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(22)]
	stp	x24, x25, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(24)]
	stp	x26, x27, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(26)]
	stp	x28, x29, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(28)]
	stp	x30, x0, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(30)]
	stp	x1, x2, [sp, OFS_KERNEL_TRAP_FRAME_FULL_PC]
.endm

// For the nested faults the stack may be corrupted, so we switch to a special
// stack set aside for this purpose.
.macro	save_kernel_context_stack_fault

	// At this point we don't care about preserving the value of TPIDR_EL0
	// and TPIDR_EL1
	msr	TPIDR_EL0, x0
	msr	TPIDR_EL1, x1

	// Get the CPU number and use it to calculate the address of the nested
	// fault context buffer for this CPU
	adr_threadlocal	x1, cpulocal_my_index
	ldrh	w1, [x1]

	adrl	x0, aarch64_emergency_stacks

	// X0 points to the special buffer we have allocated for nested faults,
	// and X1 is the CPU number of this core. The bottom of the stack for
	// this core is X0 + (X1 * NESTED_FAULT_STACK_SIZE).
	add	x0, x0, x1, lsl NESTED_FAULT_STACK_SIZE_BITS

	// Top of the stack is NESTED_FAULT_STACK_SIZE bytes higher. We also
	// need to subtract CONTEXT_REG_FRAME_SIZE_ALIGNED to cater for what we
	// are going to put on the stack. Do both in the same instruction.
	add	sp, x0, NESTED_FAULT_STACK_SIZE - KERNEL_TRAP_FRAME_FULL_SIZE

	mrs	x0, TPIDR_EL0
	mrs	x1, TPIDR_EL1

	stp	x0, x1, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(0)]
	stp	x2, x3, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(2)]
	stp	x4, x5, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(4)]
	stp	x6, x7, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(6)]
	stp	x8, x9, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(8)]
	mov	x0, sp
	stp	x10, x11, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(10)]
	stp	x12, x13, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(12)]
	mrs	x1, ELR_EL2
	stp	x14, x15, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(14)]
	stp	x16, x17, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(16)]
	stp	x18, x19, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(18)]
	mrs	x2, SPSR_EL2
	stp	x20, x21, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(20)]
	stp	x22, x23, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(22)]
	stp	x24, x25, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(24)]
	stp	x30, x0, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(30)]
	stp	x26, x27, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(26)]
	stp	x28, x29, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X(28)]
	stp	x1, x2, [sp, OFS_KERNEL_TRAP_FRAME_FULL_PC]
.endm

.macro thread_get_self, reg:req, tls_base, offset=0
.ifb \tls_base
	adr_threadlocal \reg, (current_thread + \offset)
.else
	adr_threadlocal \reg, (current_thread + \offset), \tls_base
.endif
.endm

// Hypervisor self vectors
.macro el2_vectors name
	vector vector_el2t_sync_\name\()
		save_kernel_context_full
#if !defined(NDEBUG)
		mov	x0, sp
		bl	dump_self_sync_fault
#endif
		panic	"EL2t vectors"
	vector_end vector_el2t_sync_\name\()

	vector vector_el2t_irq_\name\()
		save_kernel_context_full
#if !defined(NDEBUG)
		mov	x0, sp
		bl	dump_self_irq_fault
#endif
		panic	"EL2t vectors"
	vector_end vector_el2t_irq_\name\()

	vector vector_el2t_fiq_\name\()
		save_kernel_context_full
#if !defined(NDEBUG)
		mov	x0, sp
		bl	dump_self_fiq_fault
#endif
		panic	"EL2t vectors"
	vector_end vector_el2t_fiq_\name\()

	vector vector_el2t_serror_\name\()
		save_kernel_context_full
#if !defined(NDEBUG)
		mov	x0, sp
		bl	dump_self_serror
#endif
		panic	"EL2t vectors"
	vector_end vector_el2t_serror_\name\()


	// Hypervisor nested vectors
	vector vector_el2h_sync_\name\()
		save_kernel_context

		mov	x0, sp
		bl	vectors_exception_dispatch
		mov	x0, 0
		b	vectors_kernel_return
	vector_end vector_el2h_sync_\name\()

	vector vector_el2h_irq_\name\()
		save_kernel_context

		bl	vectors_interrupt_dispatch
		b	vectors_kernel_return
	vector_end vector_el2h_irq_\name\()

	vector vector_el2h_fiq_\name\()
		// In current implementations, all FIQs should go directly to
		// TrustZone and thus if we ever get one, panic.
		save_kernel_context_full

		panic	"EL2h vectors"
	vector_end vector_el2h_fiq_\name\()

	vector vector_el2h_serror_\name\()
		save_kernel_context

		// The dispatcher will inject a virtual SError to the RAS VM.
		mov	x0, sp
		bl	vectors_exception_dispatch
		mov	x0, 0
		b	vectors_kernel_return
	vector_end vector_el2h_serror_\name\()
.endm
