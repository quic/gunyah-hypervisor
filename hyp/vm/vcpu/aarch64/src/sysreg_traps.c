// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <log.h>
#include <preempt.h>
#include <thread.h>
#include <trace.h>
#include <vcpu.h>
#if defined(ARCH_ARM_FEAT_MPAM)
#include <arm_mpam.h>
#endif
#if defined(ARCH_ARM_FEAT_MTE)
#include <arm_mte.h>
#endif

#include <asm/sysregs.h>
#include <asm/system_registers.h>

#include "event_handlers.h"

static_assert((ARCH_AARCH64_32BIT_EL1 && ARCH_AARCH64_32BIT_EL0) ||
		      !ARCH_AARCH64_32BIT_EL1,
	      "32BIT_EL1 implies 32BIT_EL0");

#if defined(ARCH_ARM_FEAT_SHA512) != defined(ARCH_ARM_FEAT_SHA3)
#error ARCH_ARM_FEAT_SHA512 and ARCH_ARM_FEAT_SHA3 mismatch
#endif

#if SCHEDULER_CAN_MIGRATE
static bool
read_virtual_id_register(ESR_EL2_ISS_MSR_MRS_t iss, uint8_t reg_num)
{
	register_t reg_val = 0U;
	bool	   handled = true;
	thread_t  *thread  = thread_get_self();

	switch (ESR_EL2_ISS_MSR_MRS_raw(iss)) {
	// Trapped with HCR_EL2.TID1
	case ISS_MRS_MSR_REVIDR_EL1:
		// RAZ
		break;
	case ISS_MRS_MSR_AIDR_EL1:
		// RAZ
		break;
	// Trapped with HCR_EL2.TID2
	// Trapped with HCR_EL2.TID3
	case ISS_MRS_MSR_MVFR0_EL1:
		// TODO - it is possible that not all cores support the same
		// features. For non-pinned vcpus, we return the HW MVFRx_EL1
		// values, which has potential to return incorrect values.  If
		// this becomes a problem, we need to define a subset ID value
		// per machine.
#if ARCH_AARCH64_32BIT_EL0 && ARCH_AARCH64_32BIT_EL0_ALL_CORES
		sysreg64_read(MVFR0_EL1, reg_val);
#else
		reg_val = 0U; // Return defined as UNKNOWN
#endif
		break;
	case ISS_MRS_MSR_MVFR1_EL1:
#if ARCH_AARCH64_32BIT_EL0 && ARCH_AARCH64_32BIT_EL0_ALL_CORES
		sysreg64_read(MVFR1_EL1, reg_val);
#else
		reg_val = 0U; // Return defined as UNKNOWN
#endif
		break;
	case ISS_MRS_MSR_MVFR2_EL1:
#if ARCH_AARCH64_32BIT_EL0 && ARCH_AARCH64_32BIT_EL0_ALL_CORES
		sysreg64_read(MVFR2_EL1, reg_val);
#else
		reg_val = 0U; // Return defined as UNKNOWN
#endif
		break;
	case ISS_MRS_MSR_ID_AA64PFR0_EL1: {
		ID_AA64PFR0_EL1_t pfr0 = ID_AA64PFR0_EL1_default();
#if ARCH_AARCH64_32BIT_EL0 && ARCH_AARCH64_32BIT_EL0_ALL_CORES
		ID_AA64PFR0_EL1_set_EL0(&pfr0, 2U);
#else
		ID_AA64PFR0_EL1_set_EL0(&pfr0, 1U);
#endif
#if ARCH_AARCH64_32BIT_EL1
		ID_AA64PFR0_EL1_set_EL1(&pfr0, 2U);
#else
		ID_AA64PFR0_EL1_set_EL1(&pfr0, 1U);
#endif
		ID_AA64PFR0_EL1_set_EL2(&pfr0, 1U);
		ID_AA64PFR0_EL1_set_EL3(&pfr0, 1U);
#if defined(ARCH_ARM_FEAT_FP16)
		ID_AA64PFR0_EL1_set_FP(&pfr0, 1U);
		ID_AA64PFR0_EL1_set_AdvSIMD(&pfr0, 1U);
#endif
		ID_AA64PFR0_EL1_set_GIC(&pfr0, 1U);

		if (vcpu_option_flags_get_ras_error_handler(
			    &thread->vcpu_options)) {
#if defined(ARCH_ARM_FEAT_RASv1p1)
			ID_AA64PFR0_EL1_set_RAS(&pfr0, 2);
#elif defined(ARCH_ARM_FEAT_RAS)
			ID_AA64PFR0_EL1_set_RAS(&pfr0, 1);
#else
			// Nothing to do, the field is already 0
#endif
		}

#if defined(ARCH_ARM_FEAT_SEL2)
		ID_AA64PFR0_EL1_set_SEL2(&pfr0, 1U);
#endif
#if defined(ARCH_ARM_FEAT_DIT)
		ID_AA64PFR0_EL1_set_DIT(&pfr0, 1U);
#endif
#if defined(ARCH_ARM_HAVE_SCXT) &&                                             \
	(defined(ARCH_ARM_FEAT_CSV2_2) || defined(ARCH_ARM_FEAT_CSV2_3))
		if (vcpu_runtime_flags_get_scxt_allowed(&thread->vcpu_flags)) {
#if defined(ARCH_ARM_FEAT_CSV2_3)
			ID_AA64PFR0_EL1_set_CSV2(&pfr0, 3U);
#else
			ID_AA64PFR0_EL1_set_CSV2(&pfr0, 2U);
#endif
		} else {
			ID_AA64PFR0_EL1_set_CSV2(&pfr0, 1U);
		}
#elif defined(ARCH_ARM_FEAT_CSV2)
		ID_AA64PFR0_EL1_set_CSV2(&pfr0, 1U);
#endif
#if defined(ARCH_ARM_FEAT_CSV3)
		ID_AA64PFR0_EL1_set_CSV3(&pfr0, 1U);
#endif
#if defined(ARCH_ARM_FEAT_MPAM)
		if (arm_mpam_is_allowed() &&
		    vcpu_option_flags_get_mpam_allowed(&thread->vcpu_options)) {
			ID_AA64PFR0_EL1_t hw_pfr0 =
				register_ID_AA64PFR0_EL1_read();
			ID_AA64PFR0_EL1_copy_MPAM(&pfr0, &hw_pfr0);
		}
#endif
		reg_val = ID_AA64PFR0_EL1_raw(pfr0);
		break;
	}
	case ISS_MRS_MSR_ID_AA64PFR1_EL1: {
		ID_AA64PFR1_EL1_t pfr1 = ID_AA64PFR1_EL1_default();
#if defined(ARCH_ARM_FEAT_BTI)
		ID_AA64PFR1_EL1_set_BT(&pfr1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_SSBS)
#if defined(ARCH_ARM_FEAT_SSBS_MSR_MRS)
		ID_AA64PFR1_EL1_set_SSBS(&pfr1, 2U);
#else
		ID_AA64PFR1_EL1_set_SSBS(&pfr1, 1U);
#endif
#endif
#if defined(ARCH_ARM_HAVE_SCXT) && defined(ARCH_ARM_FEAT_CSV2_1p2)
		if (vcpu_runtime_flags_get_scxt_allowed(&thread->vcpu_flags)) {
			ID_AA64PFR1_EL1_set_CSV2_frac(&pfr0, 2U);
		} else {
			ID_AA64PFR1_EL1_set_CSV2_frac(&pfr0, 1U);
		}
#elif defined(ARCH_ARM_FEAT_CSV2)
#if defined(ARCH_ARM_FEAT_CSV2_1p1) || defined(ARCH_ARM_FEAT_CSV2_1p2)
		ID_AA64PFR1_EL1_set_CSV2_frac(&pfr0, 1U);
#endif
#endif
#if defined(ARCH_ARM_FEAT_MTE)
		if (arm_mte_is_allowed()) {
			ID_AA64PFR1_EL1_t hw_pfr1 =
				register_ID_AA64PFR1_EL1_read();
			ID_AA64PFR1_EL1_copy_MTE(&pfr1, &hw_pfr1);
		}
#endif
#if defined(ARCH_ARM_FEAT_MPAM)
		if (arm_mpam_is_allowed() &&
		    vcpu_option_flags_get_mpam_allowed(&thread->vcpu_options)) {
			ID_AA64PFR1_EL1_t hw_pfr1 =
				register_ID_AA64PFR1_EL1_read();
			ID_AA64PFR1_EL1_copy_MPAM_frac(&pfr1, &hw_pfr1);
		}
#endif
		reg_val = ID_AA64PFR1_EL1_raw(pfr1);
		break;
	}
	case ISS_MRS_MSR_ID_AA64ISAR0_EL1: {
		ID_AA64ISAR0_EL1_t isar0 = ID_AA64ISAR0_EL1_default();
#if defined(ARCH_ARM_FEAT_PMULL)
		ID_AA64ISAR0_EL1_set_AES(&isar0, 2U);
#elif defined(ARCH_ARM_FEAT_AES)
		ID_AA64ISAR0_EL1_set_AES(&isar0, 1U);
#endif
#if defined(ARCH_ARM_FEAT_SHA1)
		ID_AA64ISAR0_EL1_set_SHA1(&isar0, 1U);
#endif
#if defined(ARCH_ARM_FEAT_SHA512)
		ID_AA64ISAR0_EL1_set_SHA2(&isar0, 2U);
#elif defined(ARCH_ARM_FEAT_SHA256)
		ID_AA64ISAR0_EL1_set_SHA2(&isar0, 1U);
#endif
#if defined(ARCH_ARM_FEAT_CRC32)
		ID_AA64ISAR0_EL1_set_CRC32(&isar0, 1U);
#endif
#if defined(ARCH_ARM_FEAT_VHE)
		ID_AA64ISAR0_EL1_set_Atomic(&isar0, 2U);
#endif
#if defined(ARCH_ARM_FEAT_RDM)
		ID_AA64ISAR0_EL1_set_RDM(&isar0, 1U);
#endif
#if defined(ARCH_ARM_FEAT_SHA3)
		ID_AA64ISAR0_EL1_set_SHA3(&isar0, 1U);
#endif
#if defined(ARCH_ARM_FEAT_SM3)
		ID_AA64ISAR0_EL1_set_SM3(&isar0, 1U);
#endif
#if defined(ARCH_ARM_FEAT_SM4)
		ID_AA64ISAR0_EL1_set_SM4(&isar0, 1U);
#endif
#if defined(ARCH_ARM_FEAT_DotProd)
		ID_AA64ISAR0_EL1_set_DP(&isar0, 1U);
#endif
#if defined(ARCH_ARM_FEAT_FHM)
		ID_AA64ISAR0_EL1_set_FHM(&isar0, 1U);
#endif
#if defined(ARCH_ARM_FEAT_FlagM2)
		ID_AA64ISAR0_EL1_set_TS(&isar0, 2U);
#elif defined(ARCH_ARM_FEAT_FlagM)
		ID_AA64ISAR0_EL1_set_TS(&isar0, 1U);
#endif
#if defined(ARCH_ARM_FEAT_TLBIRANGE)
		ID_AA64ISAR0_EL1_set_TLB(&isar0, 2U);
#elif defined(ARCH_ARM_FEAT_TLBIOS)
		ID_AA64ISAR0_EL1_set_TLB(&isar0, 1U);
#endif
#if defined(ARCH_ARM_FEAT_RNG)
		ID_AA64ISAR0_EL1_set_RNDR(&isar0, 2U);
#endif
		reg_val = ID_AA64ISAR0_EL1_raw(isar0);
		break;
	}
	case ISS_MRS_MSR_ID_AA64ISAR1_EL1: {
		ID_AA64ISAR1_EL1_t isar1    = ID_AA64ISAR1_EL1_default();
		ID_AA64ISAR1_EL1_t hw_isar1 = register_ID_AA64ISAR1_EL1_read();
		(void)hw_isar1;
#if defined(ARCH_ARM_FEAT_DPB2)
		ID_AA64ISAR1_EL1_set_DPB(&isar1, 2U);
#elif defined(ARCH_ARM_FEAT_DPB)
		ID_AA64ISAR1_EL1_set_DPB(&isar1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_JSCVT)
		ID_AA64ISAR1_EL1_set_JSCVT(&isar1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_FCMA)
		ID_AA64ISAR1_EL1_set_FCMA(&isar1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_LRCPC2)
		ID_AA64ISAR1_EL1_set_LRCPC(&isar1, 2U);
#elif defined(ARCH_ARM_FEAT_LRCPC)
		ID_AA64ISAR1_EL1_set_LRCPC(&isar1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_FRINTTS)
		ID_AA64ISAR1_EL1_set_FRINTTS(&isar1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_SB)
		ID_AA64ISAR1_EL1_set_SB(&isar1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_SPECRES)
		ID_AA64ISAR1_EL1_set_SPECRES(&isar1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_PAuth)
		ID_AA64ISAR1_EL1_copy_APA(&isar1, &hw_isar1);
		ID_AA64ISAR1_EL1_copy_API(&isar1, &hw_isar1);
		ID_AA64ISAR1_EL1_copy_GPA(&isar1, &hw_isar1);
		ID_AA64ISAR1_EL1_copy_GPI(&isar1, &hw_isar1);
#endif
#if defined(ARCH_ARM_FEAT_DGH)
		ID_AA64ISAR1_EL1_set_DGH(&isar1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_BF16)
		ID_AA64ISAR1_EL1_set_BF16(&isar1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_I8MM)
		ID_AA64ISAR1_EL1_set_I8MM(&isar1, 1U);
#endif
		reg_val = ID_AA64ISAR1_EL1_raw(isar1);
		break;
	}
	case ISS_MRS_MSR_ID_AA64ISAR2_EL1: {
		ID_AA64ISAR2_EL1_t isar2    = ID_AA64ISAR2_EL1_default();
		ID_AA64ISAR2_EL1_t hw_isar2 = register_ID_AA64ISAR2_EL1_read();
		(void)hw_isar2;
#if defined(ARCH_ARM_FEAT_PAuth)
		ID_AA64ISAR2_EL1_copy_APA3(&isar2, &hw_isar2);
		ID_AA64ISAR2_EL1_copy_GPA3(&isar2, &hw_isar2);
		ID_AA64ISAR2_EL1_copy_PAC_frac(&isar2, &hw_isar2);
#endif
#if defined(ARCH_ARM_FEAT_CLRBHB)
		ID_AA64ISAR2_EL1_copy_CLRBHB(&isar2, &hw_isar2);
#endif
#if defined(ARCH_ARM_FEAT_WFxT)
		// Copy the hardware values across once FEAT_WFxT is implemented
		// FIXME:
		// ID_AA64ISAR2_EL1_copy_WFxT(&isar2, &hw_isar2);
#endif

		reg_val = ID_AA64ISAR2_EL1_raw(isar2);
		break;
	}

	case ISS_MRS_MSR_ID_AA64MMFR0_EL1: {
		ID_AA64MMFR0_EL1_t mmfr0 = ID_AA64MMFR0_EL1_default();

		// FIXME: match PLATFORM_VM_ADDRESS_SPACE_BITS
		ID_AA64MMFR0_EL1_set_PARange(&mmfr0, TCR_PS_SIZE_36BITS);
#if defined(ARCH_AARCH64_ASID16)
		ID_AA64MMFR0_EL1_set_ASIDBits(&mmfr0, 2U);
#endif
		ID_AA64MMFR0_EL1_set_SNSMem(&mmfr0, 1U);
#if defined(ARCH_AARCH64_BIG_END_ALL_CORES) && ARCH_AARCH64_BIG_END_ALL_CORES
		ID_AA64MMFR0_EL1_set_BigEnd(&mmfr0, 1U);
		ID_AA64MMFR0_EL1_set_BigEndEL0(&mmfr0, 0U);
#elif defined(ARCH_AARCH64_BIG_END_EL0_ALL_CORES) &&                           \
	ARCH_AARCH64_BIG_END_EL0_ALL_CORES
		ID_AA64MMFR0_EL1_set_BigEnd(&mmfr0, 0U);
		ID_AA64MMFR0_EL1_set_BigEndEL0(&mmfr0, 1U);
#endif
		ID_AA64MMFR0_EL1_set_TGran4(&mmfr0, 0U);
		ID_AA64MMFR0_EL1_set_TGran16(&mmfr0, 0U);
		ID_AA64MMFR0_EL1_set_TGran64(&mmfr0, 0xfU);
#if defined(ARCH_ARM_FEAT_ExS)
		ID_AA64MMFR0_EL1_set_ExS(&mmfr0, 1U);
#endif
#if defined(ARCH_ARM_FEAT_ECV)
		ID_AA64MMFR0_EL1_set_ECV(&mmfr0, 1U);
#endif

		reg_val = ID_AA64MMFR0_EL1_raw(mmfr0);
		break;
	}
	case ISS_MRS_MSR_ID_AA64MMFR1_EL1: {
		ID_AA64MMFR1_EL1_t hw_mmfr1 = register_ID_AA64MMFR1_EL1_read();
		ID_AA64MMFR1_EL1_t mmfr1    = ID_AA64MMFR1_EL1_default();
#if defined(ARCH_ARM_FEAT_HPDS2)
		ID_AA64MMFR1_EL1_set_HPDS(&mmfr1, 2U);
#elif defined(ARCH_ARM_FEAT_HPDS)
		ID_AA64MMFR1_EL1_set_HPDS(&mmfr1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_HAFDBS)
		ID_AA64MMFR1_EL1_copy_HAFDBS(&mmfr1, &hw_mmfr1);
#endif
#if defined(ARCH_ARM_FEAT_VMID16)
		ID_AA64MMFR1_EL1_set_VMIDBits(&mmfr1, 2U);
#endif
#if defined(ARCH_ARM_FEAT_VHE)
		ID_AA64MMFR1_EL1_set_VH(&mmfr1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_LOR)
		ID_AA64MMFR1_EL1_set_LO(&mmfr1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_PAN3)
		ID_AA64MMFR1_EL1_set_PAN(&mmfr1, 3U);
#elif defined(ARCH_ARM_FEAT_PAN2) // now known as FEAT_PAN2
		ID_AA64MMFR1_EL1_set_PAN(&mmfr1, 2U);
#elif defined(ARCH_ARM_FEAT_PAN)
		ID_AA64MMFR1_EL1_set_PAN(&mmfr1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_XNX)
		ID_AA64MMFR1_EL1_set_XNX(&mmfr1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_TWED)
		ID_AA64MMFR1_EL1_set_TWED(&mmfr1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_ETS)
		ID_AA64MMFR1_EL1_set_ETS(&mmfr1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_HCX)
		ID_AA64MMFR1_EL1_set_HCX(&mmfr1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_AFP)
		ID_AA64MMFR1_EL1_set_AFP(&mmfr1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_nTLBPA)
		ID_AA64MMFR1_EL1_set_nTLBPAwAFP(&mmfr1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_TIDCP1)
		ID_AA64MMFR1_EL1_set_TIDCP1(&mmfr1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_CMOW)
		ID_AA64MMFR1_EL1_set_CMOW(&mmfr1, 1U);
#endif
#if defined(ARCH_ARM_FEAT_ECBHB)
		ID_AA64MMFR1_EL1_copy_ECBHB(&mmfr1, &hw_mmfr1);
#endif
		reg_val = ID_AA64MMFR1_EL1_raw(mmfr1);
		(void)hw_mmfr1;
		break;
	}
	case ISS_MRS_MSR_ID_AA64MMFR2_EL1: {
		ID_AA64MMFR2_EL1_t mmfr2 = ID_AA64MMFR2_EL1_default();
#if defined(ARCH_ARM_FEAT_TTCNP)
		ID_AA64MMFR2_EL1_set_CnP(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_FEAT_UAO)
		ID_AA64MMFR2_EL1_set_UAO(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_FEAT_LSMAOC)
		ID_AA64MMFR2_EL1_set_LSM(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_FEAT_IESB)
		ID_AA64MMFR2_EL1_set_IESB(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_FEAT_LVA)
		ID_AA64MMFR2_EL1_set_VARange(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_FEAT_CCIDX)
		ID_AA64MMFR2_EL1_set_CCIDX(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_FEAT_NV2)
		ID_AA64MMFR2_EL1_set_NV(&mmfr2, 2U);
#elif defined(ARCH_ARM_FEAT_NV)
		ID_AA64MMFR2_EL1_set_NV(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_FEAT_TTST)
		ID_AA64MMFR2_EL1_set_ST(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_FEAT_LSE2)
		ID_AA64MMFR2_EL1_set_AT(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_FEAT_IDST)
		ID_AA64MMFR2_EL1_set_IDS(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_FEAT_SEL2)
		ID_AA64MMFR2_EL1_set_FWB(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_FEAT_TTL)
		ID_AA64MMFR2_EL1_set_IDS(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_FEAT_BBM) || defined(ARCH_ARM_FEAT_EVT)
		ID_AA64MMFR2_EL1_t hw_mmfr2 = register_ID_AA64MMFR2_EL1_read();
#if defined(ARCH_ARM_FEAT_BBM)
		ID_AA64MMFR2_EL1_copy_BBM(&mmfr2, &hw_mmfr2);
#endif
#if defined(ARCH_ARM_FEAT_EVT)
		ID_AA64MMFR2_EL1_copy_EVT(&mmfr2, &hw_mmfr2);
#endif
#endif
#if defined(ARCH_ARM_FEAT_E0PD)
		ID_AA64MMFR2_EL1_set_E0PD(&mmfr2, 1U);
#endif
		reg_val = ID_AA64MMFR2_EL1_raw(mmfr2);
		break;
	}
	case ISS_MRS_MSR_ID_AA64MMFR3_EL1: {
		ID_AA64MMFR3_EL1_t mmfr3    = ID_AA64MMFR3_EL1_default();
		ID_AA64MMFR3_EL1_t hw_mmfr3 = register_ID_AA64MMFR3_EL1_read();
		ID_AA64MMFR3_EL1_copy_Spec_FPACC(&mmfr3, &hw_mmfr3);
		reg_val = ID_AA64MMFR3_EL1_raw(mmfr3);
		break;
	}
	case ISS_MRS_MSR_ID_AA64MMFR4_EL1:
		reg_val = 0;
		break;
	case ISS_MRS_MSR_ID_PFR0_EL1: {
		ID_PFR0_EL1_t pfr0 = ID_PFR0_EL1_default();
		ID_PFR0_EL1_set_State0(&pfr0, 1U);
		ID_PFR0_EL1_set_State1(&pfr0, 3U);
		ID_PFR0_EL1_set_State2(&pfr0, 1U);
#if defined(ARCH_ARM_HAVE_SCXT) &&                                             \
	(defined(ARCH_ARM_FEAT_CSV2_2) || defined(ARCH_ARM_FEAT_CSV2_3))
		if (vcpu_runtime_flags_get_scxt_allowed(&thread->vcpu_flags)) {
			// At the time of writing, ARM does not have CSV2_3
			// encoding for ID_PFR0_EL1.CSV2
			ID_PFR0_EL1_set_CSV2(&pfr0, 2U);
		} else {
			ID_PFR0_EL1_set_CSV2(&pfr0, 1U);
		}
#elif defined(ARCH_ARM_FEAT_CSV2)
		ID_PFR0_EL1_set_CSV2(&pfr0, 1U);
#endif
#if defined(ARCH_ARM_FEAT_DIT)
		ID_PFR0_EL1_set_DIT(&pfr0, 1U);
#endif
		if (vcpu_option_flags_get_ras_error_handler(
			    &thread->vcpu_options)) {
#if defined(ARCH_ARM_FEAT_RASv1p1)
			ID_PFR0_EL1_set_RAS(&pfr0, 2);
#elif defined(ARCH_ARM_FEAT_RAS)
			ID_PFR0_EL1_set_RAS(&pfr0, 1);
#else
			// Nothing to do, the field is already 0
#endif
		}
		reg_val = ID_PFR0_EL1_raw(pfr0);
		break;
	}
	case ISS_MRS_MSR_ID_PFR1_EL1: {
		ID_PFR1_EL1_t pfr1 = ID_PFR1_EL1_default();
#if ARCH_AARCH64_32BIT_EL1
		ID_PFR1_EL1_set_ProgMod(&pfr1, 1U);
		ID_PFR1_EL1_set_Security(&pfr1, 1U);
		ID_PFR1_EL1_set_Virtualization(&pfr1, 1U);
#endif
		ID_PFR1_EL1_set_GenTimer(&pfr1, 1U);
		ID_PFR1_EL1_set_GIC(&pfr1, 1U);
		reg_val = ID_PFR1_EL1_raw(pfr1);
		break;
	}
	case ISS_MRS_MSR_ID_PFR2_EL1: {
		ID_PFR2_EL1_t pfr2 = ID_PFR2_EL1_default();
#if defined(ARCH_ARM_FEAT_CSV3)
		ID_PFR2_EL1_set_CSV3(&pfr2, 1U);
#endif
#if defined(ARCH_ARM_FEAT_SSBS)
		ID_PFR2_EL1_set_SSBS(&pfr2, 1U);
#endif
		reg_val = ID_PFR2_EL1_raw(pfr2);
		break;
	}
	case ISS_MRS_MSR_ID_DFR0_EL1: {
		ID_DFR0_EL1_t hw_dfr0 = ID_DFR0_EL1_default();
		ID_DFR0_EL1_t dfr0    = register_ID_DFR0_EL1_read();

		// The debug, trace, PMU and SPE modules must correctly support
		// the values reported by the hardware. All we do here is to
		// zero out fields for features we don't support.

#if defined(MODULE_VM_VDEBUG)
		ID_DFR0_EL1_copy_CopDbg(&dfr0, &hw_dfr0);
		ID_DFR0_EL1_copy_CopSDbg(&dfr0, &hw_dfr0);
		ID_DFR0_EL1_copy_MMapDbg(&dfr0, &hw_dfr0);
#endif
#if defined(MODULE_PLATFORM_ARM_PMU)
		ID_DFR0_EL1_copy_PerfMon(&dfr0, &hw_dfr0);
#endif

		reg_val = ID_DFR0_EL1_raw(dfr0);
		break;
	}
	case ISS_MRS_MSR_ID_ISAR0_EL1: {
		ID_ISAR0_EL1_t isar0 = ID_ISAR0_EL1_default();
		ID_ISAR0_EL1_set_BitCount(&isar0, 1U);
		ID_ISAR0_EL1_set_BitField(&isar0, 1U);
		ID_ISAR0_EL1_set_CmpBranch(&isar0, 1U);
		ID_ISAR0_EL1_set_Divide(&isar0, 2U);
		reg_val = ID_ISAR0_EL1_raw(isar0);
		break;
	}
	case ISS_MRS_MSR_ID_ISAR1_EL1: {
		ID_ISAR1_EL1_t isar1 = ID_ISAR1_EL1_default();
		ID_ISAR1_EL1_set_Except(&isar1, 1U);
		ID_ISAR1_EL1_set_Except_AR(&isar1, 1U);
		ID_ISAR1_EL1_set_Extend(&isar1, 2U);
		ID_ISAR1_EL1_set_IfThen(&isar1, 1U);
		ID_ISAR1_EL1_set_Immediate(&isar1, 1U);
		ID_ISAR1_EL1_set_Interwork(&isar1, 3U);
		ID_ISAR1_EL1_set_Jazelle(&isar1, 1U);
		reg_val = ID_ISAR1_EL1_raw(isar1);
		break;
	}
	case ISS_MRS_MSR_ID_ISAR2_EL1: {
		ID_ISAR2_EL1_t isar2 = ID_ISAR2_EL1_default();
		ID_ISAR2_EL1_set_LoadStore(&isar2, 2U);
		ID_ISAR2_EL1_set_MemHint(&isar2, 4U);
		ID_ISAR2_EL1_set_MultiAccessInt(&isar2, 0U);
		ID_ISAR2_EL1_set_Mult(&isar2, 2U);
		ID_ISAR2_EL1_set_MultS(&isar2, 3U);
		ID_ISAR2_EL1_set_MultU(&isar2, 2U);
		ID_ISAR2_EL1_set_PSR_AR(&isar2, 1U);
		ID_ISAR2_EL1_set_Reversal(&isar2, 2U);
		reg_val = ID_ISAR2_EL1_raw(isar2);
		break;
	}
	case ISS_MRS_MSR_ID_ISAR3_EL1: {
		ID_ISAR3_EL1_t isar3 = ID_ISAR3_EL1_default();
		ID_ISAR3_EL1_set_Saturate(&isar3, 1U);
		ID_ISAR3_EL1_set_SIMD(&isar3, 3U);
		ID_ISAR3_EL1_set_SVC(&isar3, 1U);
		ID_ISAR3_EL1_set_SynchPrim(&isar3, 2U);
		ID_ISAR3_EL1_set_TabBranch(&isar3, 1U);
		ID_ISAR3_EL1_set_T32Copy(&isar3, 1U);
		ID_ISAR3_EL1_set_TrueNOP(&isar3, 1U);
		reg_val = ID_ISAR3_EL1_raw(isar3);
		break;
	}
	case ISS_MRS_MSR_ID_ISAR4_EL1: {
		ID_ISAR4_EL1_t isar4 = ID_ISAR4_EL1_default();
		ID_ISAR4_EL1_set_Unpriv(&isar4, 2U);
		ID_ISAR4_EL1_set_WithShifts(&isar4, 4U);
		ID_ISAR4_EL1_set_Writeback(&isar4, 1U);
#if ARCH_AARCH64_32BIT_EL1
		ID_ISAR4_EL1_set_SMC(&isar4, 1U);
#endif
		ID_ISAR4_EL1_set_Barrier(&isar4, 1U);
		reg_val = ID_ISAR4_EL1_raw(isar4);
		break;
	}
	case ISS_MRS_MSR_ID_ISAR5_EL1: {
		ID_ISAR5_EL1_t isar5 = ID_ISAR5_EL1_default();
		ID_ISAR5_EL1_set_SEVL(&isar5, 1U);
#if defined(ARCH_ARM_FEAT_PMULL)
		ID_ISAR5_EL1_set_AES(&isar5, 2U);
#elif defined(ARCH_ARM_FEAT_AES)
		ID_ISAR5_EL1_set_AES(&isar5, 1U);
#endif
#if defined(ARCH_ARM_FEAT_SHA1)
		ID_ISAR5_EL1_set_SHA1(&isar5, 1U);
		ID_ISAR5_EL1_set_SHA2(&isar5, 1U);
#endif
#if defined(ARCH_ARM_FEAT_CRC32)
		ID_ISAR5_EL1_set_CRC32(&isar5, 1U);
#endif
#if defined(ARCH_ARM_FEAT_RDM)
		ID_ISAR5_EL1_set_RDM(&isar5, 1U);
#endif
#if defined(ARCH_ARM_FEAT_FCMA)
		ID_ISAR5_EL1_set_VCMA(&isar5, 2U);
#endif
		reg_val = ID_ISAR5_EL1_raw(isar5);
		break;
	}
	case ISS_MRS_MSR_ID_ISAR6_EL1: {
		ID_ISAR6_EL1_t isar6 = ID_ISAR6_EL1_default();
#if defined(ARCH_ARM_FEAT_JSCVT)
		ID_ISAR6_EL1_set_JSCVT(&isar6, 1U);
#endif
#if defined(ARCH_ARM_FEAT_DotProd)
		ID_ISAR6_EL1_set_DP(&isar6, 1U);
#endif
#if defined(ARCH_ARM_FEAT_FHM)
		ID_ISAR6_EL1_set_FHM(&isar6, 1U);
#endif
#if defined(ARCH_ARM_FEAT_SB)
		ID_ISAR6_EL1_set_SB(&isar6, 1U);
#endif
#if defined(ARCH_ARM_FEAT_SPECRES)
		ID_ISAR6_EL1_set_SPECRES(&isar6, 1U);
#endif
		reg_val = ID_ISAR6_EL1_raw(isar6);
		break;
	}
	case ISS_MRS_MSR_ID_MMFR0_EL1: {
		ID_MMFR0_EL1_t mmfr0 = ID_MMFR0_EL1_default();
		ID_MMFR0_EL1_set_VMSA(&mmfr0, 5U);
		ID_MMFR0_EL1_set_OuterShr(&mmfr0, 1U);
		ID_MMFR0_EL1_set_ShareLvl(&mmfr0, 1U);
		ID_MMFR0_EL1_set_AuxReg(&mmfr0, 2U);
		ID_MMFR0_EL1_set_InnerShr(&mmfr0, 1U);
		reg_val = ID_MMFR0_EL1_raw(mmfr0);
		break;
	}
	case ISS_MRS_MSR_ID_MMFR1_EL1: {
		ID_MMFR1_EL1_t mmfr1 = ID_MMFR1_EL1_default();
		ID_MMFR1_EL1_set_BPred(&mmfr1, 4U);
		reg_val = ID_MMFR1_EL1_raw(mmfr1);
		break;
	}
	case ISS_MRS_MSR_ID_MMFR2_EL1: {
		ID_MMFR2_EL1_t mmfr2 = ID_MMFR2_EL1_default();
		ID_MMFR2_EL1_set_UniTLB(&mmfr2, 6U);
		ID_MMFR2_EL1_set_MemBarr(&mmfr2, 2U);
		ID_MMFR2_EL1_set_WFIStall(&mmfr2, 1U);
		reg_val = ID_MMFR2_EL1_raw(mmfr2);
		break;
	}
	case ISS_MRS_MSR_ID_MMFR3_EL1: {
		ID_MMFR3_EL1_t mmfr3 = ID_MMFR3_EL1_default();
		ID_MMFR3_EL1_set_CMaintVA(&mmfr3, 1U);
		ID_MMFR3_EL1_set_CMaintSW(&mmfr3, 1U);
		ID_MMFR3_EL1_set_BPMaint(&mmfr3, 2U);
		ID_MMFR3_EL1_set_MaintBcst(&mmfr3, 2U);
#if defined(ARCH_ARM_FEAT_PAN3)
		ID_MMFR3_EL1_set_PAN(&mmfr3, 3U);
#elif defined(ARCH_ARM_FEAT_PAN2) // now known as FEAT_PAN2
		ID_MMFR3_EL1_set_PAN(&mmfr3, 2U);
#elif defined(ARCH_ARM_FEAT_PAN)
		ID_MMFR3_EL1_set_PAN(&mmfr3, 1U);
#endif
		ID_MMFR3_EL1_set_CohWalk(&mmfr3, 1U);
		ID_MMFR3_EL1_set_CMemSz(&mmfr3, 2U);
		reg_val = ID_MMFR3_EL1_raw(mmfr3);
		break;
	}
	case ISS_MRS_MSR_ID_MMFR4_EL1: {
		ID_MMFR4_EL1_t mmfr4 = ID_MMFR4_EL1_default();
		ID_MMFR4_EL1_set_AC2(&mmfr4, 1U);
#if defined(ARCH_ARM_FEAT_XNX)
		ID_MMFR4_EL1_set_XNX(&mmfr4, 1U);
#endif
#if defined(ARCH_ARM_FEAT_TTCNP)
		ID_MMFR4_EL1_set_CnP(&mmfr4, 1U);
#endif
#if defined(ARCH_ARM_FEAT_HPDS2)
		ID_MMFR4_EL1_set_HPDS(&mmfr4, 2U);
#elif defined(ARCH_ARM_FEAT_AA32HPD)
		ID_MMFR4_EL1_set_HPDS(&mmfr4, 1U);
#endif
#if defined(ARCH_ARM_FEAT_LSMAOC)
		ID_MMFR4_EL1_set_LSM(&mmfr4, 1U);
#endif
#if defined(ARCH_ARM_FEAT_CCIDX)
		ID_MMFR4_EL1_set_CCIDX(&mmfr4, 1U);
#endif
#if defined(ARCH_ARM_FEAT_EVT)
		ID_MMFR4_EL1_t hw_mmfr4 = register_ID_MMFR4_EL1_read();
		ID_MMFR4_EL1_copy_EVT(&mmfr4, &hw_mmfr4);
#endif
		reg_val = ID_MMFR4_EL1_raw(mmfr4);
		break;
	}
	case ISS_MRS_MSR_ID_AA64DFR1_EL1:
	case ISS_MRS_MSR_ID_AA64AFR0_EL1:
	case ISS_MRS_MSR_ID_AA64AFR1_EL1:
	case ISS_MRS_MSR_ID_AFR0_EL1:
	case ISS_MRS_MSR_ID_AA64SMFR0_EL1:
		// RAZ
		break;
	default:
		handled = false;
		break;
	}

	if (handled) {
		vcpu_gpr_write(thread, reg_num, reg_val);
	}

	return handled;
}
#endif

