// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hypconstants.h>

#include <asm/asm_defs.inc>
#include <asm/panic.inc>

#include "vectors_el2.inc"
#include "vectors_vcpu.inc"


	.section	.text

// Returning from a guest exception
function vcpu_exception_return, align=32
	BRANCH_TARGET(c,)
	thread_get_self	x30, offset=OFS_THREAD_VCPU_REGS_GPR

	// Load ELR_EL2, SPSR_EL2
	ldp	x27, x28, [x30, OFS_VCPU_GPR_PC]

	vcpu_pauth_exit_thread x30, x0, x1, x28, x2

	// Restore the general purpose registers
	ldp	x0, x1, [x30, OFS_VCPU_GPR_X(0)]
	ldp	x2, x3, [x30, OFS_VCPU_GPR_X(2)]
	ldp	x4, x5, [x30, OFS_VCPU_GPR_X(4)]
	ldp	x6, x7, [x30, OFS_VCPU_GPR_X(6)]
	ldp	x8, x9, [x30, OFS_VCPU_GPR_X(8)]
	msr	ELR_EL2, x27
	ldp	x10, x11, [x30, OFS_VCPU_GPR_X(10)]
	ldp	x12, x13, [x30, OFS_VCPU_GPR_X(12)]
	ldp	x14, x15, [x30, OFS_VCPU_GPR_X(14)]
	ldp	x16, x17, [x30, OFS_VCPU_GPR_X(16)]
	msr	SPSR_EL2, x28
	ldp	x18, x19, [x30, OFS_VCPU_GPR_X(18)]
	ldp	x20, x21, [x30, OFS_VCPU_GPR_X(20)]
	ldp	x22, x23, [x30, OFS_VCPU_GPR_X(22)]
	ldp	x24, x25, [x30, OFS_VCPU_GPR_X(24)]
	ldp	x26, x27, [x30, OFS_VCPU_GPR_X(26)]
	ldp	x28, x29, [x30, OFS_VCPU_GPR_X(28)]
	ldr	x30, [x30, OFS_VCPU_GPR_X(30)]

	eret
function_end vcpu_exception_return

// Returning from a guest hypercall
//
// A fast call (where C returns in registers) may need to sanitise starting
// between X0 and X2. A slow call (where C returns in memory) always loads
// the first 8 registers from the stack, having sanitised them in memory
// before making the call.
function vcpu_hypercall_return_sanitize_x0, align=64
	mov	x0, xzr
function_chain vcpu_hypercall_return_sanitize_x0, vcpu_hypercall_return_sanitize_x1
	mov	x1, xzr
function_chain vcpu_hypercall_return_sanitize_x1, vcpu_hypercall_return_sanitize_x2
	mov	x2, xzr
	mov	x3, xzr
	mov	x4, xzr
	mov	x5, xzr
	mov	x6, xzr
	mov	x7, xzr
function_chain vcpu_hypercall_return_sanitize_x2, vcpu_hypercall_return_sanitize_x8
	// SP_EL2 points to the kernel stack in the thread structure

	// Restore ELR_EL2, SPSR_EL2
	ldp	x9, x10, [sp], #16

	vcpu_pauth_exit x19, x20, x21, x10, x22

	// Sanitise the AAPCS64-defined caller-saved registers to prevent
	// information leakage from compiled hypervisor code to the guest.
	// X30 is also caller-saved but was saved and restored above, as was
	// X18 which must be preserved at all times.

	// Restore caller saved registers
	ldp	x28, x29, [sp], #16
	mov	x11, xzr
	mov	x12, xzr
	ldp	x26, x27, [sp], #16
	msr	elr_el2, x9
	mov	x13, xzr
	ldp	x24, x25, [sp], #16
	msr	spsr_el2, x10
	mov	x14, xzr
	ldp	x22, x23, [sp], #16
	mov	x15, xzr
	mov	x16, xzr
	ldp	x20, x21, [sp], #16

	mov	x17, xzr

	// Temporarily preserve x8 for backwards compatibility. It should be
	// sanitised once this is removed.
	//mov	x8, xzr
	ldp	x8, x19, [sp], #16

	// Pop X18 and X30 that we had pushed on entry
	ldp	x18, x30, [sp], #16

	// Note: X9 = ELR_EL2 (not sensitive); X10 = SPSR_EL2 (possibly
	// sensitive and should be zeroed).
	mov	x10, xzr

	eret
function_end vcpu_hypercall_return_sanitize_x8

#if defined(ARCH_ARM_8_3_PAUTH)
function vcpu_pauth_exit_failed
	panic	"Invalid SPSR_EL2.M in VCPU exception return"
function_end vcpu_pauth_exit_failed
#endif
