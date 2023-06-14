// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hypconstants.h>

#include <asm/asm_defs.inc>

bss64 aarch64_kaslr_base, local
bss64 hypervisor_prng_nonce

	.section .text.boot.init

function aarch64_init_generate_seed, local, section=nosection
	// KASLR - Seed is passed in x0-x3, however, for portability it may not
	// be random. Mix in some values from registers that are UNKNOWN after
	// reset: TPIDR_EL2, TTBR0_EL2, MAIR_EL2, VBAR_EL2 and FAR_EL2.
	abs64	x12, 0xb3b2c4c2400caad3L	// Large Prime
	abs64	x11, 0x4bccfdf1			// Random Constant
	mrs	x14, TPIDR_EL2
	crc32cx	w11, w11, x14
	mrs	x15, TTBR0_EL2
	crc32cx	w11, w11, x15
	mov	x13, x11
	mrs	x16, MAIR_EL2
	crc32cx	w11, w11, x16
	mrs	x17, VBAR_EL2
	crc32cx	w11, w11, x17
	mrs	x18, FAR_EL2
	crc32cx	w11, w11, x18
	eor	x13, x13, x11, ROR 32

	mul	x10, x14, x12
	rbit	x10, x10
	eor	x10, x10, x12
	mul	x10, x15, x10
	rbit	x10, x10
	eor	x10, x10, x12
	mul	x10, x16, x10
	rbit	x10, x10
	eor	x10, x10, x12
	mul	x10, x17, x10
	rbit	x10, x10
	eor	x10, x10, x12
	mul	x10, x18, x10

	// We have no persistent storage, so use chip UNKNOWN value as nonce
	adrp	x12, hypervisor_prng_nonce
	str	x13, [x12, :lo12:hypervisor_prng_nonce]

	eor	x10, x10, x10, ROR 32
	// Mix in with the platform provided seed.
	// FIXME: this should use a real hash function
	eor	x0, x0, x10
	eor	x0, x0, x11
	eor	x0, x0, x1
	eor	x0, x0, x2
	eor	x0, x0, x3
	ret
function_end aarch64_init_generate_seed

#define LOWER_ATTRS VMSA_PAGE_ENTRY_LOWER_ATTRS_SHIFT
#define UPPER_ATTRS VMSA_PAGE_ENTRY_UPPER_ATTRS_SHIFT

#define VSMAv8_LEVEL_BITS 9
#define VSMAv8_ADDRESS_BITS_LEVEL0                                             \
	(VSMAv8_ADDRESS_BITS_LEVEL1 + VSMAv8_LEVEL_BITS)
#define VSMAv8_ADDRESS_BITS_LEVEL1                                             \
	(VSMAv8_ADDRESS_BITS_LEVEL2 + VSMAv8_LEVEL_BITS)
#define VSMAv8_ADDRESS_BITS_LEVEL2                                             \
	(VSMAv8_ADDRESS_BITS_LEVEL3 + VSMAv8_LEVEL_BITS)
#define VSMAv8_ADDRESS_BITS_LEVEL3 12
#define VSMAv8_ENTRY_BITS 3
#define VSMAv8_ENTRY_SIZE (1 << VSMAv8_ENTRY_BITS)

#define VSMAv8_TABLE_TYPE 3
#define VSMAv8_BLOCK_TYPE 1

#define VSMAv8_TABLE_REFCOUNT_SHIFT VMSA_TABLE_ENTRY_REFCOUNT_SHIFT

#define VSMAv8_AP_SHIFT (LOWER_ATTRS + VMSA_STG1_LOWER_ATTRS_AP_SHIFT)
#define VSMAv8_AP_RW_EL0_NONE ENUM_VMSA_STG1_AP_EL0_NONE_UPPER_READ_WRITE
#define VSMAv8_AP_RW_EL0_RW ENUM_VMSA_STG1_AP_ALL_READ_WRITE
#define VSMAv8_AP_RO_EL0_NONE ENUM_VMSA_STG1_AP_EL0_NONE_UPPER_READ_ONLY
#define VSMAv8_AP_RO_EL0_RO ENUM_VMSA_STG1_AP_ALL_READ_ONLY