static vcpu_trap_result_t
default_sys_read(const ESR_EL2_ISS_MSR_MRS_t *iss, register_t *reg_val_ptr)
{
	vcpu_trap_result_t ret	   = VCPU_TRAP_RESULT_EMULATED;
	register_t	   reg_val = 0ULL;

	uint8_t opc0, opc1, crn, crm;

	opc0 = ESR_EL2_ISS_MSR_MRS_get_Op0(iss);
	opc1 = ESR_EL2_ISS_MSR_MRS_get_Op1(iss);
	crn  = ESR_EL2_ISS_MSR_MRS_get_CRn(iss);
	crm  = ESR_EL2_ISS_MSR_MRS_get_CRm(iss);

	if ((opc0 == 3U) && (opc1 == 0U) && (crn == 0U) && (crm >= 1U) &&
	    (crm <= 7U)) {
		// It is IMPLEMENTATION DEFINED whether HCR_EL2.TID3
		// traps MRS accesses to the registers in this range
		// (that have not been handled above). If we ever get
		// here print a debug message so we can investigate.
		TRACE_AND_LOG(DEBUG, DEBUG,
			      "Emulated RAZ for ID register: ISS {:#x}",
			      ESR_EL2_ISS_MSR_MRS_raw(*iss));
		reg_val = 0U;
	} else {
		ret = VCPU_TRAP_RESULT_UNHANDLED;
	}

	*reg_val_ptr = reg_val;
	return ret;
}

