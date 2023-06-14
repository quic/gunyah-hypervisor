// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Size of the per-CPU memory reserved for nested fault context buffer and stack
#if !defined(NDEBUG)
#define NESTED_FAULT_STACK_SIZE_BITS 11		// 2048 bytes
#else
#define NESTED_FAULT_STACK_SIZE_BITS 10		// 1024 bytes
#endif
#define NESTED_FAULT_STACK_SIZE (1 << NESTED_FAULT_STACK_SIZE_BITS)

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

#if defined(ARCH_ARM_FEAT_PAuth)
// Define symbols for pointer auth offsets so we can access them from macros
.equ	pauth_DA_ofs, OFS_AARCH64_PAUTH_KEYS_DA
.equ	pauth_DB_ofs, OFS_AARCH64_PAUTH_KEYS_DB
.equ	pauth_IA_ofs, OFS_AARCH64_PAUTH_KEYS_IA
.equ	pauth_IB_ofs, OFS_AARCH64_PAUTH_KEYS_IB
.equ	pauth_GA_ofs, OFS_AARCH64_PAUTH_KEYS_GA

// Macro for loading an EL2 key
.macro  kernel_pauth_entry_key k:req, kp:req, kl:req, kh:req
	ldp	\kl, \kh, [\kp, pauth_\k\()_ofs]
	msr	AP\k\()KeyLo_EL1, \kl
	msr	AP\k\()KeyHi_EL1, \kh
.endm
#endif

// Macro for restoring the EL2 pointer auth keys (discarding any EL1 keys)
.macro kernel_pauth_entry kp:req, kl:req, kh:req
#if defined(ARCH_ARM_FEAT_PAuth)
	adrl	\kp, aarch64_pauth_keys
	kernel_pauth_entry_key DA, \kp, \kl, \kh
	kernel_pauth_entry_key DB, \kp, \kl, \kh
	kernel_pauth_entry_key IA, \kp, \kl, \kh
	kernel_pauth_entry_key IB, \kp, \kl, \kh
	kernel_pauth_entry_key GA, \kp, \kl, \kh
	isb
#endif
.endm

.macro	check_stack_overflow
	// Kernel stacks are aligned to twice their maximum allowed size.
	// This means valid stack accesses will never have the bit at
	// THREAD_STACK_MAX_SIZE set, otherwise we have a stack overflow.
	// We must temporarily swap SP and X0 in order to test this.
	sub	sp, sp, x0
	add	x0, sp, x0
	tst	x0, THREAD_STACK_MAX_SIZE
	sub	x0, sp, x0
	sub	sp, sp, x0
	neg	x0, x0
	b.ne	handle_stack_fault
.endm

.macro	save_kernel_context full:req overflow:req
	disable_phys_access

.if \full
	// SP (SP_EL2) points to the kernel stack in the thread structure
	// Skip 256 bytes of stack so we don't clobber any stack history useful
	// for debugging the exception.
	sub	sp, sp, KERNEL_TRAP_FRAME_FULL_SIZE + 0x100
.else
	// SP (SP_EL2) points to the kernel stack in the thread structure
	sub	sp, sp, KERNEL_TRAP_FRAME_SIZE
.endif

.if \overflow
	// The exception may have been triggered by a stack overflow,
	// so check for this before storing anything on the stack.
	check_stack_overflow
.endif

	stp	x0, x1, [sp, OFS_KERNEL_TRAP_FRAME_X(0)]
	mrs	x0, ELR_EL2
	stp	x2, x3, [sp, OFS_KERNEL_TRAP_FRAME_X(2)]
	// Recalculate the SP from before we pushed the frame. Note that we
	// can't do a regular PACIASP before changing SP because we need to
	// save a register to load ELR_EL2 into first, and doing that before
	// updating SP risks an infinite loop if the store faults.
.if \full
	add	x2, sp, KERNEL_TRAP_FRAME_FULL_SIZE + 0x100
.else
	add	x2, sp, KERNEL_TRAP_FRAME_SIZE
.endif

	stp	x4, x5, [sp, OFS_KERNEL_TRAP_FRAME_X(4)]
#if defined(ARCH_ARM_FEAT_PAuth)
	// Insert PAC bits in ELR_EL2 before we save it (matching the ERETAA
	// in vectors_kernel_return)
	pacia	x0, x2
#endif
	stp	x6, x7, [sp, OFS_KERNEL_TRAP_FRAME_X(6)]
	mrs	x1, SPSR_EL2
	stp	x8, x9, [sp, OFS_KERNEL_TRAP_FRAME_X(8)]
	stp	x10, x11, [sp, OFS_KERNEL_TRAP_FRAME_X(10)]
	stp	x12, x13, [sp, OFS_KERNEL_TRAP_FRAME_X(12)]
	stp	x14, x15, [sp, OFS_KERNEL_TRAP_FRAME_X(14)]
	stp	x16, x17, [sp, OFS_KERNEL_TRAP_FRAME_X(16)]