#define VSMAv8_SH_SHIFT (LOWER_ATTRS + VMSA_STG1_LOWER_ATTRS_SH_SHIFT)
#define VSMAv8_SH_NON_SHAREABLE ENUM_VMSA_SHAREABILITY_NON_SHAREABLE
#define VSMAv8_SH_OUTER ENUM_VMSA_SHAREABILITY_OUTER_SHAREABLE
#define VSMAv8_SH_INNER ENUM_VMSA_SHAREABILITY_INNER_SHAREABLE

#define VSMAv8_ATTRIDX_SHIFT (LOWER_ATTRS + VMSA_STG1_LOWER_ATTRS_ATTR_IDX_SHIFT)
#define VSMAv8_AF_SHIFT (LOWER_ATTRS + VMSA_STG1_LOWER_ATTRS_AF_SHIFT)
#define VSMAv8_NG_SHIFT (LOWER_ATTRS + VMSA_STG1_LOWER_ATTRS_NG_SHIFT)

#if defined(ARCH_ARM_FEAT_VHE)
#define VSMAv8_UXN_SHIFT (UPPER_ATTRS + VMSA_STG1_UPPER_ATTRS_UXN_SHIFT)
#define VSMAv8_PXN_SHIFT (UPPER_ATTRS + VMSA_STG1_UPPER_ATTRS_PXN_SHIFT)
#else
#define VSMAv8_XN_SHIFT (UPPER_ATTRS + VMSA_STG1_UPPER_ATTRS_XN_SHIFT)
#endif

#if defined(ARCH_ARM_FEAT_BTI)
#define VSMAv8_GP_SHIFT (UPPER_ATTRS + VMSA_STG1_UPPER_ATTRS_GP_SHIFT)
#endif

#define MAIR_DEFAULTS	0x0004080cbb4fff44
// 63:56 - Attr7 - 0x00 - Device-nGnRnE
// 55:48 - Attr6 - 0x04 - Device-nGnRE
// 47:40 - Attr5 - 0x08 - Device-nGRE
// 39:32 - Attr4 - 0x0c - Device-GRE
// 31:24 - Attr3 - 0xBB - Normal inner/outer WT/RA/WA
// 23:16 - Attr2 - 0x4F - Outer NC, Inner WB/RA/WA
// 15:8  - Attr1 - 0xFF - Normal inner/outer WB/RA/WA
// 7:0   - Attr0 - 0x44 - Normal inner/outer non-cachable.
#define MAIR_ATTRIDX_NORMAL 1

#define LOWER_ATTRIBUTES_HYP (\
		(MAIR_ATTRIDX_NORMAL << VSMAv8_ATTRIDX_SHIFT) | \
		(VSMAv8_SH_INNER << VSMAv8_SH_SHIFT) | \
		(1 << VSMAv8_AF_SHIFT) | \
		(0 << VSMAv8_NG_SHIFT))
#define LOWER_ATTRIBUTES_HYP_R (\
		LOWER_ATTRIBUTES_HYP | \
		(VSMAv8_AP_RO_EL0_NONE << VSMAv8_AP_SHIFT))
#define LOWER_ATTRIBUTES_HYP_RW ( \
		LOWER_ATTRIBUTES_HYP | \
		(VSMAv8_AP_RW_EL0_NONE << VSMAv8_AP_SHIFT))
#define UPPER_ATTRIBUTES_HYP_X \
	(0)
#define UPPER_ATTRIBUTES_HYP_NX \
	(1 << VSMAv8_PXN_SHIFT)
// (DBM << 51) | (CONTIG << 52) | (PXN << 53) | (UXN << 54)

#if defined(ARCH_ARM_FEAT_BTI)
#define GUARDED_PAGE (1 << VSMAv8_GP_SHIFT)
#endif

#define TCR_RGN ENUM_TCR_RGN_NORMAL_WRITEBACK_RA_WA
#define TCR_SH ENUM_TCR_SH_INNER_SHAREABLE
#define TCR_TG0 ENUM_TCR_TG0_GRANULE_SIZE_4KB
#define TCR_TG1 ENUM_TCR_TG1_GRANULE_SIZE_4KB

// Note: the nested macros are are needed to expand the config to a number
// before it is pasted into the enum macro name.
#define TCR_PS TCR_PS_FOR_CONFIG(PLATFORM_PHYS_ADDRESS_BITS)
#define TCR_PS_FOR_CONFIG(x) TCR_PS_FOR_SIZE(x)
#define TCR_PS_FOR_SIZE(x) ENUM_TCR_PS_SIZE_##x##BITS