static register_t
sys_aa64mmfr3_read(void)
{
	register_t reg_val = 0ULL;

	ID_AA64MMFR3_EL1_t mmfr3    = ID_AA64MMFR3_EL1_default();
	ID_AA64MMFR3_EL1_t hw_mmfr3 = register_ID_AA64MMFR3_EL1_read();
	ID_AA64MMFR3_EL1_copy_Spec_FPACC(&mmfr3, &hw_mmfr3);
	reg_val = ID_AA64MMFR3_EL1_raw(mmfr3);

	return reg_val;
}

static register_t
sys_aa64mmfr2_read(void)
{
	register_t reg_val = 0ULL;

	ID_AA64MMFR2_EL1_t mmfr2 = register_ID_AA64MMFR2_EL1_read();

	mmfr2 = ID_AA64MMFR2_EL1_clean(mmfr2);

	reg_val = ID_AA64MMFR2_EL1_raw(mmfr2);

	return reg_val;
}

static register_t
sys_aa64mmfr1_read(void)
{
	register_t reg_val = 0ULL;

	ID_AA64MMFR1_EL1_t mmfr1 = register_ID_AA64MMFR1_EL1_read();

	mmfr1 = ID_AA64MMFR1_EL1_clean(mmfr1);

#if defined(ARCH_ARM_FEAT_PAN3)
	assert(ID_AA64MMFR1_EL1_get_PAN(&mmfr1) >= 3U);
	ID_AA64MMFR1_EL1_set_PAN(&mmfr1, 3U);
#elif defined(ARCH_ARM_FEAT_PAN2) // now known as FEAT_PAN2
	assert(ID_AA64MMFR1_EL1_get_PAN(&mmfr1) >= 2U);
	ID_AA64MMFR1_EL1_set_PAN(&mmfr1, 2U);
#elif defined(ARCH_ARM_FEAT_PAN)
	assert(ID_AA64MMFR1_EL1_get_PAN(&mmfr1) >= 1U);
	ID_AA64MMFR1_EL1_set_PAN(&mmfr1, 1U);
#else
	ID_AA64MMFR1_EL1_set_PAN(&mmfr1, 0U);
#endif
	reg_val = ID_AA64MMFR1_EL1_raw(mmfr1);

	return reg_val;
}

