// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Make sure the structure layouts haven't changed
#if (((OFS_VCPU_GPR_X(30) + 8) != OFS_VCPU_GPR_PC) ||   \
     ((OFS_VCPU_GPR_PC + 8) != OFS_VCPU_GPR_SPSR_EL2))
#error The layout of regs_gpr structure has changed
#endif

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
	clear_guest_registers "24,25"
	stp	x28, x29, [x0, OFS_VCPU_GPR_X(28)]
	clear_guest_registers "26,27"
	clear_guest_registers "28,29"
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
	clear_guest_registers "26,27"
	stp	x30, x1, [x0, OFS_VCPU_GPR_X(30)]
	clear_guest_registers "28,29"
	clear_guest_registers "1"
.endm

// This macro zeros guest-vm register contents to make it harder for guest VM
// to influence branch prediction via gadgets in kernel code.
.macro	clear_guest_registers reglist
	.irp n,\reglist
	mov	x\n, xzr
	.endr
.endm