#if defined(ARCH_ARM_FEAT_VHE)
#define TCR_EL2_HYP ( \
	TCR_EL2_E2H1_EPD0_MASK | \
	(HYP_T1SZ << TCR_EL2_E2H1_T1SZ_SHIFT) | \
	(0 << TCR_EL2_E2H1_A1_SHIFT) | \
	(TCR_RGN << TCR_EL2_E2H1_IRGN1_SHIFT) | \
	(TCR_RGN << TCR_EL2_E2H1_ORGN1_SHIFT) | \
	(TCR_SH << TCR_EL2_E2H1_SH1_SHIFT) | \
	(TCR_TG1 << TCR_EL2_E2H1_TG1_SHIFT) | \
	(TCR_PS << TCR_EL2_E2H1_IPS_SHIFT))
#else
#define TCR_EL2_HYP ( \
	(HYP_T0SZ << TCR_EL2_E2H0_T0SZ_SHIFT) | \
	(TCR_RGN << TCR_EL2_E2H0_IRGN0_SHIFT) | \
	(TCR_RGN << TCR_EL2_E2H0_ORGN0_SHIFT) | \
	(TCR_SH << TCR_EL2_E2H0_SH0_SHIFT) | \
	(TCR_TG0 << TCR_EL2_E2H0_TG0_SHIFT) | \
	(TCR_PS << TCR_EL2_E2H0_PS_SHIFT))
#endif

#if defined(__ARM_FEATURE_UNALIGNED)
#define SCTLR_EL2_VM_HYP_DEBUG 0
#else
// FIXME:
//#define SCTLR_EL2_VM_HYP_DEBUG SCTLR_EL2_VM_A_MASK
#define SCTLR_EL2_VM_HYP_DEBUG 0
#endif
#define SCTLR_EL2_VM_HYP ( \
		SCTLR_EL2_VM_HYP_DEBUG | \
		SCTLR_EL2_VM_M_MASK | \
		SCTLR_EL2_VM_C_MASK | \
		SCTLR_EL2_VM_SA_MASK | \
		SCTLR_EL2_VM_I_MASK | \
		SCTLR_EL2_VM_WXN_MASK)

#if defined(ARCH_ARM_FEAT_VHE)
// Enable EL2 E2H (hosted hypervisor) mode
#define HCR_EL2_HYP ( \
    HCR_EL2_FMO_MASK | HCR_EL2_IMO_MASK | HCR_EL2_AMO_MASK | \
    HCR_EL2_TGE_MASK | HCR_EL2_E2H_MASK)

// Kernel main pagetable in TTBR1_EL2
// 39 bit address space (3-level) for KASLR
// HYP_T1SZ = 64 - 39 = 25
#define HYP_T1SZ	(64 - HYP_ASPACE_HIGH_BITS)
#define HYP_LEVEL1_ALIGN	(37 - HYP_T1SZ)
#define HYP_LEVEL1_ENTRIES	(1U << (HYP_ASPACE_HIGH_BITS - 30))

#else
#define HCR_EL2_HYP ( \
    HCR_EL2_FMO_MASK | HCR_EL2_IMO_MASK | HCR_EL2_AMO_MASK | \
    HCR_EL2_TGE_MASK)

// Kernel main pagetable in TTBR0_EL2
// 39 bit address space (3-level) for KASLR
// HYP_T0SZ = 64 - 39 = 25
#define HYP_T0SZ	(64 - HYP_ASPACE_LOW_BITS)
#define HYP_LEVEL1_ALIGN	(40 - HYP_T0SZ)
#define HYP_LEVEL1_ENTRIES	(1U << (HYP_ASPACE_LOW_BITS - 30))
#endif

#define BITS_2MiB	21
#define BITS_1GiB	30
#define SIZE_2MiB	(1 << BITS_2MiB)
#define SIZE_1GiB	(1 << BITS_1GiB)
#define MASK_2MiB	(SIZE_2MiB - 1)
#define MASK_1GiB	(SIZE_1GiB - 1)