static register_t
sys_aa64mmfr0_read(void)
{
	register_t reg_val = 0ULL;

	ID_AA64MMFR0_EL1_t mmfr0 = register_ID_AA64MMFR0_EL1_read();

	mmfr0 = ID_AA64MMFR0_EL1_clean(mmfr0);

	reg_val = ID_AA64MMFR0_EL1_raw(mmfr0);

	return reg_val;
}

static register_t
sys_aa64isar2_read(void)
{
	register_t reg_val = 0ULL;

	ID_AA64ISAR2_EL1_t isar2 = register_ID_AA64ISAR2_EL1_read();

	isar2 = ID_AA64ISAR2_EL1_clean(isar2);

#if !defined(ARCH_ARM_FEAT_PAuth)
	// When PAUTH using QARMA3 is disabled, hide it from the VM
	ID_AA64ISAR2_EL1_set_APA3(&isar2, 0U);
	ID_AA64ISAR2_EL1_set_GPA3(&isar2, 0U);
	ID_AA64ISAR2_EL1_set_PAC_frac(&isar2, 0U);
#endif
#if defined(ARCH_ARM_FEAT_WFxT)
	// Remove once FEAT_WFxT is implemented
	// FIXME:
	ID_AA64ISAR2_EL1_set_WFxT(&isar2, 0U);
#endif
	reg_val = ID_AA64ISAR2_EL1_raw(isar2);

	return reg_val;
}

