// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hypconstants.h>

#include <asm/asm_defs.inc>
#include "vectors_el2.inc"

	.section	.text

function vectors_kernel_return
	// X0 is a mask to perform a bit-wise OR into SPSR before restoring;
	// used to disable interrupts on return from preemption
.if ((OFS_KERNEL_TRAP_FRAME_X30 + 8) != OFS_KERNEL_TRAP_FRAME_SPSR_EL2) || \
	((OFS_KERNEL_TRAP_FRAME_X29 + 8) != OFS_KERNEL_TRAP_FRAME_PC) || \
	((OFS_KERNEL_TRAP_FRAME_X(18) + 8) != OFS_KERNEL_TRAP_FRAME_SP_EL2)
.error "Kernel trap frame layout has changed"
.endif
	prfm	pldl1keep, [sp, OFS_KERNEL_TRAP_FRAME_X(0)]
	// Load X18, SP_EL2
	ldp	x18, x1, [sp, OFS_KERNEL_TRAP_FRAME_X(18)]
	ldp	x29, x16, [sp, OFS_KERNEL_TRAP_FRAME_X29]
	// Load x30, SPSR_EL2
	ldp	x30, x17, [sp, OFS_KERNEL_TRAP_FRAME_X30]
	ldp	x2, x3, [sp, OFS_KERNEL_TRAP_FRAME_X(2)]
	ldp	x4, x5, [sp, OFS_KERNEL_TRAP_FRAME_X(4)]
	ldp	x6, x7, [sp, OFS_KERNEL_TRAP_FRAME_X(6)]
	ldp	x8, x9, [sp, OFS_KERNEL_TRAP_FRAME_X(8)]
	msr	ELR_EL2, x16
	ldp	x10, x11, [sp, OFS_KERNEL_TRAP_FRAME_X(10)]
	// OR X0 into SPSR
	orr	x17, x17, x0
	ldp	x12, x13, [sp, OFS_KERNEL_TRAP_FRAME_X(12)]
	msr	SPSR_EL2, x17
	ldp	x14, x15, [sp, OFS_KERNEL_TRAP_FRAME_X(14)]
	mov	x0, sp
	ldp	x16, x17, [sp, OFS_KERNEL_TRAP_FRAME_X(16)]
	mov	sp, x1
	ldp	x0, x1, [x0, OFS_KERNEL_TRAP_FRAME_X(0)]

#if defined(ARCH_ARM_FEAT_PAuth)
	// Matching PACIA is in save_kernel_context
	eretaa
#else
	eret
#endif
function_end vectors_kernel_return
