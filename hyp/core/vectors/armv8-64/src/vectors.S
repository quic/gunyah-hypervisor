// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hypconstants.h>

#include <asm/asm_defs.inc>
#include <asm/panic.inc>

#include "vectors_el2.inc"

	.section	.text.vectors

	// The vector table base is 2KB aligned
	// (16 vectors, 32 instructions each)
	.balign		2048
.global vectors_aarch64
vectors_aarch64:

el2_vectors kernel

// Guest vectors
vector vector_guest64_sync_panic
	save_kernel_context_full
	panic	"64-bit guest vectors"
vector_end vector_guest64_sync_panic

vector vector_guest64_irq_panic
	save_kernel_context_full
	panic	"64-bit guest vectors"
vector_end vector_guest64_irq_panic

vector vector_guest64_fiq_panic
	save_kernel_context_full
	panic	"64-bit guest vectors"
vector_end vector_guest64_fiq_panic

vector vector_guest64_serror_panic
	save_kernel_context_full
	panic	"64-bit guest vectors"
vector_end vector_guest64_serror_panic

vector vector_guest32_sync_panic
	save_kernel_context_full
	panic	"32-bit guest vectors"
vector_end vector_guest32_sync_panic

vector vector_guest32_irq_panic
	save_kernel_context_full
	panic	"32-bit guest vectors"
vector_end vector_guest32_irq_panic

vector vector_guest32_fiq_panic
	save_kernel_context_full
	panic	"32-bit guest vectors"
vector_end vector_guest32_fiq_panic

vector vector_guest32_serror_panic
	save_kernel_context_full
	panic	"32-bit guest vectors"
vector_end vector_guest32_serror_panic

function vectors_exception_dispatch_full
	// In save_kernel_context, space was reserved for a full frame.
	// Save remaining callee-saved registers.
	stp	x19, x20, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X19]
	stp	x21, x22, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X21]
	stp	x23, x24, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X23]
	stp	x25, x26, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X25]
	stp	x27, x28, [sp, OFS_KERNEL_TRAP_FRAME_FULL_X27]

	b	vectors_exception_dispatch
function_end vectors_exception_dispatch_full

function handle_stack_fault
	save_kernel_context_stack_fault
#if !defined(NDEBUG)
	mov	x0, sp
	bl	dump_nested_fault
#endif
	panic	"Stack fault detected in EL2h vectors"
function_end handle_stack_fault

function __stack_chk_fail
	save_kernel_context_stack_fault
#if !defined(NDEBUG)
	mov	x0, sp
	bl	dump_nested_fault
#endif
	panic	"Stack corruption detected in EL2"
function_end __stack_chk_fail

	.section	.bss.vectors, "aw", @nobits
// Allocate a chunk of memory for each CPU for the nested fault stack
#define EMERGENCY_STACKS_SIZE (NESTED_FAULT_STACK_SIZE * PLATFORM_MAX_CORES)

	.balign	16
aarch64_emergency_stacks:
	.size aarch64_emergency_stacks, EMERGENCY_STACKS_SIZE
	.space EMERGENCY_STACKS_SIZE