static register_t
sys_aa64isar1_read(void)
{
	register_t reg_val = 0ULL;

	ID_AA64ISAR1_EL1_t isar1 = register_ID_AA64ISAR1_EL1_read();

	isar1 = ID_AA64ISAR1_EL1_clean(isar1);
#if !defined(ARCH_ARM_FEAT_BF16)
	ID_AA64ISAR1_EL1_set_BF16(&isar1, 0U);
#endif
#if !defined(ARCH_ARM_FEAT_PAuth)
	// When no PAUTH is enabled, hide it from the VM
	ID_AA64ISAR1_EL1_set_APA(&isar1, 0U);
	ID_AA64ISAR1_EL1_set_API(&isar1, 0U);
	ID_AA64ISAR1_EL1_set_GPA(&isar1, 0U);
	ID_AA64ISAR1_EL1_set_GPI(&isar1, 0U);
#endif
	reg_val = ID_AA64ISAR1_EL1_raw(isar1);

	return reg_val;
}

static register_t
sys_aa64isar0_read(void)
{
	register_t reg_val = 0ULL;

	ID_AA64ISAR0_EL1_t isar0 = register_ID_AA64ISAR0_EL1_read();

	isar0 = ID_AA64ISAR0_EL1_clean(isar0);

	reg_val = ID_AA64ISAR0_EL1_raw(isar0);

	return reg_val;
}

