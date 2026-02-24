// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hypconstants.h>
#include <asm/asm_defs.inc>
#include <asm/panic.inc>

#include "vectors_el2.inc"
#include "vectors_vcpu.inc"
// ---------------------------------------------------------------------------
// bpiall_workaround
//
// Saves all GP registers and system registers, then drops to AArch32 EL1 to
// execute BPIALL, and returns via HVC.Entire sequence is in 32 instructions
// Called from vector_guest64_sync and vector_guest32_sync above.
// ---------------------------------------------------------------------------
.macro bpiall_workaround
	stp	x0,  x1,  [sp, #-0x120]!
	stp	x2,  x3,  [sp, 0x010]
	stp	x4,  x5,  [sp, 0x020]
	stp	x6,  x7,  [sp, 0x030]
	stp	x8,  x9,  [sp, 0x040]
	stp	x10, x11, [sp, 0x050]
	stp	x12, x13, [sp, 0x060]
	stp	x14, x15, [sp, 0x070]
	stp	x16, x17, [sp, 0x080]
	stp	x18, x19, [sp, 0x090]
	stp	x20, x21, [sp, 0x0A0]
	stp	x22, x23, [sp, 0x0B0]
	stp	x24, x25, [sp, 0x0C0]
	stp	x26, x27, [sp, 0x0D0]
	stp	x28, x29, [sp, 0x0E0]
	stp	x30, xzr, [sp, 0x0F0]		// x30 + alignment pad
	mrs	x15, ELR_EL2
	mrs	x16, SPSR_EL2
	stp	x15, x16, [sp, 0x100]
	mrs	x15, SCTLR_EL1
	mrs	x16, ESR_EL2
	stp	x15, x16, [sp, 0x110]
	msr	HCR_EL2,   xzr
	mov	x3, #SCTLR_EL1_I_MASK
	msr	SCTLR_EL1, x3
	adr	x1, bpiall_wa_exit_vectors
	msr	VBAR_EL2, x1
	ldr	x2, bpiall_wa_code_pa
	msr	ELR_EL2, x2
	mov	x3, #(ENUM_SPSR_32BIT_MODE_SUPERVISOR | SPSR_EL2_A32_A_MASK | SPSR_EL2_A32_I_MASK | SPSR_EL2_A32_F_MASK)
	msr	SPSR_EL2, x3
	eret
.endm

function bpiall_wa_code local
	.word	0xEE070FD5	// MCR p15, 0, r0, c7, c5, 6 - BPIALL: invalidate all Branch Predictors
	.word	0xE1400070	// HVC #0 - trap back to EL2 via bpiall_wa_exit_vectors
function_end bpiall_wa_code

// PA of bpiall_wa_code, fixed up by vcpu_bpiall_wa_init_pa() before MMU init.
// In .rodata so it can be loaded with a single PC-relative LDR from bpiall_workaround.
	const64	bpiall_wa_code_pa


// ---------------------------------------------------------------------------
// bpiall_wa_restore_and_dispatch
//
// HVC return path.  Restores registers saved in the vector slot
// and branches to vector_guest32_sync or vector_guest64_sync based on
// SPSR_EL2.M[4].
// ---------------------------------------------------------------------------
function bpiall_wa_restore_and_dispatch local
	// Restore system registers
	thread_get_fast	x15, x16
	ldp	x17, x18, [sp, 0x110]		// x17=SCTLR_EL1, x18=ESR_EL2
	ldp	x19, x20, [sp, 0x100]		// x19=ELR_EL2,   x20=SPSR_EL2
	adr	x21, vcpu_aarch64_vectors_tramp_bpiall
	ldr	x22, [x15, OFS_THREAD_VCPU_REGS_EL2_HCR_EL2]
	msr	SCTLR_EL1, x17
	msr	ESR_EL2,   x18
	msr	ELR_EL2,   x19
	msr	SPSR_EL2,  x20
	msr	VBAR_EL2,  x21
	msr	HCR_EL2, x22
	tst	x20, #SPSR_EL2_BASE_M4_MASK		// M[4]: 1=AArch32, 0=AArch64
	// Restore GP registers and unwind stack
	ldp	x0,  x1,  [sp, 0x000]
	ldp	x2,  x3,  [sp, 0x010]
	ldp	x4,  x5,  [sp, 0x020]
	ldp	x6,  x7,  [sp, 0x030]
	ldp	x8,  x9,  [sp, 0x040]
	ldp	x10, x11, [sp, 0x050]
	ldp	x12, x13, [sp, 0x060]
	ldp	x14, x15, [sp, 0x070]
	ldp	x16, x17, [sp, 0x080]
	ldp	x18, x19, [sp, 0x090]
	ldp	x20, x21, [sp, 0x0A0]
	ldp	x22, x23, [sp, 0x0B0]
	ldp	x24, x25, [sp, 0x0C0]
	ldp	x26, x27, [sp, 0x0D0]
	ldp	x28, x29, [sp, 0x0E0]
	ldr	x30,      [sp, 0x0F0]
	add	sp, sp, #0x120
	b.ne	vector_guest32_sync		// AArch32 guest
	b	vector_guest64_sync		// AArch64 guest
function_end bpiall_wa_restore_and_dispatch

// ---------------------------------------------------------------------------
// vcpu_bpiall_wa_init_pa
//
// Called from early assembly before the MMU is enabled and bpiall_wa_code
// PA is stored in bpiall_wa_code_pa
// ---------------------------------------------------------------------------
function vcpu_bpiall_wa_init_pa
	adr	x0, bpiall_wa_code		// VA == PA before MMU
	adr	x1, bpiall_wa_code_pa
	str	x0, [x1]
	ret
function_end vcpu_bpiall_wa_init_pa

// ===========================================================================
// vcpu_aarch64_vectors_tramp_bpiall
//
// 2KB-aligned trampoline vector table installed as VBAR_EL2 on CPUs that
// require the BPIALL workaround.
//
// ARM vector table layout (each slot is 128 bytes = 32 instructions):
//
//   0x000-0x180  Current EL, SP0  (el2t): standard EL2 self-trap handlers
//   0x200-0x380  Current EL, SPx  (el2h): standard EL2 self-trap handlers
//   0x400-0x580  Lower EL, AArch64 (lel64 / guest64):
//                  sync  -> b bpiall_workaround
//                  others -> delegate to base handlers
//   0x600-0x780  Lower EL, AArch32 (lel32 / guest32):
//                  sync  -> b bpiall_workaround
//                  others -> delegate to base handlers
// ===========================================================================
	.section	.text.vectors.bpiall
	.balign		2048
.global vcpu_aarch64_vectors_tramp_bpiall
vcpu_aarch64_vectors_tramp_bpiall:

// EL2 vectors (0x000-0x380): standard EL2 self-trap handlers.
// Fills exactly 8 x 128 = 1024 bytes, placing guest vectors at 0x400.
	el2_vectors vcpu_trap_bpiall 1

// Lower EL AArch64 vectors (0x400-0x580):
// guest64_sync: branch to bpiall_workaround.  All others delegate.
vector vector_guest64_sync_vcpu_tramp_bpiall
	bpiall_workaround
vector_end vector_guest64_sync_vcpu_tramp_bpiall

vector vector_guest64_irq_vcpu_tramp_bpiall
	b	vector_guest64_irq
vector_end vector_guest64_irq_vcpu_tramp_bpiall

vector vector_guest64_fiq_vcpu_tramp_bpiall
	b	vector_guest64_fiq
vector_end vector_guest64_fiq_vcpu_tramp_bpiall

vector vector_guest64_serror_vcpu_tramp_bpiall
	b	vector_guest64_serror
vector_end vector_guest64_serror_vcpu_tramp_bpiall

// Lower EL AArch32 vectors (0x600-0x780):
// guest32_sync: branch to bpiall_workaround.  All others delegate.
vector vector_guest32_sync_vcpu_tramp_bpiall
	bpiall_workaround
vector_end vector_guest32_sync_vcpu_tramp_bpiall

vector vector_guest32_irq_vcpu_tramp_bpiall
	b	vector_guest32_irq
vector_end vector_guest32_irq_vcpu_tramp_bpiall

vector vector_guest32_fiq_vcpu_tramp_bpiall
	b	vector_guest32_fiq
vector_end vector_guest32_fiq_vcpu_tramp_bpiall

vector vector_guest32_serror_vcpu_tramp_bpiall
	b	vector_guest32_serror
vector_end vector_guest32_serror_vcpu_tramp_bpiall

// ===========================================================================
// bpiall_wa_exit_vectors
//
// Temporary VBAR_EL2 active only during the brief AArch32 EL1 window.
// Only the lower-EL-AArch32-sync entry (offset 0x600) is expected to fire
// (triggered by the HVC in bpiall_wa_code).  All other entries panic.
// ===========================================================================
	.section	.text.vectors.bpiall
	.balign	2048
bpiall_wa_exit_vectors:

vector vector_el2t_sync_bpiall_wa
	save_kernel_context_full
	panic	"Unexpected EL2t sync during BPIALL WA"
vector_end vector_el2t_sync_bpiall_wa

vector vector_el2t_irq_bpiall_wa
	save_kernel_context_full
	panic	"Unexpected EL2t IRQ during BPIALL WA"
vector_end vector_el2t_irq_bpiall_wa

vector vector_el2t_fiq_bpiall_wa
	save_kernel_context_full
	panic	"Unexpected EL2t FIQ during BPIALL WA"
vector_end vector_el2t_fiq_bpiall_wa

vector vector_el2t_serror_bpiall_wa
	save_kernel_context_full
	panic	"Unexpected EL2t SError during BPIALL WA"
vector_end vector_el2t_serror_bpiall_wa

vector vector_el2h_sync_bpiall_wa
	save_kernel_context_full
	panic	"Unexpected EL2h sync during BPIALL WA"
vector_end vector_el2h_sync_bpiall_wa

vector vector_el2h_irq_bpiall_wa
	save_kernel_context_full
	panic	"Unexpected EL2h IRQ during BPIALL WA"
vector_end vector_el2h_irq_bpiall_wa

vector vector_el2h_fiq_bpiall_wa
	save_kernel_context_full
	panic	"Unexpected EL2h FIQ during BPIALL WA"
vector_end vector_el2h_fiq_bpiall_wa

vector vector_el2h_serror_bpiall_wa
	save_kernel_context_full
	panic	"Unexpected EL2h SError during BPIALL WA"
vector_end vector_el2h_serror_bpiall_wa

vector vector_lel64_sync_bpiall_wa
	save_kernel_context_full
	panic	"Unexpected 64-bit guest sync during BPIALL WA"
vector_end vector_lel64_sync_bpiall_wa

vector vector_lel64_irq_bpiall_wa
	save_kernel_context_full
	panic	"Unexpected 64-bit guest IRQ during BPIALL WA"
vector_end vector_lel64_irq_bpiall_wa

vector vector_lel64_fiq_bpiall_wa
	save_kernel_context_full
	panic	"Unexpected 64-bit guest FIQ during BPIALL WA"
vector_end vector_lel64_fiq_bpiall_wa

vector vector_lel64_serror_bpiall_wa
	save_kernel_context_full
	panic	"Unexpected 64-bit guest SError during BPIALL WA"
vector_end vector_lel64_serror_bpiall_wa

// offset 0x600: expected HVC return path.
vector vector_lel32_sync_bpiall_wa
	b	bpiall_wa_restore_and_dispatch
vector_end vector_lel32_sync_bpiall_wa

vector vector_lel32_irq_bpiall_wa
	save_kernel_context_full
	panic	"Unexpected 32-bit guest IRQ during BPIALL WA"
vector_end vector_lel32_irq_bpiall_wa

vector vector_lel32_fiq_bpiall_wa
	save_kernel_context_full
	panic	"Unexpected 32-bit guest FIQ during BPIALL WA"
vector_end vector_lel32_fiq_bpiall_wa

vector vector_lel32_serror_bpiall_wa
	save_kernel_context_full
	panic	"Unexpected 32-bit guest SError during BPIALL WA"
vector_end vector_lel32_serror_bpiall_wa