// Initialize the KASLR address
//
// This is called early in cold boot with MMU and data cache disabled. The
// stack is valid.
//
// Input / preserve registers:
//    x0-x3: KASLR seed from the platform code
//    x19-x28: preserved as per AAPCS64
// Returns:
//    x0: KASLR base
function aarch64_init_kaslr, section=nosection
	stp	x29, x30, [sp, -16]!
	mov	x29, sp

	bl	aarch64_init_generate_seed

	// To test pathological seed, define this below
#if defined(DISABLE_KASLR) && DISABLE_KASLR
	mov	x0, 0
#endif

	// Calculate KASLR virtual base address
local mm_calc_virt_base:
#if defined(ARCH_ARM_FEAT_VHE)
	mov	x10, 0xffffffffffe00000
	ubfiz	x9, x0, BITS_2MiB, (HYP_ASPACE_HIGH_BITS - BITS_2MiB)
	eor	x14, x10, x9

	// - ensure that the hypervisor image doesn't wrap
	adr	x10, image_virt_start
	bic	x10, x10, MASK_2MiB
	adrl	x11, image_virt_end
	sub	x10, x11, x10
	adds	x10, x10, x14
	bcc	1f

	// -- virtual address wraps or ends at -1, try again
	lsr	x0, x0, 18
	movk	x0, 0x5555, lsl 48
	b	LOCAL(mm_calc_virt_base)
#else
	// - KASLR base needs to be in the lower half of the the address space,
	// because the upper half is reserved for physaccess
	ubfiz	x9, x0, BITS_2MiB, (HYP_ASPACE_LOWER_HALF_BITS - BITS_2MiB)
	// Ensure the KASLR base is outside of the 1:1 area
	mov	x10, (1 << HYP_ASPACE_MAP_DIRECT_BITS)
	orr	x14, x10, x9

	// - ensure that the hypervisor image doesn't go out of the lower half of
	// the address space
	adr	x10, image_virt_start
	adrl	x11, image_virt_last
	sub	x10, x11, x10
	add	x10, x10, x14
	mov	x9, (1 << HYP_ASPACE_LOWER_HALF_BITS)
	cmp	x9, x10
	bhi	1f

	// -- virtual address overlaps the upper half, try again
	lsr	x0, x0, (HYP_ASPACE_LOWER_HALF_BITS - BITS_2MiB)
	movk	x0, 0x5555, lsl 32
	b	LOCAL(mm_calc_virt_base)
#endif

1:
	adrp	x9, aarch64_kaslr_base
	str	x14, [x9, :lo12:aarch64_kaslr_base]
	mov	x0, x14

	ldp	x29, x30, [sp], 16
	ret
function_end aarch64_init_kaslr

// Initialize the EL2 MMU.
//
// This is called early in boot with MMU and data cache disabled. The stack
// pointer may be invalid and should not be accessed.
//
// Input / preserve registers:
//    w2: bool first_boot
//    x30: Physical address of return (to be translated to virtual address)
//    x19-x28: preserved as per AAPCS64
function aarch64_init_address_space, section=nosection
	// Ensure TLB-EL2 is clean, dsb and isb deferred until before mmu enable
	tlbi	alle2

	abs64   x9, HCR_EL2_HYP
	msr	HCR_EL2, x9
	isb

	// === Setup translation registers ===
	// - setup TCR_EL2
	abs64	x28, TCR_EL2_HYP
	msr	TCR_EL2, x28
	// -- preserve x28 for use below

	// - setup MAIR_EL2
	abs64	x9, MAIR_DEFAULTS
	msr	MAIR_EL2, x9

	// - setup TTBRx_EL2 (hypervisor code / data)
	adrl	x12, aarch64_pt_ttbr_level1	// physical addresses of EL2 PT
#if defined(ARCH_ARM_FEAT_VHE)
	orr	x9, x12, TTBR1_EL2_CNP_MASK
	// -- preserve x12 for use below
	msr	TTBR1_EL2, x9
#else
	orr	x9, x12, TTBR0_EL2_CNP_MASK
	// -- preserve x12 for use below
	msr	TTBR0_EL2, x9