static register_t
sys_aa64dfr0_read(const thread_t *thread)
{
	register_t reg_val = 0ULL;

	ID_AA64DFR0_EL1_t dfr0	  = ID_AA64DFR0_EL1_default();
	ID_AA64DFR0_EL1_t hw_dfr0 = register_ID_AA64DFR0_EL1_read();

	// The debug, trace, PMU and SPE modules must correctly support
	// the values reported by the hardware. All we do here is to
	// zero out fields for missing modules.

#if defined(MODULE_VM_VDEBUG)
	// Note that ARMv8-A does not allow 0 (not implemented) in this
	// field. So without this module is not really supported.
	ID_AA64DFR0_EL1_copy_DebugVer(&dfr0, &hw_dfr0);

	ID_AA64DFR0_EL1_copy_BRPs(&dfr0, &hw_dfr0);
	ID_AA64DFR0_EL1_copy_WRPs(&dfr0, &hw_dfr0);
	ID_AA64DFR0_EL1_copy_CTX_CMPs(&dfr0, &hw_dfr0);
	ID_AA64DFR0_EL1_copy_DoubleLock(&dfr0, &hw_dfr0);
#endif
#if defined(MODULE_VM_ARM_VM_PMU)
	ID_AA64DFR0_EL1_copy_PMUVer(&dfr0, &hw_dfr0);
#endif
#if defined(INTERFACE_VET)
	// Set IDs for VMs allowed to trace
	if (vcpu_option_flags_get_trace_allowed(&thread->vcpu_options)) {
#if defined(MODULE_VM_VETE)
		ID_AA64DFR0_EL1_copy_TraceVer(&dfr0, &hw_dfr0);
		ID_AA64DFR0_EL1_copy_TraceFilt(&dfr0, &hw_dfr0);
#endif
#if defined(MODULE_VM_VTRBE)
		ID_AA64DFR0_EL1_copy_TraceBuffer(&dfr0, &hw_dfr0);
#endif
	}
#else
	(void)thread;
#endif

#if defined(MODULE_SPE)
	ID_AA64DFR0_EL1_copy_PMSVer(&dfr0, &hw_dfr0);
#endif

	reg_val = ID_AA64DFR0_EL1_raw(dfr0);

	return reg_val;
}

static register_t
sys_aa64pfr1_read(const thread_t *thread)
{
	register_t reg_val = 0ULL;

	ID_AA64PFR1_EL1_t pfr1 = register_ID_AA64PFR1_EL1_read();

	pfr1 = ID_AA64PFR1_EL1_clean(pfr1);
#if defined(ARCH_ARM_FEAT_MTE)
	if (!arm_mte_is_allowed()) {
		ID_AA64PFR1_EL1_set_MTE(&pfr1, 0);
	}
#else
	ID_AA64PFR1_EL1_set_MTE(&pfr1, 0);
#endif
#if defined(ARCH_ARM_FEAT_RAS) || defined(ARCH_ARM_FEAT_RASv1p1)
	if (!vcpu_option_flags_get_ras_error_handler(&thread->vcpu_options)) {
		ID_AA64PFR1_EL1_set_RAS_frac(&pfr1, 0);
	}
#else
	(void)thread;
#endif
#if defined(ARCH_ARM_HAVE_SCXT) && defined(ARCH_ARM_FEAT_CSV2_1p2)
	if (!vcpu_option_flags_get_scxt_allowed(&thread->vcpu_options)) {
		ID_AA64PFR1_EL1_set_CSV2_frac(&pfr1, 1U);
	}
#elif defined(ARCH_ARM_FEAT_CSV2_1p1)
	ID_AA64PFR1_EL1_set_CSV2_frac(&pfr1, 1U);
#else
	ID_AA64PFR1_EL1_set_CSV2_frac(&pfr1, 0U);
	(void)thread;
#endif

#if defined(ARCH_ARM_FEAT_MPAM)
	if (!arm_mpam_is_allowed() ||
	    !vcpu_option_flags_get_mpam_allowed(&thread->vcpu_options)) {
		// No MPAM
		ID_AA64PFR1_EL1_set_MPAM_frac(&pfr1, 0);
	}
#else
	// No MPAM
	ID_AA64PFR1_EL1_set_MPAM_frac(&pfr1, 0);
	(void)thread;
#endif
	// No SME / NMI
	ID_AA64PFR1_EL1_set_SME(&pfr1, 0);
	ID_AA64PFR1_EL1_set_NMI(&pfr1, 0);

	reg_val = ID_AA64PFR1_EL1_raw(pfr1);

	return reg_val;
}

static register_t
sys_aa64pfr0_read(const thread_t *thread)
{
	register_t reg_val = 0ULL;

	ID_AA64PFR0_EL1_t pfr0 = register_ID_AA64PFR0_EL1_read();

	pfr0 = ID_AA64PFR0_EL1_clean(pfr0);
#if !ARCH_AARCH64_32BIT_EL0
	// Require EL0 to be 64-bit only, even if core supports 32-bit
	ID_AA64PFR0_EL1_set_EL0(&pfr0, 1U);
#endif
#if !ARCH_AARCH64_32BIT_EL1
	// Require EL1 to be 64-bit only, even if core supports 32-bit
	ID_AA64PFR0_EL1_set_EL1(&pfr0, 1U);
#endif
	ID_AA64PFR0_EL1_set_EL2(&pfr0, 1U);
	ID_AA64PFR0_EL1_set_EL3(&pfr0, 1U);
#if defined(ARCH_ARM_HAVE_SCXT)
	if (!vcpu_runtime_flags_get_scxt_allowed(&thread->vcpu_flags)) {
		ID_AA64PFR0_EL1_set_CSV2(&pfr0, 1U);
	}
#elif defined(ARCH_ARM_FEAT_CSV2)
	ID_AA64PFR0_EL1_set_CSV2(&pfr0, 1U);
	(void)thread;
#else
	(void)thread;
#endif

#if defined(ARCH_ARM_FEAT_MPAM)
	if (!arm_mpam_is_allowed() ||
	    !vcpu_option_flags_get_mpam_allowed(&thread->vcpu_options)) {
		// No MPAM
		ID_AA64PFR0_EL1_set_MPAM(&pfr0, 0);
	}
#else
	// No MPAM
	ID_AA64PFR0_EL1_set_MPAM(&pfr0, 0);
	(void)thread;
#endif

#if defined(ARCH_ARM_FEAT_SVE)
	// Tell non-SVE allowed guests that there is no SVE
	if (!vcpu_option_flags_get_sve_allowed(&thread->vcpu_options)) {
		ID_AA64PFR0_EL1_set_SVE(&pfr0, 0);
	}
#else
	// No SVE
	ID_AA64PFR0_EL1_set_SVE(&pfr0, 0);
	(void)thread;
#endif

#if defined(ARCH_ARM_FEAT_RAS) || defined(ARCH_ARM_FEAT_RASv1p1)
	// Tell non-RAS handler guests there is no RAS
	if (!vcpu_option_flags_get_ras_error_handler(&thread->vcpu_options)) {
		ID_AA64PFR0_EL1_set_RAS(&pfr0, 0);
	}
#endif
#if defined(ARCH_ARM_FEAT_AMUv1) || defined(ARCH_ARM_FEAT_AMUv1p1)
	// Tell non-HLOS guests that there is no AMU
	if (!vcpu_option_flags_get_hlos_vm(&thread->vcpu_options)) {
		ID_AA64PFR0_EL1_set_AMU(&pfr0, 0);
	}
#else
	(void)thread;
#endif
#if !defined(ARCH_ARM_FEAT_SEL2)
	ID_AA64PFR0_EL1_set_SEL2(&pfr0, 0U);
#endif
	ID_AA64PFR0_EL1_set_RME(&pfr0, 0U);

	reg_val = ID_AA64PFR0_EL1_raw(pfr0);

	return reg_val;
}

static register_t
sys_mmfr3_read(void)
{
	register_t reg_val = 0ULL;
	sysreg64_read(ID_MMFR3_EL1, reg_val);
	ID_MMFR3_EL1_t mmfr1 = ID_MMFR3_EL1_cast(reg_val);
#if defined(ARCH_ARM_FEAT_PAN3)
	assert(ID_MMFR3_EL1_get_PAN(&mmfr1) >= 3U);
	ID_MMFR3_EL1_set_PAN(&mmfr1, 3U);
#elif defined(ARCH_ARM_FEAT_PAN2) // now known as FEAT_PAN2
	assert(ID_MMFR3_EL1_get_PAN(&mmfr1) >= 2U);
	ID_MMFR3_EL1_set_PAN(&mmfr1, 2U);
#elif defined(ARCH_ARM_FEAT_PAN)
	assert(ID_MMFR3_EL1_get_PAN(&mmfr1) >= 1U);
	ID_MMFR3_EL1_set_PAN(&mmfr1, 1U);
#else
	ID_MMFR3_EL1_set_PAN(&mmfr1, 0U);
#endif
	reg_val = ID_MMFR3_EL1_raw(mmfr1);

	return reg_val;
}

