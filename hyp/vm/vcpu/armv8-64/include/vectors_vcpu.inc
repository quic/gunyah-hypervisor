// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Make sure the structure layouts haven't changed
#if (((OFS_VCPU_GPR_X(30) + 8) != OFS_VCPU_GPR_PC) ||   \
     ((OFS_VCPU_GPR_PC + 8) != OFS_VCPU_GPR_SPSR_EL2))
#error The layout of vcpu_gpr_t has changed
#endif

#if defined(ARCH_ARM_8_3_PAUTH)
// Define symbols for pointer auth offsets so we can access them from macros
.equ	vcpu_pauth_tp_pauth_ofs, \
	OFS_THREAD_VCPU_REGS_PAUTH - OFS_THREAD_VCPU_REGS_GPR
.equ	pauth_DA_ofs, OFS_AARCH64_PAUTH_KEYS_DA
.equ	pauth_DB_ofs, OFS_AARCH64_PAUTH_KEYS_DB
.equ	pauth_IA_ofs, OFS_AARCH64_PAUTH_KEYS_IA
.equ	pauth_IB_ofs, OFS_AARCH64_PAUTH_KEYS_IB
.equ	pauth_GA_ofs, OFS_AARCH64_PAUTH_KEYS_GA

// Macro for saving an EL1 key and loading the corresponding EL2 key
.macro  vcpu_pauth_entry_key k:req, tp:req, kp:req, \
		tl:req, th:req, kl:req, kh:req
	mrs	\tl, AP\k\()KeyLo_EL1
	ldp	\kl, \kh, [\kp, pauth_\k\()_ofs]
	mrs	\th, AP\k\()KeyHi_EL1
	msr	AP\k\()KeyLo_EL1, \kl
	stp	\tl, \th, [\tp, vcpu_pauth_tp_pauth_ofs + pauth_\k\()_ofs]
	msr	AP\k\()KeyHi_EL1, \kh
.endm

// Macro for loading an EL1 key
.macro  vcpu_pauth_exit_key k:req, tp:req, tl:req, th:req
	ldp	\tl, \th, [\tp, vcpu_pauth_tp_pauth_ofs + pauth_\k\()_ofs]
	msr	AP\k\()KeyLo_EL1, \tl
	msr	AP\k\()KeyHi_EL1, \th
.endm
#endif

.macro  vcpu_pauth_entry tp:req, kp:req, tl:req, th:req, kl:req, kh:req
#if defined(ARCH_ARM_8_3_PAUTH)
	thread_get_self	\tp, offset=OFS_THREAD_VCPU_REGS_GPR
	vcpu_pauth_entry_thread \tp, \kp, \tl, \th, \kl, \kh
#endif
.endm

.macro  vcpu_pauth_entry_thread tp:req, kp:req, tl:req, th:req, kl:req, kh:req
#if defined(ARCH_ARM_8_3_PAUTH)
	adrl	\kp, aarch64_pauth_keys
	vcpu_pauth_entry_key DA, \tp, \kp, \tl, \th, \kl, \kh
	vcpu_pauth_entry_key DB, \tp, \kp, \tl, \th, \kl, \kh
	vcpu_pauth_entry_key IA, \tp, \kp, \tl, \th, \kl, \kh
	vcpu_pauth_entry_key IB, \tp, \kp, \tl, \th, \kl, \kh
	vcpu_pauth_entry_key GA, \tp, \kp, \tl, \th, \kl, \kh
	isb
#endif
.endm

.macro  vcpu_pauth_exit tp:req, tl:req, th:req, spsr:req, tmp:req
#if defined(ARCH_ARM_8_3_PAUTH)
	thread_get_self	\tp, offset=OFS_THREAD_VCPU_REGS_GPR
	vcpu_pauth_exit_thread \tp, \tl, \th, \spsr, \tmp
#endif
.endm

.macro  vcpu_pauth_exit_thread tp:req, tl:req, th:req, spsr:req, tmp:req
#if defined(ARCH_ARM_8_3_PAUTH)
	// There is no need to sign userspace return addresses, but we do need
	// to check that we are actually returning to userspace here: either
	// SPSR_EL2.M[4]=1 (for 32-bit mode) or SPSR_EL2.M[3]=0 (for EL1 or
	// EL0), i.e. SPSR_EL2[4:3] != 0b01.
	ubfx	\tmp, \spsr, 3, 2

	vcpu_pauth_exit_key DA, \tp, \tl, \th
	vcpu_pauth_exit_key DB, \tp, \tl, \th
	vcpu_pauth_exit_key IA, \tp, \tl, \th
	vcpu_pauth_exit_key IB, \tp, \tl, \th

	sub	\tmp, \tmp, 1

	vcpu_pauth_exit_key GA, \tp, \tl, \th

	cbz	\tmp, vcpu_pauth_exit_failed
#endif
.endm