#endif
	isb

	// Load the KASLR base address
	adrp	x9, aarch64_kaslr_base
	ldr	x14, [x9, :lo12:aarch64_kaslr_base]

	// - calculate virtual return address
	and	x30, x30, MASK_2MiB
	add	x30, x30, x14

	// - calculate virtual address of vectors_boot_aarch64
	adrl	x9, vectors_boot_aarch64
	and	x9, x9, MASK_2MiB
	add	x9, x9, x14

	// - set the MMU-init vectors to: vectors_boot_aarch64
	// -- on enabling the MMU, the resulting prefetch abort will jump there
	msr	VBAR_EL2, x9

	// Skip the PT setup if this is not the first boot
	cbz	w2, LOCAL(aarch64_init_address_space_warm)

	// === Setup the TTBR1_EL2 mappings for the hypervisor
	// - create hypervisor text/ro 2MB mapping
	adrl	x13, aarch64_pt_ttbr_level2
	// -- calculate 2MB table entries
#if defined(ARCH_ARM_FEAT_VHE)
	ubfx	x10, x9, VSMAv8_ADDRESS_BITS_LEVEL1, HYP_ASPACE_HIGH_BITS - VSMAv8_ADDRESS_BITS_LEVEL1
#else
	ubfx	x10, x9, VSMAv8_ADDRESS_BITS_LEVEL1, HYP_ASPACE_LOW_BITS - VSMAv8_ADDRESS_BITS_LEVEL1
#endif
	add	x10, x12, x10, lsl VSMAv8_ENTRY_BITS	// x10 points to Level-1 table entry
	ubfx	x11, x9, VSMAv8_ADDRESS_BITS_LEVEL2, VSMAv8_LEVEL_BITS
	add	x11, x13, x11, lsl VSMAv8_ENTRY_BITS	// x11 points to Level-2 table entry
	// -- construct Level-1 table entry
	// Set initial entry count in the level below. Assume there's two entries.
	// Update with the actual count of entries used.
	mov	x9, (VSMAv8_TABLE_TYPE | (2 << VSMAv8_TABLE_REFCOUNT_SHIFT))
	orr	x9, x9, x13
	str	x9, [x10]
	// -- construct Level-2 2MB block entry - for hypervisor code
	adr	x12, image_virt_start		// calculate the hypervisor text physical base
	bic	x12, x12, MASK_2MiB		//
	orr	x9, x12, VSMAv8_BLOCK_TYPE
	movz	x10, LOWER_ATTRIBUTES_HYP_R
#if defined(ARCH_ARM_FEAT_BTI)
	movk	x10, ((UPPER_ATTRIBUTES_HYP_X | GUARDED_PAGE)  >> 48), LSL 48
#else
	movk	x10, (UPPER_ATTRIBUTES_HYP_X >> 48), LSL 48
#endif
	orr	x9, x9, x10
	str	x9, [x11]

	// - create hypervisor RW 2MB mapping
	adrp	x9, data_base			// calculate the hypervisor data physical base
	bic	x9, x9, MASK_2MiB		//
	add	x10, x9, x14
	sub	x15, x10, x12

	cmp	x15, x14
	b.eq	LOCAL(mm_init_failure)		// if RO and RW are on the same 2MiB page, fail
	lsr	x10, x15, BITS_1GiB
	cmp	x10, x14, lsr BITS_1GiB		// test whether RO and RW share the same level2 table
	beq	LOCAL(mm_init_rw_level2)

	// -- RW section falls on the next 1GiB page, so we need a new table
	//    entry and use the second level2 page-table
	adrl	x12, aarch64_pt_ttbr_level1
	add	x13, x13, (1 << (VSMAv8_LEVEL_BITS + VSMAv8_ENTRY_BITS))
	// -- Update entry count for first level 1 entry
#if defined(ARCH_ARM_FEAT_VHE)
	ubfx	x10, x14, VSMAv8_ADDRESS_BITS_LEVEL1, HYP_ASPACE_HIGH_BITS - VSMAv8_ADDRESS_BITS_LEVEL1
#else
	ubfx	x10, x14, VSMAv8_ADDRESS_BITS_LEVEL1, HYP_ASPACE_LOW_BITS - VSMAv8_ADDRESS_BITS_LEVEL1
#endif
	add	x10, x12, x10, lsl VSMAv8_ENTRY_BITS	// x10 points to Level-1 table entry
	ldr	x11, [x10]
	sub	x11, x11, (1 << VSMAv8_TABLE_REFCOUNT_SHIFT)
	str	x11, [x10]

	// -- calculate 2MB hyp table entries