static register_t
sys_dfr0_read(const thread_t *thread)
{
	register_t    reg_val = 0ULL;
	ID_DFR0_EL1_t dfr0    = register_ID_DFR0_EL1_read();

	// The debug, trace, PMU and SPE modules must correctly support
	// the values reported by the hardware. All we do here is to
	// zero out fields for features we don't support.

#if !defined(MODULE_VM_VDEBUG)
	// Note that ARMv8-A does not allow 0 (not implemented) in the
	// CopDbg field. So this configuration is not really supported.
	ID_DFR0_EL1_set_CopDbg(&dfr0, 0U);
	ID_DFR0_EL1_set_CopSDbg(&dfr0, 0U);
	ID_DFR0_EL1_set_MMapDbg(&dfr0, 0U);
	ID_DFR0_EL1_set_MProfDbg(&dfr0, 0U);
#endif

#if defined(MODULE_VM_VETE)
	// Only the HLOS VM is allowed to trace
	if (!vcpu_option_flags_get_trace_allowed(&thread->vcpu_options)) {
		ID_DFR0_EL1_set_CopTrc(&dfr0, 0U);
		ID_DFR0_EL1_set_TraceFilt(&dfr0, 0U);
	}
#else
	ID_DFR0_EL1_set_CopTrc(&dfr0, 0U);
	ID_DFR0_EL1_set_TraceFilt(&dfr0, 0U);
	(void)thread;
#endif
#if defined(MODULE_VM_VETM)
	// Only the HLOS VM is allowed to trace
	if (!vcpu_option_flags_get_trace_allowed(&thread->vcpu_options)) {
		ID_DFR0_EL1_set_MMapTrc(&dfr0, 0U);
	}
#else
	ID_DFR0_EL1_set_MMapTrc(&dfr0, 0U);
	(void)thread;
#endif
#if !defined(MODULE_PLATFORM_ARM_PMU)
	ID_DFR0_EL1_set_PerfMon(&dfr0, 0U);
#endif

	reg_val = ID_DFR0_EL1_raw(dfr0);

	return reg_val;
}

static register_t
sys_pfr2_read(void)
{
	register_t    reg_val = 0ULL;
	ID_PFR2_EL1_t pfr2    = ID_PFR2_EL1_default();
#if defined(ARCH_ARM_FEAT_CSV3)
	ID_PFR2_EL1_set_CSV3(&pfr2, 1U);
#endif
#if defined(ARCH_ARM_FEAT_SSBS)
	ID_PFR2_EL1_set_SSBS(&pfr2, 1U);
#endif
	reg_val = ID_PFR2_EL1_raw(pfr2);

	return reg_val;
}

static register_t
sys_pfr1_read(void)
{
	register_t    reg_val = 0ULL;
	ID_PFR1_EL1_t pfr1    = register_ID_PFR1_EL1_read();

	reg_val = ID_PFR1_EL1_raw(pfr1);
	return reg_val;
}

static register_t
sys_pfr0_read(const thread_t *thread)
{
	register_t reg_val = 0ULL;

	ID_PFR0_EL1_t pfr0 = register_ID_PFR0_EL1_read();

#if defined(ARCH_ARM_FEAT_RAS) || defined(ARCH_ARM_FEAT_RASv1p1)
	// Tell non-RAS handler guests there is no RAS.
	if (!vcpu_option_flags_get_ras_error_handler(&thread->vcpu_options)) {
		ID_PFR0_EL1_set_RAS(&pfr0, 0);
	}
#else
	(void)thread;
#endif
#if defined(ARCH_ARM_FEAT_AMUv1) || defined(ARCH_ARM_FEAT_AMUv1p1)
	// Tell non-HLOS guests that there is no AMU
	if (!vcpu_option_flags_get_hlos_vm(&thread->vcpu_options)) {
		ID_PFR0_EL1_set_AMU(&pfr0, 0);
	}
#else
	(void)thread;
#endif
#if defined(ARCH_ARM_HAVE_SCXT)
	if (!vcpu_runtime_flags_get_scxt_allowed(&thread->vcpu_flags)) {
		ID_PFR0_EL1_set_CSV2(&pfr0, 1U);
	}
#elif defined(ARCH_ARM_FEAT_CSV2)
	ID_PFR0_EL1_set_CSV2(&pfr0, 1U);
	(void)thread;
#else
	(void)thread;
#endif

	reg_val = ID_PFR0_EL1_raw(pfr0);

	return reg_val;
}

// For the guests with no AMU access we should trap the AMU registers by setting
// CPTR_EL2.TAM and clearing ACTLR_EL2.AMEN. However the trapped registers
// should be handled in the AMU module, and not here.

vcpu_trap_result_t
sysreg_read(ESR_EL2_ISS_MSR_MRS_t iss)
{
	register_t	   reg_val = 0ULL; // Default action is RAZ
	vcpu_trap_result_t ret	   = VCPU_TRAP_RESULT_EMULATED;
	thread_t	  *thread  = thread_get_self();

	// Assert this is a read
	assert(ESR_EL2_ISS_MSR_MRS_get_Direction(&iss));

	uint8_t reg_num = ESR_EL2_ISS_MSR_MRS_get_Rt(&iss);

	// Remove the fields that are not used in the comparison
	ESR_EL2_ISS_MSR_MRS_t temp_iss = iss;
	ESR_EL2_ISS_MSR_MRS_set_Rt(&temp_iss, 0U);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&temp_iss, false);

#if SCHEDULER_CAN_MIGRATE
	// If not pinned, use virtual ID register values.
	if (!vcpu_option_flags_get_pinned(&thread->vcpu_options) &&
	    read_virtual_id_register(temp_iss, reg_num)) {
		goto out;
	}
#endif

	switch (ESR_EL2_ISS_MSR_MRS_raw(temp_iss)) {
	// The registers trapped with HCR_EL2.TID3
	case ISS_MRS_MSR_ID_PFR0_EL1:
		reg_val = sys_pfr0_read(thread);
		break;
	case ISS_MRS_MSR_ID_PFR1_EL1:
		reg_val = sys_pfr1_read();
		break;
	case ISS_MRS_MSR_ID_PFR2_EL1:
		reg_val = sys_pfr2_read();
		break;
	case ISS_MRS_MSR_ID_DFR0_EL1:
		reg_val = sys_dfr0_read(thread);
		break;
	case ISS_MRS_MSR_ID_AFR0_EL1:
		// RES0 - We don't know any AFR0 bits
		break;
	case ISS_MRS_MSR_ID_MMFR0_EL1:
		sysreg64_read(ID_MMFR0_EL1, reg_val);
		break;
	case ISS_MRS_MSR_ID_MMFR1_EL1:
		sysreg64_read(ID_MMFR1_EL1, reg_val);
		break;
	case ISS_MRS_MSR_ID_MMFR2_EL1:
		sysreg64_read(ID_MMFR2_EL1, reg_val);
		break;
	case ISS_MRS_MSR_ID_MMFR3_EL1:
		reg_val = sys_mmfr3_read();
		break;
	case ISS_MRS_MSR_ID_MMFR4_EL1:
		sysreg64_read(ID_MMFR4_EL1, reg_val);
		break;
	case ISS_MRS_MSR_ID_ISAR0_EL1:
		sysreg64_read(ID_ISAR0_EL1, reg_val);
		break;
	case ISS_MRS_MSR_ID_ISAR1_EL1:
		sysreg64_read(ID_ISAR1_EL1, reg_val);
		break;
	case ISS_MRS_MSR_ID_ISAR2_EL1:
		sysreg64_read(ID_ISAR2_EL1, reg_val);
		break;
	case ISS_MRS_MSR_ID_ISAR3_EL1:
		sysreg64_read(ID_ISAR3_EL1, reg_val);
		break;
	case ISS_MRS_MSR_ID_ISAR4_EL1:
		sysreg64_read(ID_ISAR4_EL1, reg_val);
		break;
	case ISS_MRS_MSR_ID_ISAR5_EL1:
		sysreg64_read(ID_ISAR5_EL1, reg_val);
		break;
	case ISS_MRS_MSR_ID_ISAR6_EL1:
		sysreg64_read(S3_0_C0_C2_7, reg_val);
		break;
	case ISS_MRS_MSR_MVFR0_EL1:
		sysreg64_read(MVFR0_EL1, reg_val);
		break;
	case ISS_MRS_MSR_MVFR1_EL1:
		sysreg64_read(MVFR1_EL1, reg_val);
		break;
	case ISS_MRS_MSR_MVFR2_EL1:
		sysreg64_read(MVFR2_EL1, reg_val);
		break;
	case ISS_MRS_MSR_ID_AA64PFR0_EL1:
		reg_val = sys_aa64pfr0_read(thread);
		break;
	case ISS_MRS_MSR_ID_AA64PFR1_EL1:
		reg_val = sys_aa64pfr1_read(thread);
		break;
	case ISS_MRS_MSR_ID_AA64ZFR0_EL1:
#if defined(ARCH_ARM_FEAT_SVE)
		// The SVE module will handle this register
		ret = VCPU_TRAP_RESULT_UNHANDLED;
#else
		// When SVE is not implemented this register is RAZ, do nothing
#endif
		break;
	case ISS_MRS_MSR_ID_AA64SMFR0_EL1:
		// No Scalable Matrix Extension support for now
		break;
	case ISS_MRS_MSR_ID_AA64DFR0_EL1:
		reg_val = sys_aa64dfr0_read(thread);
		break;
	case ISS_MRS_MSR_ID_AA64DFR1_EL1:
		// RES0 - We don't know any AA64DFR1 bits
		break;
	case ISS_MRS_MSR_ID_AA64AFR0_EL1:
		// RES0 - We don't know any AA64AFR0 bits
		break;
	case ISS_MRS_MSR_ID_AA64AFR1_EL1:
		// RES0 - We don't know any AA64AFR1 bits
		break;
	case ISS_MRS_MSR_ID_AA64ISAR0_EL1:
		reg_val = sys_aa64isar0_read();
		break;
	case ISS_MRS_MSR_ID_AA64ISAR1_EL1:
		reg_val = sys_aa64isar1_read();
		break;
	case ISS_MRS_MSR_ID_AA64ISAR2_EL1:
		reg_val = sys_aa64isar2_read();
		break;
	case ISS_MRS_MSR_ID_AA64MMFR0_EL1:
		reg_val = sys_aa64mmfr0_read();
		break;
	case ISS_MRS_MSR_ID_AA64MMFR1_EL1:
		reg_val = sys_aa64mmfr1_read();
		break;
	case ISS_MRS_MSR_ID_AA64MMFR2_EL1:
		reg_val = sys_aa64mmfr2_read();
		break;
	case ISS_MRS_MSR_ID_AA64MMFR3_EL1:
		reg_val = sys_aa64mmfr3_read();
		break;
	case ISS_MRS_MSR_ID_AA64MMFR4_EL1:
		reg_val = 0;
		break;
	// The trapped ACTLR_EL1 by default returns 0 for reads.
	// The particular access should be handled in sysreg_read_cpu.
	case ISS_MRS_MSR_ACTLR_EL1:
		reg_val = 0U;
		break;
	default:
		ret = default_sys_read(&iss, &reg_val);
		break;
	}

	// Update the thread's register
	if (ret == VCPU_TRAP_RESULT_EMULATED) {
		vcpu_gpr_write(thread, reg_num, reg_val);
	}

