// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hypconstants.h>

#include <asm/asm_defs.inc>

	.global aarch64_boot_stack

	.section .bss.aarch64_boot_stack, "aw", @nobits
	.balign 16
	// This stack is used by boot_cold_init. It must be large enough to run
	// all the first-boot event triggers. This memory is then reclaimed by
	// the hypervisor_partition.
aarch64_boot_stack:
	.space BOOT_STACK_SIZE
	.previous

#if !defined(__ARM_FEATURE_UNALIGNED)
#define SCTLR_EL2_BOOT_DEBUG SCTLR_EL2_VM_A_MASK
#else
#define SCTLR_EL2_BOOT_DEBUG 0
#endif
#define SCTLR_EL2_BOOT ( \
		SCTLR_EL2_BOOT_DEBUG | \
		SCTLR_EL2_VM_I_MASK | \
		SCTLR_EL2_VM_WXN_MASK | \
		SCTLR_EL2_VM_SA_MASK)

// Hypervisor's 256-bit PRNG value
	_data hypervisor_prng_seed, 32, 8, "global"
	.space 32

// Initialize the EL2 Environment
//
// This is called by the platform boot code on the first power-on of the boot
// CPU, with MMU and caches disabled.
//
// Input arguments:
//    x0-x3: 256-bit RNG seed from the platform code
//    w4: Logical CPU number from the platform code

function aarch64_init
	// Disable debug, aborts and interrupts.
	msr	DAIFSet, 0xf

	// Save the seed
	adrl	x20, hypervisor_prng_seed
	stp	x0, x1, [x20]
	stp	x2, x3, [x20, 16]

	// Set the boot SCTLR
	abs64	x9, SCTLR_EL2_BOOT
	msr	SCTLR_EL2, x9
	isb

	// Save logical CPU number in w20 (call-preserved)
	mov	w20, w4

	// Set terminating frame pointer
	mov	x29, 0

	// Set physical boot stack
	adrl	x10, aarch64_boot_stack + BOOT_STACK_SIZE
	mov	sp, x10

	// Initialize the KASLR and return base virtual address
	bl	aarch64_init_kaslr
	// x0 return has hypervisor KASLR base address

	bic	x2, x0, 0x1fffff	// relocation offset
	adrl	x0, _DYNAMIC		// get address of _DYNAMIC
	bic	x1, x0, 0x1fffff	// current address offset

	// Apply ELF relocations
	bl	boot_rel_fixup

	// Initialize and enable the MMU
	mov	x2, 1
	bl	aarch64_init_address_space

	// w20 - contains logical cpu number
	//      (contiguous numbering: 0 .. N-1)
	cmp	w20, PLATFORM_MAX_CORES
	bge	aarch64_boot_error

	// Set virtual boot stack
	msr	SPSel, 1
	adrl	x10, aarch64_boot_stack + BOOT_STACK_SIZE
	mov	sp, x10

	// Set an invalid thread pointer address
	mov	x22, (1 << 63) - 1
	msr	TPIDR_EL2, x22

	// Runtime init sets TPIDR_EL2 and therefore must be called from
	// assembly to separate it from any code that might access
	// _Thread_local variables.
	mov	w0, w20
	bl	trigger_boot_runtime_first_init_event

	// Assert that the TPIDR_EL2 has been initialized
	mrs	x8, TPIDR_EL2
	cmp	x8, x22
	beq	aarch64_boot_error

	mov	w0, w20
	b	boot_cold_init
function_end aarch64_init

// Cold-boot a secondary CPU
//
// This is called by the platform boot code on the first power-on of any CPU
// other than the boot CPU, with MMU and caches disabled.
//
// Input arguments:
//    x0: Virtual pointer to the CPU's idle thread
//    w1: Logical CPU number from the platform code

function aarch64_secondary_init
	// Disable debug, aborts and interrupts.
	msr	DAIFSet, 0xf

	// Set the boot SCTLR
	abs64	x9, SCTLR_EL2_BOOT
	msr	SCTLR_EL2, x9
	isb

	// Save arguments in callee-saved registers
	mov	x19, x0
	mov	x20, x1

	// Set terminating frame pointer
	mov	x29, 0

#if !defined(NDEBUG)
	// Misalign the stack pointer to force a fault if it is used
	mov	sp, 1
#endif

	// Initialize and enable the MMU
	mov	x2, 0
	bl	aarch64_init_address_space

	// w20 - contains logical cpu number
	//      (contiguous numbering: 0 .. N-1)
	cmp	w20, PLATFORM_MAX_CORES
	bge	aarch64_boot_error

	// Load the idle thread's stack pointer
	ldr	x0, [x19, OFS_THREAD_CONTEXT_SP]
	msr	SPSel, 1
	mov	sp, x0

	// Set an invalid thread pointer address
	mov	x22, (1 << 63) - 1
	msr	TPIDR_EL2, x22

	// Runtime init sets TPIDR_EL2 and therefore must be called from
	// assembly to separate it from any code that might access
	// _Thread_local variables.
	mov	x0, x19
	bl	trigger_boot_runtime_warm_init_event

	// Assert that the TPIDR_EL2 has been initialized
	mrs	x8, TPIDR_EL2
	cmp	x8, x22
	beq	aarch64_boot_error

	// Start triggering the secondary cold boot events
	mov	w0, w20
	b	boot_secondary_init
function_end aarch64_secondary_init

// Warm-boot a CPU
//
// This is called by the platform boot code on a resume from power-off suspend
// or restart after hotplug of any CPU, with MMU and caches disabled.
//
// Input arguments:
//    x0: Virtual pointer to the CPU's idle thread.

function aarch64_warm_init
	// Disable debug, aborts and interrupts.
	msr	DAIFSet, 0xf

	// Set the boot SCTLR
	abs64	x9, SCTLR_EL2_BOOT
	msr	SCTLR_EL2, x9
	isb

	// Save arguments in callee-saved registers
	mov	x19, x0

	// Set terminating frame pointer
	mov	x29, 0

#if !defined(NDEBUG)
	// Misalign the stack pointer to force a fault if it is used
	mov	sp, 1
#endif

	// Initialize and enable the MMU
	mov	x2, 0
	bl	aarch64_init_address_space

	// Load the idle thread's stack pointer
	ldr	x0, [x19, OFS_THREAD_CONTEXT_SP]
	msr	SPSel, 1
	mov	sp, x0

	// Set an invalid thread pointer address
	mov	x22, (1 << 63) - 1
	msr	TPIDR_EL2, x22

	// Runtime init sets TPIDR_EL2 and therefore must be called from
	// assembly to separate it from any code that might access
	// _Thread_local variables.
	mov	x0, x19
	bl	trigger_boot_runtime_warm_init_event

	// Assert that the TPIDR_EL2 has been initialized
	mrs	x8, TPIDR_EL2
	cmp	x8, x22
	beq	aarch64_boot_error

	// Start triggering the warm boot events
	b	boot_warm_init
function_end aarch64_warm_init

function aarch64_boot_error
9:
	wfi
	b	9b
function_end aarch64_boot_error

	// Boot information which is passed from the linker
const64 thread_size, tbss_size
const64 thread_align, tbss_align