.macro	save_guest_context_vcpu_zero_0_13_irq
	disable_phys_access

	// SP (SP_EL2) points to the kernel stack
	stp	x0, x1, [sp, -16]!		// Save X0 & X1
	thread_get_self	x0, offset=OFS_THREAD_VCPU_REGS_GPR

	stp	x2, x3, [x0, OFS_VCPU_GPR_X(2)]
	mrs	x1, SPSR_EL2
	stp	x4, x5, [x0, OFS_VCPU_GPR_X(4)]
	stp	x6, x7, [x0, OFS_VCPU_GPR_X(6)]
	clear_guest_registers "4,5"
	ldp	x2, x3, [sp], #16		// Recover X0 & X1
	stp	x8, x9, [x0, OFS_VCPU_GPR_X(8)]
	clear_guest_registers "6,7"
	str	x1, [x0, OFS_VCPU_GPR_SPSR_EL2]
	stp	x10, x11, [x0, OFS_VCPU_GPR_X(10)]
	clear_guest_registers "8,9"
	stp	x12, x13, [x0, OFS_VCPU_GPR_X(12)]
	mrs	x1, ELR_EL2
	clear_guest_registers "10,11"
	stp	x2, x3, [x0, OFS_VCPU_GPR_X(0)]
	clear_guest_registers "12,13"
	clear_guest_registers "2,3"
	stp	x30, x1, [x0, OFS_VCPU_GPR_X(30)]
	clear_guest_registers "1"
.endm

.macro	save_guest_context_vcpu_zero_14_29_irq
	stp	x14, x15, [x0, OFS_VCPU_GPR_X(14)]
	stp	x16, x17, [x0, OFS_VCPU_GPR_X(16)]
	clear_guest_registers "14,15"
	stp	x18, x19, [x0, OFS_VCPU_GPR_X(18)]
	clear_guest_registers "16,17"
	stp	x20, x21, [x0, OFS_VCPU_GPR_X(20)]
	clear_guest_registers "18,19"
	stp	x22, x23, [x0, OFS_VCPU_GPR_X(22)]
	clear_guest_registers "20,21"
	stp	x24, x25, [x0, OFS_VCPU_GPR_X(24)]
	clear_guest_registers "22,23"
	stp	x26, x27, [x0, OFS_VCPU_GPR_X(26)]
	clear_guest_registers "24"
	stp	x28, x29, [x0, OFS_VCPU_GPR_X(28)]

	vcpu_pauth_entry_thread x0, x25, x26, x27, x28, x29
	clear_guest_registers "25,26,27,28,29"
.endm

.macro	save_guest_exception_context_zero_1_29
	// SP (SP_EL2) points to the kernel stack
	stp	x0, x1, [sp, -16]!		// Save X0 & X1
	thread_get_self	x0, offset=OFS_THREAD_VCPU_REGS_GPR

	stp	x2, x3, [x0, OFS_VCPU_GPR_X(2)]
	mrs	x1, SPSR_EL2
	stp	x4, x5, [x0, OFS_VCPU_GPR_X(4)]
	stp	x6, x7, [x0, OFS_VCPU_GPR_X(6)]
	clear_guest_registers "4,5"
	ldp	x2, x3, [sp], #16		// Recover X0 & X1
	stp	x8, x9, [x0, OFS_VCPU_GPR_X(8)]
	clear_guest_registers "6,7"
	str	x1, [x0, OFS_VCPU_GPR_SPSR_EL2]
	stp	x10, x11, [x0, OFS_VCPU_GPR_X(10)]
	clear_guest_registers "8,9"
	stp	x12, x13, [x0, OFS_VCPU_GPR_X(12)]
	clear_guest_registers "10,11"
	stp	x2, x3, [x0, OFS_VCPU_GPR_X(0)]
	clear_guest_registers "12,13"
	stp	x14, x15, [x0, OFS_VCPU_GPR_X(14)]
	clear_guest_registers "2,3"
	stp	x16, x17, [x0, OFS_VCPU_GPR_X(16)]
	clear_guest_registers "14,15"
	stp	x18, x19, [x0, OFS_VCPU_GPR_X(18)]
	clear_guest_registers "16,17"
	stp	x20, x21, [x0, OFS_VCPU_GPR_X(20)]
	clear_guest_registers "18,19"
	stp	x22, x23, [x0, OFS_VCPU_GPR_X(22)]
	clear_guest_registers "20,21"
	stp	x24, x25, [x0, OFS_VCPU_GPR_X(24)]
	mrs	x1, ELR_EL2
	clear_guest_registers "22,23"
	stp	x26, x27, [x0, OFS_VCPU_GPR_X(26)]
	clear_guest_registers "24,25"
	stp	x28, x29, [x0, OFS_VCPU_GPR_X(28)]
	clear_guest_registers "26"
	stp	x30, x1, [x0, OFS_VCPU_GPR_X(30)]

	vcpu_pauth_entry_thread x0, x30, x27, x28, x29, x1
	clear_guest_registers "27,28,29,1"
.endm

// This macro zeros guest-vm register contents to make it harder for guest VM
// to influence branch prediction via gadgets in kernel code.
.macro	clear_guest_registers reglist
	.irp n,\reglist
	mov	x\n, xzr
	.endr
.endm