#if SCHEDULER_CAN_MIGRATE
out:
#endif
	return ret;
}

vcpu_trap_result_t
sysreg_read_fallback(ESR_EL2_ISS_MSR_MRS_t iss)
{
	vcpu_trap_result_t ret	  = VCPU_TRAP_RESULT_UNHANDLED;
	thread_t	  *thread = thread_get_self();

	if (ESR_EL2_ISS_MSR_MRS_get_Op0(&iss) == 2U) {
		// Debug registers, RAZ by default
		vcpu_gpr_write(thread, ESR_EL2_ISS_MSR_MRS_get_Rt(&iss), 0U);
		ret = VCPU_TRAP_RESULT_EMULATED;
	}

	return ret;
}

vcpu_trap_result_t
sysreg_write(ESR_EL2_ISS_MSR_MRS_t iss)
{
	vcpu_trap_result_t ret	  = VCPU_TRAP_RESULT_EMULATED;
	thread_t	  *thread = thread_get_self();

	if (compiler_expected(ESR_EL2_ISS_MSR_MRS_get_Op0(&iss) != 1U)) {
		ret = VCPU_TRAP_RESULT_UNHANDLED;
		goto out;
	}

	// Assert this is a write
	assert(!ESR_EL2_ISS_MSR_MRS_get_Direction(&iss));

	// Remove the fields that are not used in the comparison
	ESR_EL2_ISS_MSR_MRS_t temp_iss = iss;
	ESR_EL2_ISS_MSR_MRS_set_Rt(&temp_iss, 0U);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&temp_iss, false);

	switch (ESR_EL2_ISS_MSR_MRS_raw(temp_iss)) {
	// System instructions trapped with HCR_EL2.TSW
	case ISS_MRS_MSR_DC_CSW:
	case ISS_MRS_MSR_DC_CISW:
	case ISS_MRS_MSR_DC_ISW:
		// Set/way cache ops are not safe under virtualisation (or, in
		// most cases, without virtualisation) as they are vulnerable
		// to racing with prefetches through EL2 mappings, or with
		// migration if that is enabled. Warn if a VM executes one.
		TRACE_AND_LOG(DEBUG, INFO, "Unsafe DC *SW in VM {:d} @ {:#x}",
			      thread->addrspace->vmid,
			      ELR_EL2_raw(thread->vcpu_regs_gpr.pc));

		// However, they're only unsafe for the VM executing them
		// (because DC ISW is upgraded to DC CISW in hardware) so we
		// disable the trap after the first warning (except on physical
		// CPUs with an erratum that makes all set/way ops unsafe).
		// FIXME:
		preempt_disable();
		thread->vcpu_regs_el2.hcr_el2 = register_HCR_EL2_read();
		HCR_EL2_set_TSW(&thread->vcpu_regs_el2.hcr_el2, false);
		register_HCR_EL2_write(thread->vcpu_regs_el2.hcr_el2);
		preempt_enable();
		ret = VCPU_TRAP_RESULT_RETRY;
		break;
	default:
		ret = VCPU_TRAP_RESULT_UNHANDLED;
		break;
	}

out:
	return ret;
}

vcpu_trap_result_t
sysreg_write_fallback(ESR_EL2_ISS_MSR_MRS_t iss)
{
	vcpu_trap_result_t ret	  = VCPU_TRAP_RESULT_EMULATED;
	thread_t	  *thread = thread_get_self();

	// Read the thread's register
	uint8_t	   reg_num = ESR_EL2_ISS_MSR_MRS_get_Rt(&iss);
	register_t reg_val = vcpu_gpr_read(thread, reg_num);

	// Remove the fields that are not used in the comparison
	ESR_EL2_ISS_MSR_MRS_t temp_iss = iss;
	ESR_EL2_ISS_MSR_MRS_set_Rt(&temp_iss, 0U);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&temp_iss, false);

	switch (ESR_EL2_ISS_MSR_MRS_raw(temp_iss)) {
	// The registers trapped with HCR_EL2.TVM
	case ISS_MRS_MSR_SCTLR_EL1: {
		SCTLR_EL1_t sctrl = SCTLR_EL1_cast(reg_val);
		// If HCR_EL2.DC is set, prevent VM's enabling Stg-1 MMU
		if (HCR_EL2_get_DC(&thread->vcpu_regs_el2.hcr_el2) &&
		    SCTLR_EL1_get_M(&sctrl)) {
			ret = VCPU_TRAP_RESULT_UNHANDLED;
		} else {
			register_SCTLR_EL1_write(sctrl);
		}
		break;
	}
	case ISS_MRS_MSR_TTBR0_EL1:
		register_TTBR0_EL1_write(TTBR0_EL1_cast(reg_val));
		break;
	case ISS_MRS_MSR_TTBR1_EL1:
		register_TTBR1_EL1_write(TTBR1_EL1_cast(reg_val));
		break;
	case ISS_MRS_MSR_TCR_EL1:
		register_TCR_EL1_write(TCR_EL1_cast(reg_val));
		break;
	case ISS_MRS_MSR_ESR_EL1:
		register_ESR_EL1_write(ESR_EL1_cast(reg_val));
		break;
	case ISS_MRS_MSR_FAR_EL1:
		register_FAR_EL1_write(FAR_EL1_cast(reg_val));
		break;
	case ISS_MRS_MSR_AFSR0_EL1:
		register_AFSR0_EL1_write(AFSR0_EL1_cast(reg_val));
		break;
	case ISS_MRS_MSR_AFSR1_EL1:
		register_AFSR1_EL1_write(AFSR1_EL1_cast(reg_val));
		break;
	case ISS_MRS_MSR_MAIR_EL1:
		register_MAIR_EL1_write(MAIR_EL1_cast(reg_val));
		break;
	case ISS_MRS_MSR_AMAIR_EL1:
		// WI
		break;
	// The trapped ACTLR_EL1 by default will be ignored for writes.
	// The particular access should be handled in sysreg_read_cpu.
	case ISS_MRS_MSR_ACTLR_EL1:
		// WI
		break;
	case ISS_MRS_MSR_CONTEXTIDR_EL1:
		register_CONTEXTIDR_EL1_write(CONTEXTIDR_EL1_cast(reg_val));
		break;
	default: {
		uint8_t opc0 = ESR_EL2_ISS_MSR_MRS_get_Op0(&iss);
		if (opc0 == 2U) {
			// Debug registers, WI by default
		} else {
			ret = VCPU_TRAP_RESULT_UNHANDLED;
		}
		break;
	}
	}

	return ret;
}