#if defined(ARCH_ARM_FEAT_VHE)
	ubfx	x10, x15, VSMAv8_ADDRESS_BITS_LEVEL1, HYP_ASPACE_HIGH_BITS - VSMAv8_ADDRESS_BITS_LEVEL1
#else
	ubfx	x10, x15, VSMAv8_ADDRESS_BITS_LEVEL1, HYP_ASPACE_LOW_BITS - VSMAv8_ADDRESS_BITS_LEVEL1
#endif
	add	x10, x12, x10, lsl VSMAv8_ENTRY_BITS	// x10 points to Level-1 table entry
	ubfx	x11, x15, VSMAv8_ADDRESS_BITS_LEVEL2, VSMAv8_LEVEL_BITS
	add	x11, x13, x11, lsl VSMAv8_ENTRY_BITS	// x11 points to Level-2 table entry
	// -- construct Level-1 table entry
	mov	x12, (VSMAv8_TABLE_TYPE | (1 << VSMAv8_TABLE_REFCOUNT_SHIFT))
	orr	x12, x12, x13
	str	x12, [x10]

local mm_init_rw_level2:
	ubfx	x11, x15, VSMAv8_ADDRESS_BITS_LEVEL2, VSMAv8_LEVEL_BITS
	add	x11, x13, x11, lsl VSMAv8_ENTRY_BITS	// x11 points to Level-2 table entry
	orr	x9, x9, VSMAv8_BLOCK_TYPE
	movz	x10, LOWER_ATTRIBUTES_HYP_RW
#if defined(ARCH_ARM_FEAT_VHE)
	movk	x10, (UPPER_ATTRIBUTES_HYP_NX >> 48), LSL 48
#endif
	orr	x9, x9, x10
	str	x9, [x11]

local aarch64_init_address_space_warm:
	// Ensure we take the normal vector in the Data Abort
	msr	SPSel, 0

	// Flush outstanding stores and synchronize outstanding operations
	// including system register updates and TLB flushing.
	dsb	nsh
	isb

	// Setup SCTLR and Enable MMU
	abs64	x9, SCTLR_EL2_VM_HYP
	msr	SCTLR_EL2, x9
	// MMU is enabled, still 1:1
	// NOTE: We expect to fault here, and jump to the vector!
	isb

1:	// SHOULD NOT GET HERE!
	b	1b

local mm_init_failure:
1:
	wfi
	b	1b
function_end aarch64_init_address_space


.macro boot_vector name offset
	. = vectors_boot_aarch64 + \offset
vector_aarch64_\name\():
.endm


	.section .text.boot_vectors

	// Alignment for Boot Vectors
	.balign 2048

	// Vectors for booting EL2
	// w2 = bool first_boot
	// x30 = virtual return address
vectors_boot_aarch64:

boot_vector self_sync_mmu_init 0x0
	// For the first boot we must use the emergency vectors
	adr	x9, kernel_vectors_aarch64
	adr	x10, emergency_vectors_aarch64
	cmp	w2, 0
	csel	x9, x9, x10, eq
	msr	VBAR_EL2, x9
	isb
	msr	DAIFclr, 4
	ret	x30


// Hypervisor EL2 pagetables:

	.section .bss.hyp_pt.level1, "aw", @nobits
	.p2align HYP_LEVEL1_ALIGN
	.global aarch64_pt_ttbr_level1
aarch64_pt_ttbr_level1:
#if defined(ARCH_ARM_FEAT_VHE)
	.global aarch64_pt_ttbr1_level1
aarch64_pt_ttbr1_level1:
#else
	.global aarch64_pt_ttbr0_level1
aarch64_pt_ttbr0_level1:
#endif
	.space	HYP_LEVEL1_ENTRIES * 8

	// We have two 4KB level2 tables here, to handle the case where the
	// code/ro and rw data mappings are across a 1GiB boundary (separate
	// level 1 entries).
	.section .bss.hyp.pt.level2, "aw", @nobits
	.balign 4096
aarch64_pt_ttbr_level2:
	.space (1 << (VSMAv8_LEVEL_BITS + VSMAv8_ENTRY_BITS)) * 2
