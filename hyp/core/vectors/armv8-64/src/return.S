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
	ldp	x30, x16, [sp, OFS_KERNEL_TRAP_FRAME_X30]
	ldr	x17, [sp, OFS_KERNEL_TRAP_FRAME_SPSR_EL2]
	ldp	x2, x3, [sp, OFS_KERNEL_TRAP_FRAME_X(2)]
	ldp	x4, x5, [sp, OFS_KERNEL_TRAP_FRAME_X(4)]
	ldp	x6, x7, [sp, OFS_KERNEL_TRAP_FRAME_X(6)]
	ldp	x8, x9, [sp, OFS_KERNEL_TRAP_FRAME_X(8)]
	msr	ELR_EL2, x16
	ldp	x10, x11, [sp, OFS_KERNEL_TRAP_FRAME_X(10)]
	orr	x17, x17, x0
	ldp	x12, x13, [sp, OFS_KERNEL_TRAP_FRAME_X(12)]
	msr	SPSR_EL2, x17
	ldp	x14, x15, [sp, OFS_KERNEL_TRAP_FRAME_X(14)]
	ldp	x16, x17, [sp, OFS_KERNEL_TRAP_FRAME_X(16)]
	ldp	x18, x29, [sp, OFS_KERNEL_TRAP_FRAME_X(18)]
	ldp	x0, x1, [sp, OFS_KERNEL_TRAP_FRAME_X(0)]

	add	sp, sp, KERNEL_TRAP_FRAME_SIZE

	eret
function_end vectors_kernel_return
