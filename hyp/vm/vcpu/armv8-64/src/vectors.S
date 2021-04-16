// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hypconstants.h>

#if defined(HYPERCALLS)
#include <hypcall_def.h>
#endif

#include <asm/asm_defs.inc>
#include <asm/cpu.h>
#include <asm/panic.inc>

#include "vectors_el2.inc"
#include "vectors_vcpu.inc"

.macro hypercall_decode_native el:req, ec:req
	// Extract the exception class
	mrs	x18, ESR_EL2
	lsr	w30, w18, 26

#if defined(HYPERCALLS)
	// Is it an HVC instruction?
	cmp	w30, \ec

	// Extract ESR_EL2.ISS (hypercall number)
	ubfx	x18, x18, 0, 16
	// Adjust in regards to the base hypercall number
	sub	x18, x18, HYPERCALL_BASE

	b.ne	LOCAL(\el\()_non_hypercall)
	// Is it a valid hypercall?
	cmp	x18, HYPERCALL_NUM
	b.hs	LOCAL(\el\()_non_hypercall)

	// Temporarily preserve x8 for backwards compatibility.
	stp	x8, x19, [sp, #-16]!

	// It's a native hypercall, we zero or trash HVC caller saved registers.
	// note: x9, x10, x11, x18, x30 are already overwritten here.
	clear_guest_registers "12,13,14"

	// Save and zero out callee saved registers. We don't let these get
	// passed as-is to the C code to prevent EL1 targeting EL2 gadgets.
	// Note: x19 saved above
	stp	x20, x21, [sp, #-16]!
	clear_guest_registers "15,16,17"
	stp	x22, x23, [sp, #-16]!
	clear_guest_registers "8,19"
	stp	x24, x25, [sp, #-16]!
	clear_guest_registers "20,21"
	stp	x26, x27, [sp, #-16]!
	clear_guest_registers "22,23"
	stp	x28, x29, [sp, #-16]!
	// moved to hypercall_64_entry (vector is full)
	// - clear_guest_registers "24,25,26,27,28,29"

	b	hypercall_64_entry
#endif

local \el\()_non_hypercall:
.endm

	.section	.text.vectors

	// The vector table base is 2KB aligned
	// (16 vectors, 32 instructions each)
	.balign		2048
.global vcpu_aarch64_vectors
vcpu_aarch64_vectors:

el2_vectors vcpu

// Guest vectors
// The assumption is that upon entry, SP (SP_EL2) points to the kernel stack,
// so we can start pushing to the stack without having to adjust SP first.
vector vector_guest64_sync
	disable_phys_access
	// First check whether this is a hypercall or some other exception, as
	// it affects which registers we need to save.

	stp	x18, x30, [sp, #-16]!

	hypercall_decode_native aarch64, ENUM_ESR_EC_HVC64_EL2

	// Not a hypercall, jump to the trap handler
	// Pop the original values of X18 and X30 as we need to save them in
	// the exception context
	ldp	x18, x30, [sp], #16

	b	exception_64_entry
vector_end vector_guest64_sync

vector vector_guest64_irq
	save_guest_context_vcpu_zero_0_13_irq

	b	irq_64_entry
vector_end vector_guest64_irq

vector vector_guest64_fiq
	// In current implementations, all FIQs should go directly to TrustZone
	// and thus if we ever get one, panic.
	save_kernel_context_full

	panic	"64-bit guest vectors"
vector_end vector_guest64_fiq

vector vector_guest64_serror
	disable_phys_access

	b	exception_64_entry
vector_end vector_guest64_serror


#if defined(CONFIG_AARCH64_32BIT_EL1)
vector vector_guest32_sync
	disable_phys_access
	// First check whether this is a hypercall or some other exception, as
	// it affects which registers we need to save.

	stp	x18, x30, [sp, #-16]!

	hypercall_decode_native aarch32, ENUM_ESR_EC_HVC32_EL2

	// Not a hypercall, jump to the trap handler
	// Pop the original values of X18 and X30 as we need to save them in
	// the exception context
	ldp	x18, x30, [sp], #16

	b	exception_32_entry
vector_end vector_guest32_sync

vector vector_guest32_irq
	save_kernel_context_vcpu_zero_0_13
	save_guest_context_vcpu_zero_14_29

	bl	vcpu_interrupt_dispatch
	b	vcpu_exception_return
vector_end vector_guest32_irq

vector vector_guest32_fiq
	// In current implementations, all FIQs should go directly to TrustZone
	// and thus if we ever get one, panic.
	save_kernel_context_full

	panic	"32-bit guest vectors"
vector_end vector_guest32_fiq

vector vector_guest32_serror
	disable_phys_access

	bl	exception_32_entry
vector_end vector_guest32_serror

#else	// defined(CONFIG_AARCH64_32BIT_EL1)

// In theory we should never get to any of these
vector vector_guest32_sync
	save_kernel_context_full
	panic	"32-bit guest vectors"
vector_end vector_guest32_sync

vector vector_guest32_irq
	save_kernel_context_full
	panic	"32-bit guest vectors"
vector_end vector_guest32_irq

vector vector_guest32_fiq
	save_kernel_context_full
	panic	"32-bit guest vectors"
vector_end vector_guest32_fiq

vector vector_guest32_serror
	save_kernel_context_full
	panic	"32-bit guest vectors"
vector_end vector_guest32_serror

#endif // defined(CONFIG_AARCH64_32BIT_EL1)


function hypercall_64_entry local, align=(1 << CPU_L1D_LINE_BITS)
	mrs	x10, elr_el2
	clear_guest_registers "24,25"
	mrs	x11, spsr_el2
	clear_guest_registers "26,27"

	// Compute the hypercall table handler address in x9.
	adr	x9, hypercall_table

	// Save ELR_EL2 and SPSR_EL2.
	stp	x10, x11, [sp, #-16]!

	add	x9, x9, x18, lsl 3
	clear_guest_registers "28,29"

	// ELR_EL2, SPSR_EL2 are also VM controlled values, zero them as well.
	clear_guest_registers "10,11"

	// Jump to the hypercall table handler.
	br	x9
function_end hypercall_64_entry

function exception_64_entry local, align=(1 << CPU_L1D_LINE_BITS)
	save_guest_exception_context_zero_1_29

	mov	x0, 1		// 1: AArch64
	bl	vcpu_exception_dispatch
	b	vcpu_exception_return
function_end exception_64_entry

function irq_64_entry local, align=(1 << CPU_L1D_LINE_BITS)
	save_guest_context_vcpu_zero_14_29_irq

	bl	vcpu_interrupt_dispatch
	b	vcpu_exception_return
function_end irq_64_entry

#if defined(CONFIG_AARCH64_32BIT_EL1)
function exception_32_entry local
	save_guest_exception_context_zero_1_29

	mov	x0, 0		// 0: AArch32
	bl	vcpu_exception_dispatch
	b	vcpu_exception_return
function_end exception_32_entry
#endif // defined(CONFIG_AARCH64_32BIT_EL1)