#if ((OFS_KERNEL_TRAP_FRAME_X30 + 8) != OFS_KERNEL_TRAP_FRAME_SPSR_EL2) || \
	((OFS_KERNEL_TRAP_FRAME_X29 + 8) != OFS_KERNEL_TRAP_FRAME_PC) || \
	((OFS_KERNEL_TRAP_FRAME_X(18) + 8) != OFS_KERNEL_TRAP_FRAME_SP_EL2)
#error The layout of kernel_trap_frame structure has changed
#endif
	// Save X18 and original SP_EL2
	stp	x18, x2, [sp, OFS_KERNEL_TRAP_FRAME_X(18)]
	stp	x29, x0, [sp, OFS_KERNEL_TRAP_FRAME_X29]
	add	x29, sp, OFS_KERNEL_TRAP_FRAME_X29
	stp	x30, x1, [sp, OFS_KERNEL_TRAP_FRAME_X30]

	// The callee-saved registers (X19-X28) are not saved here. If any
	// assembly code after this point wants to modify any of these
	// registers, it will need to save them first.
.endm

// Use this macro ONLY if jumping to panic or the kernel debugger afterwards.
// There is no function that can restore this frame.
.macro	save_kernel_context_full
	disable_phys_access

	// SP (SP_EL2) points to the kernel stack
	sub	sp, sp, KERNEL_TRAP_FRAME_FULL_SIZE

	stp	x0, x1, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(0)]
	stp	x2, x3, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(2)]
	stp	x4, x5, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(4)]
	stp	x6, x7, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(6)]
	stp	x8, x9, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(8)]
	add	x0, sp, KERNEL_TRAP_FRAME_FULL_SIZE
	stp	x10, x11, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(10)]
	stp	x12, x13, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(12)]
	mrs	x1, ELR_EL2
	stp	x14, x15, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(14)]
	stp	x16, x17, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(16)]
	mrs	x2, SPSR_EL2
#if ((OFS_KERNEL_TRAP_FRAME_FULL_BASE_X30 + 8) != OFS_KERNEL_TRAP_FRAME_FULL_BASE_SPSR_EL2) || \
	((OFS_KERNEL_TRAP_FRAME_FULL_BASE_X29 + 8) != OFS_KERNEL_TRAP_FRAME_FULL_BASE_PC) || \
	((OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(18) + 8) != OFS_KERNEL_TRAP_FRAME_FULL_BASE_SP_EL2)
#error The layout of kernel_trap_frame structure has changed
#endif
	stp	x18, x0, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(18)]
	stp	x29, x1, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X29]
	stp	x30, x2, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X30]
	add	x29, sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X29

	stp	x19, x20, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X19]
	stp	x21, x22, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X21]
	stp	x23, x24, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X23]
	stp	x25, x26, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X25]
	stp	x27, x28, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X27]
.endm

// For the nested faults the stack may be corrupted, so we switch to a special
// stack set aside for this purpose.
.macro	save_kernel_context_stack_fault
	// At this point we don't care about preserving the value of TPIDR_EL0
	// and TPIDR_EL1
	msr	TPIDR_EL0, x0
	msr	TPIDR_EL1, x1

	// The emergency stacks must use the emergency vectors.
	adr	x0, emergency_vectors_aarch64
	msr	VBAR_EL2, x0
	isb

	// Get the CPU number and use it to calculate the address of the nested
	// fault context buffer for this CPU
	adr_threadlocal	x1, current_thread + OFS_THREAD_CPULOCAL_CURRENT_CPU
	ldrh	w1, [x1]

	adrl	x0, aarch64_emergency_stacks

	// X0 points to the special buffer we have allocated for nested faults,
	// and X1 is the CPU number of this core. The bottom of the stack for
	// this core is X0 + (X1 * NESTED_FAULT_STACK_SIZE).
	add	x0, x0, x1, lsl NESTED_FAULT_STACK_SIZE_BITS

	// Top of the stack is NESTED_FAULT_STACK_SIZE bytes higher. We also
	// need to subtract CONTEXT_REG_FRAME_SIZE_ALIGNED to cater for what we
	// are going to put on the stack. Do both in the same instruction.
	// Don't modify sp yet because we want to save it in the frame.
	add	x0, x0, NESTED_FAULT_STACK_SIZE - KERNEL_TRAP_FRAME_FULL_SIZE

	// Push a few extra registers
	stp	x2, x3, [x0, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(2)]

	// Restore the stashed x0 and x1 and push them too
	mrs	x2, TPIDR_EL0
	mrs	x3, TPIDR_EL1
	stp	x2, x3, [x0, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(0)]

	// Stash the faulting stack and update sp
	mov	x3, sp
	mov	sp, x0

	// Push everything else
	stp	x4, x5, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(4)]
	stp	x6, x7, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(6)]
	stp	x8, x9, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(8)]
	stp	x10, x11, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(10)]
	stp	x12, x13, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(12)]
	mrs	x1, ELR_EL2
	stp	x14, x15, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(14)]
	stp	x16, x17, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(16)]
	mrs	x2, SPSR_EL2
#if ((OFS_KERNEL_TRAP_FRAME_FULL_BASE_X30 + 8) != OFS_KERNEL_TRAP_FRAME_FULL_BASE_SPSR_EL2) || \
	((OFS_KERNEL_TRAP_FRAME_FULL_BASE_X29 + 8) != OFS_KERNEL_TRAP_FRAME_FULL_BASE_PC) || \
	((OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(18) + 8) != OFS_KERNEL_TRAP_FRAME_FULL_BASE_SP_EL2)
#error The layout of kernel_trap_frame structure has changed
#endif
	stp	x18, x3, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X(18)]
	stp	x29, x1, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X29]
	stp	x30, x2, [sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X30]
	add	x29, sp, OFS_KERNEL_TRAP_FRAME_FULL_BASE_X29

	stp	x19, x20, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X19]
	stp	x21, x22, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X21]
	stp	x23, x24, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X23]
	stp	x25, x26, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X25]
	stp	x27, x28, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X27]
.endm

.macro thread_get_self, reg:req, tls_base, offset=0
.ifb \tls_base
	adr_threadlocal \reg, (current_thread + \offset)
.else
	adr_threadlocal \reg, (current_thread + \offset), \tls_base
.endif
.endm

// Hypervisor self vectors
.macro el2_vectors name:req, overflow:req
	vector vector_el2t_sync_\name\()
		save_kernel_context_full
		mov	x0, sp
		bl	dump_self_sync_fault
		panic	"EL2t vectors"
	vector_end vector_el2t_sync_\name\()

	vector vector_el2t_irq_\name\()
		save_kernel_context_full
		mov	x0, sp
		bl	dump_self_irq_fault
		panic	"EL2t vectors"
	vector_end vector_el2t_irq_\name\()

	vector vector_el2t_fiq_\name\()
		save_kernel_context_full
		mov	x0, sp
		bl	dump_self_fiq_fault
		panic	"EL2t vectors"
	vector_end vector_el2t_fiq_\name\()

	vector vector_el2t_serror_\name\()
		save_kernel_context_full
		mov	x0, sp
		bl	dump_self_serror
		panic	"EL2t vectors"
	vector_end vector_el2t_serror_\name\()


	// Hypervisor nested vectors
	vector vector_el2h_sync_\name\()
		save_kernel_context 1 \overflow

		mov	x0, sp
		bl	vectors_exception_dispatch_full
		mov	x0, 0
		b	vectors_kernel_return
	vector_end vector_el2h_sync_\name\()

	vector vector_el2h_irq_\name\()
		save_kernel_context 0 0

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
		save_kernel_context 1 0

		// The dispatcher will inject a virtual SError to the RAS VM.
		mov	x0, sp
		bl	vectors_exception_dispatch_full
		mov	x0, 0
		b	vectors_kernel_return
	vector_end vector_el2h_serror_\name\()
.endm

.macro default_guest_vectors name:req
	vector vector_guest64_sync_\name\()
		save_kernel_context_full
		panic	"64-bit guest vectors"
	vector_end vector_guest64_sync_\name\()

	vector vector_guest64_irq_\name\()
		save_kernel_context_full
		panic	"64-bit guest vectors"
	vector_end vector_guest64_irq_\name\()

	vector vector_guest64_fiq_\name\()
		save_kernel_context_full
		panic	"64-bit guest vectors"
	vector_end vector_guest64_fiq_\name\()

	vector vector_guest64_serror_\name\()
		save_kernel_context_full
		panic	"64-bit guest vectors"
	vector_end vector_guest64_serror_\name\()

	vector vector_guest32_sync_\name\()
		save_kernel_context_full
		panic	"32-bit guest vectors"
	vector_end vector_guest32_sync_\name\()

	vector vector_guest32_irq_\name\()
		save_kernel_context_full
		panic	"32-bit guest vectors"
	vector_end vector_guest32_irq_\name\()

	vector vector_guest32_fiq_\name\()
		save_kernel_context_full
		panic	"32-bit guest vectors"
	vector_end vector_guest32_fiq_\name\()

	vector vector_guest32_serror_\name\()
		save_kernel_context_full
		panic	"32-bit guest vectors"
	vector_end vector_guest32_serror_\name\()
.endm

.macro default_vectors name:req, overflow:req
	el2_vectors \name \overflow
	default_guest_vectors \name
.endm
