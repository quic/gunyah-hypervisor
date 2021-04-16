// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <log.h>
#include <thread.h>
#include <trace.h>
#include <vcpu.h>

#include <asm/sysregs.h>
#include <asm/system_registers.h>

#include "event_handlers.h"

#if SCHEDULER_CAN_MIGRATE
static bool
read_virtual_id_register(ESR_EL2_ISS_MSR_MRS_t iss, uint8_t reg_num)
{
	register_t val	   = 0U;
	bool	   handled = true;
	thread_t * thread  = thread_get_self();

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
	case ISS_MRS_MSR_ID_AA64PFR0_EL1: {
		ID_AA64PFR0_EL1_t pfr0 = ID_AA64PFR0_EL1_default();
		ID_AA64PFR0_EL1_set_EL0(&pfr0, 2U);
#if defined(CONFIG_AARCH64_32BIT_EL1)
		ID_AA64PFR0_EL1_set_EL1(&pfr0, 2U);
#else
		ID_AA64PFR0_EL1_set_EL1(&pfr0, 1U);
#endif
		ID_AA64PFR0_EL1_set_EL2(&pfr0, 1U);
		ID_AA64PFR0_EL1_set_EL3(&pfr0, 1U);
#if defined(ARCH_ARM_8_2_FP16)
		ID_AA64PFR0_EL1_set_FP(&pfr0, 1U);
		ID_AA64PFR0_EL1_set_AdvSIMD(&pfr0, 1U);
#endif
		ID_AA64PFR0_EL1_set_GIC(&pfr0, 1U);
#if defined(ARCH_ARM_8_4_SECEL2)
		ID_AA64PFR0_EL1_set_SEL2(&pfr0, 1U);
#endif
#if (ARCH_ARM_VER >= 84) || defined(ARCH_ARM_8_4_DIT)
		ID_AA64PFR0_EL1_set_DIT(&pfr0, 1U);
#endif
#if (ARCH_ARM_VER >= 85) || defined(ARCH_ARM_8_0_CSV2)
#if defined(ARM_ARM_8_0_CSV2_SCXTNUM)
		ID_AA64PFR0_EL1_set_CSV2(&pfr0, 2U);
#else
		ID_AA64PFR0_EL1_set_CSV2(&pfr0, 1U);
#endif
#endif
#if (ARCH_ARM_VER >= 85) || defined(ARCH_ARM_8_0_CSV3)
		ID_AA64PFR0_EL1_set_CSV3(&pfr0, 1U);
#endif
		val = ID_AA64PFR0_EL1_raw(pfr0);
		break;
	}
	case ISS_MRS_MSR_ID_AA64PFR1_EL1: {
		ID_AA64PFR1_EL1_t pfr1 = ID_AA64PFR1_EL1_default();
#if (ARCH_ARM_VER >= 85) || defined(ARCH_ARM_8_5_BTI)
		ID_AA64PFR1_EL1_set_BT(&pfr1, 1U);
#endif
#if (ARCH_ARM_VER >= 85) || defined(ARCH_ARM_8_0_SSBS)
#if defined(ARCH_ARM_8_0_SSBS_MSR_MRS)
		ID_AA64PFR1_EL1_set_SSBS(&pfr1, 2U);
#else
		ID_AA64PFR1_EL1_set_SSBS(&pfr1, 1U);
#endif
#endif
		val = ID_AA64PFR1_EL1_raw(pfr1);
		break;
	}
	case ISS_MRS_MSR_ID_AA64ISAR0_EL1: {
		ID_AA64ISAR0_EL1_t isar0 = ID_AA64ISAR0_EL1_default();
#if defined(ARCH_ARM_8_0_AES_PMULL)
		ID_AA64ISAR0_EL1_set_AES(&isar0, 2U);
#elif defined(ARCH_ARM_8_0_AES)
		ID_AA64ISAR0_EL1_set_AES(&isar0, 1U);
#endif
#if defined(ARCH_ARM_8_0_SHA)
		ID_AA64ISAR0_EL1_set_SHA1(&isar0, 1U);
#endif
#if defined(ARCH_ARM_8_2_SHA)
		ID_AA64ISAR0_EL1_set_SHA2(&isar0, 2U);
#elif defined(ARCH_ARM_8_0_SHA)
		ID_AA64ISAR0_EL1_set_SHA2(&isar0, 1U);
#endif
		ID_AA64ISAR0_EL1_set_CRC32(&isar0, 1U);
#if (ARCH_ARM_VER >= 81) || defined(ARCH_ARM_8_1_VHE)
		ID_AA64ISAR0_EL1_set_Atomic(&isar0, 2U);
#endif
#if (ARCH_ARM_VER >= 81) || defined(ARCH_ARM_8_1_RDMA)
		ID_AA64ISAR0_EL1_set_RDM(&isar0, 1U);
#endif
#if defined(ARCH_ARM_8_2_SHA)
		ID_AA64ISAR0_EL1_set_SHA3(&isar0, 1U);
#endif
#if defined(ARCH_ARM_8_2_SM)
		ID_AA64ISAR0_EL1_set_SM3(&isar0, 1U);
		ID_AA64ISAR0_EL1_set_SM4(&isar0, 1U);
#endif
#if (ARCH_ARM_VER >= 84) || defined(ARCH_ARM_8_2_DOTPROD)
		ID_AA64ISAR0_EL1_set_DP(&isar0, 1U);
#endif
#if defined(ARCH_ARM_8_2_FP16) &&                                              \
	((ARCH_ARM_VER >= 84) || defined(ARCH_ARM_8_2_FHM))
		ID_AA64ISAR0_EL1_set_FHM(&isar0, 1U);
#endif
#if (ARCH_ARM_VER >= 85) || defined(ARCH_ARM_8_5_CONDM)
		ID_AA64ISAR0_EL1_set_TS(&isar0, 2U);
#elif (ARCH_ARM_VER >= 84) || defined(ARCH_ARM_8_4_CONDM)
		ID_AA64ISAR0_EL1_set_TS(&isar0, 1U);
#endif
#if (ARCH_ARM_VER >= 84) || defined(ARCH_ARM_8_4_TLBI)
		ID_AA64ISAR0_EL1_set_TLB(&isar0, 2U);
#endif
#if defined(ARCH_ARM_8_5_RNG)
		ID_AA64ISAR0_EL1_set_RNDR(&isar0, 2U);
#endif
		val = ID_AA64ISAR0_EL1_raw(isar0);
		break;
	}
	case ISS_MRS_MSR_ID_AA64ISAR1_EL1: {
		ID_AA64ISAR1_EL1_t isar1 = ID_AA64ISAR1_EL1_default();
#if (ARCH_ARM_VER >= 85) || defined(ARCH_ARM_8_2_DCCVADP)
		ID_AA64ISAR1_EL1_set_DPB(&isar1, 2U);
#elif (ARCH_ARM_VER >= 82) || defined(ARCH_ARM_8_2_DCPOP)
		ID_AA64ISAR1_EL1_set_DPB(&isar1, 1U);
#endif
#if (ARCH_ARM_VER >= 83) || defined(ARCH_ARM_8_3_JSCONV)
		ID_AA64ISAR1_EL1_set_JSCVT(&isar1, 1U);
#endif
#if (ARCH_ARM_VER >= 83) || defined(ARCH_ARM_8_3_COMPNUM)
		ID_AA64ISAR1_EL1_set_FCMA(&isar1, 1U);
#endif
#if (ARCH_ARM_VER >= 84) || defined(ARCH_ARM_8_4_RCPC)
		ID_AA64ISAR1_EL1_set_LRCPC(&isar1, 2U);
#elif (ARCH_ARM_VER >= 83) || defined(ARCH_ARM_8_3_RCPC)
		ID_AA64ISAR1_EL1_set_LRCPC(&isar1, 1U);
#endif
#if (ARCH_ARM_VER >= 85) || defined(ARCH_ARM_8_5_FRINT)
		ID_AA64ISAR1_EL1_set_FRINTTS(&isar1, 1U);
#endif
#if (ARCH_ARM_VER >= 85) || defined(ARCH_ARM_8_0_SB)
		ID_AA64ISAR1_EL1_set_SB(&isar1, 1U);
#endif
#if (ARCH_ARM_VER >= 85) || defined(ARCH_ARM_8_0_PREDINV)
		ID_AA64ISAR1_EL1_set_SPECRES(&isar1, 1U);
#endif
		val = ID_AA64ISAR1_EL1_raw(isar1);
		break;
	}
	case ISS_MRS_MSR_ID_AA64MMFR0_EL1: {
		ID_AA64MMFR0_EL1_t mmfr0 = ID_AA64MMFR0_EL1_default();
		// FIXME: match PLATFORM_VM_ADDRESS_SPACE_BITS
		ID_AA64MMFR0_EL1_set_PARange(&mmfr0, TCR_PS_SIZE_36BITS);
#if defined(ARCH_AARCH64_ASID16)
		ID_AA64MMFR0_EL1_set_ASIDBits(&mmfr0, 2U);
#endif
		ID_AA64MMFR0_EL1_set_TGran16(&mmfr0, 1U);
#if (ARCH_ARM_VER >= 85) || defined(ARCH_ARM_8_5_CSEH)
		ID_AA64MMFR0_EL1_set_ExS(&mmfr0, 1U);
#endif
		val = ID_AA64MMFR0_EL1_raw(mmfr0);
		break;
	}
	case ISS_MRS_MSR_ID_AA64MMFR1_EL1: {
		ID_AA64MMFR1_EL1_t mmfr1 = ID_AA64MMFR1_EL1_default();
#if defined(ARCH_ARM_8_1_TTHM_HD)
		ID_AA64MMFR1_EL1_set_HAFDBS(&mmfr1, 2U);
#elif defined(ARCH_ARM_8_1_TTHM)
		ID_AA64MMFR1_EL1_set_HAFDBS(&mmfr1, 1U);
#endif
#if defined(ARCH_ARM_8_1_VMID16)
		ID_AA64MMFR1_EL1_set_VMIDBits(&mmfr1, 2U);
#endif
#if (ARCH_ARM_VER >= 81) || defined(ARCH_ARM_8_1_VHE)
		ID_AA64MMFR1_EL1_set_VH(&mmfr1, 1U);
#endif
#if defined(ARCH_ARM_8_2_TTPBHA)
		ID_AA64MMFR1_EL1_set_HPDS(&mmfr1, 2U);
#elif (ARCH_ARM_VER >= 81) || defined(ARCH_ARM_8_1_HPD)
		ID_AA64MMFR1_EL1_set_HPDS(&mmfr1, 1U);
#endif
#if (ARCH_ARM_VER >= 81)
		ID_AA64MMFR1_EL1_set_LO(&mmfr1, 1U);
#endif
#if (ARCH_ARM_VER >= 82) || defined(ARCH_ARM_8_2_ATS1E1)
		ID_AA64MMFR1_EL1_set_PAN(&mmfr1, 2U);
#elif (ARCH_ARM_VER >= 81) || defined(ARCH_ARM_8_1_PAN)
		ID_AA64MMFR1_EL1_set_PAN(&mmfr1, 1U);
#endif
#if (ARCH_ARM_VER >= 82) || defined(ARCH_ARM_8_2_TTS2UXN)
		ID_AA64MMFR1_EL1_set_XNX(&mmfr1, 1U);
#endif
		val = ID_AA64MMFR1_EL1_raw(mmfr1);
		break;
	}
	case ISS_MRS_MSR_ID_AA64MMFR2_EL1: {
		ID_AA64MMFR2_EL1_t mmfr2 = ID_AA64MMFR2_EL1_default();
#if (ARCH_ARM_VER >= 82) || defined(ARCH_ARM_8_2_TTCNP)
		ID_AA64MMFR2_EL1_set_CnP(&mmfr2, 1U);
#endif
#if (ARCH_ARM_VER >= 82) || defined(ARCH_ARM_8_2_UAO)
		ID_AA64MMFR2_EL1_set_UAO(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_8_2_LSMAOC)
		ID_AA64MMFR2_EL1_set_LSM(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_8_2_IESB)
		ID_AA64MMFR2_EL1_set_IESB(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_8_2_LVA)
		ID_AA64MMFR2_EL1_set_VARange(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_8_3_CCIDX)
		ID_AA64MMFR2_EL1_set_CCIDX(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_8_4_NV)
		ID_AA64MMFR2_EL1_set_NV(&mmfr2, 2U);
#elif defined(ARCH_ARM_8_3_NV)
		ID_AA64MMFR2_EL1_set_NV(&mmfr2, 1U);
#endif
#if (ARCH_ARM_VER >= 84) || defined(ARCH_ARM_8_4_TTST)
		ID_AA64MMFR2_EL1_set_ST(&mmfr2, 1U);
#endif
#if (ARCH_ARM_VER >= 84) || defined(ARCH_ARM_8_4_LSE)
		ID_AA64MMFR2_EL1_set_AT(&mmfr2, 1U);
#endif
#if (ARCH_ARM_VER >= 84) || defined(ARCH_ARM_8_4_IDST)
		ID_AA64MMFR2_EL1_set_IDS(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_8_4_SECEL2)
		ID_AA64MMFR2_EL1_set_FWB(&mmfr2, 1U);
#endif
#if (ARCH_ARM_VER >= 84) || defined(ARCH_ARM_8_4_TTL)
		ID_AA64MMFR2_EL1_set_IDS(&mmfr2, 1U);
#endif
#if defined(ARCH_ARM_8_4_TTREM_LEVEL2)
		ID_AA64MMFR2_EL1_set_BBM(&mmfr2, 2U);
#elif defined(ARCH_ARM_8_4_TTREM)
		ID_AA64MMFR2_EL1_set_BBM(&mmfr2, 1U);
#endif
#if (ARCH_ARM_VER >= 85) || defined(ARCH_ARM_8_2_EVT_TTLB)
		ID_AA64MMFR2_EL1_set_EVT(&mmfr2, 2U);
#elif defined(ARCH_ARM_8_2_EVT)
		ID_AA64MMFR2_EL1_set_EVT(&mmfr2, 1U);
#endif
#if (ARCH_ARM_VER >= 85)
		ID_AA64MMFR2_EL1_set_E0PD(&mmfr2, 1U);
#endif
		val = ID_AA64MMFR2_EL1_raw(mmfr2);
		break;
	}
	case ISS_MRS_MSR_ID_PFR0_EL1: {
		ID_PFR0_EL1_t pfr0 = ID_PFR0_EL1_default();
		ID_PFR0_EL1_set_State0(&pfr0, 1U);
		ID_PFR0_EL1_set_State1(&pfr0, 3U);
		ID_PFR0_EL1_set_State2(&pfr0, 1U);
#if (ARCH_ARM_VER >= 85) || defined(ARCH_ARM_8_0_CSV2)
		ID_PFR0_EL1_set_CSV2(&pfr0, 1U);
#endif
#if (ARCH_ARM_VER >= 84) || defined(ARCH_ARM_8_4_DIT)
		ID_PFR0_EL1_set_DIT(&pfr0, 1U);
#endif
		val = ID_PFR0_EL1_raw(pfr0);
		break;
	}
	case ISS_MRS_MSR_ID_PFR1_EL1: {
		ID_PFR1_EL1_t pfr1 = ID_PFR1_EL1_default();
#if defined(CONFIG_AARCH64_32BIT_EL1)
		ID_PFR1_EL1_set_ProgMod(&pfr1, 1U);
		ID_PFR1_EL1_set_Security(&pfr1, 1U);
		ID_PFR1_EL1_set_Virtualization(&pfr1, 1U);
#endif
		ID_PFR1_EL1_set_GenTimer(&pfr1, 1U);
		ID_PFR1_EL1_set_GIC(&pfr1, 1U);
		val = ID_PFR1_EL1_raw(pfr1);
		break;
	}
	case ISS_MRS_MSR_ID_PFR2_EL1: {
		ID_PFR2_EL1_t pfr2 = ID_PFR2_EL1_default();
#if (ARCH_ARM_VER >= 85) || defined(ARCH_ARM_8_0_CSV3)
		ID_PFR2_EL1_set_CSV3(&pfr2, 1U);
#endif
#if (ARCH_ARM_VER >= 85) || defined(ARCH_ARM_8_0_SSBS)
		ID_PFR2_EL1_set_SSBS(&pfr2, 1U);
#endif
		val = ID_PFR2_EL1_raw(pfr2);
		break;
	}
	case ISS_MRS_MSR_ID_ISAR0_EL1: {
		ID_ISAR0_EL1_t isar0 = ID_ISAR0_EL1_default();
		ID_ISAR0_EL1_set_BitCount(&isar0, 1U);
		ID_ISAR0_EL1_set_BitField(&isar0, 1U);
		ID_ISAR0_EL1_set_CmpBranch(&isar0, 1U);
		ID_ISAR0_EL1_set_Divide(&isar0, 2U);
		val = ID_ISAR0_EL1_raw(isar0);
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
		val = ID_ISAR1_EL1_raw(isar1);
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
		val = ID_ISAR2_EL1_raw(isar2);
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
		val = ID_ISAR3_EL1_raw(isar3);
		break;
	}
	case ISS_MRS_MSR_ID_ISAR4_EL1: {
		ID_ISAR4_EL1_t isar4 = ID_ISAR4_EL1_default();
		ID_ISAR4_EL1_set_Unpriv(&isar4, 2U);
		ID_ISAR4_EL1_set_WithShifts(&isar4, 4U);
		ID_ISAR4_EL1_set_Writeback(&isar4, 1U);
#if defined(CONFIG_AARCH64_32BIT_EL1)
		ID_ISAR4_EL1_set_SMC(&isar4, 1U);
#endif
		ID_ISAR4_EL1_set_Barrier(&isar4, 1U);
		val = ID_ISAR4_EL1_raw(isar4);
		break;
	}
	case ISS_MRS_MSR_ID_ISAR5_EL1: {
		ID_ISAR5_EL1_t isar5 = ID_ISAR5_EL1_default();
		ID_ISAR5_EL1_set_SEVL(&isar5, 1U);
#if defined(ARCH_ARM_8_0_AES_PMULL)
		ID_ISAR5_EL1_set_AES(&isar5, 2U);
#elif defined(ARCH_ARM_8_0_AES)
		ID_ISAR5_EL1_set_AES(&isar5, 1U);
#endif
#if defined(ARCH_ARM_8_0_SHA)
		ID_ISAR5_EL1_set_SHA1(&isar5, 1U);
		ID_ISAR5_EL1_set_SHA2(&isar5, 1U);
#endif
		ID_ISAR5_EL1_set_CRC32(&isar5, 1U);
#if (ARCH_ARM_VER >= 81)
		ID_ISAR5_EL1_set_RDM(&isar5, 1U);
#endif
#if (ARCH_ARM_VER >= 83)
		ID_ISAR5_EL1_set_VCMA(&isar5, 2U);
#endif
		val = ID_ISAR5_EL1_raw(isar5);
		break;
	}
	case ISS_MRS_MSR_ID_ISAR6_EL1: {
		ID_ISAR6_EL1_t isar6 = ID_ISAR6_EL1_default();
#if (ARCH_ARM_VER >= 83)
		ID_ISAR6_EL1_set_JSCVT(&isar6, 1U);
#endif
#if (ARCH_ARM_VER >= 84) || defined(ARCH_ARM_8_2_DOTPROD)
		ID_ISAR6_EL1_set_DP(&isar6, 1U);
#endif
#if defined(ARCH_ARM_8_2_FP16) &&                                              \
	((ARCH_ARM_VER >= 84) || defined(ARCH_ARM_8_2_FHM))
		ID_ISAR6_EL1_set_FHM(&isar6, 1U);
#endif
#if (ARCH_ARM_VER >= 85) || defined(ARCH_ARM_8_0_SB)
		ID_ISAR6_EL1_set_SB(&isar6, 1U);
#endif
#if (ARCH_ARM_VER >= 85) || defined(ARCH_ARM_8_0_PREDINV)
		ID_ISAR6_EL1_set_SPECRES(&isar6, 1U);
#endif
		val = ID_ISAR6_EL1_raw(isar6);
		break;
	}
	case ISS_MRS_MSR_ID_MMFR0_EL1: {
		ID_MMFR0_EL1_t mmfr0 = ID_MMFR0_EL1_default();
		ID_MMFR0_EL1_set_VMSA(&mmfr0, 5U);
		ID_MMFR0_EL1_set_OuterShr(&mmfr0, 1U);
		ID_MMFR0_EL1_set_ShareLvl(&mmfr0, 1U);
		ID_MMFR0_EL1_set_AuxReg(&mmfr0, 2U);
		ID_MMFR0_EL1_set_InnerShr(&mmfr0, 1U);
		val = ID_MMFR0_EL1_raw(mmfr0);
		break;
	}
	case ISS_MRS_MSR_ID_MMFR1_EL1: {
		ID_MMFR1_EL1_t mmfr1 = ID_MMFR1_EL1_default();
		ID_MMFR1_EL1_set_BPred(&mmfr1, 4U);
		val = ID_MMFR1_EL1_raw(mmfr1);
		break;
	}
	case ISS_MRS_MSR_ID_MMFR2_EL1: {
		ID_MMFR2_EL1_t mmfr2 = ID_MMFR2_EL1_default();
		ID_MMFR2_EL1_set_UniTLB(&mmfr2, 6U);
		ID_MMFR2_EL1_set_MemBarr(&mmfr2, 2U);
		ID_MMFR2_EL1_set_WFIStall(&mmfr2, 1U);
		val = ID_MMFR2_EL1_raw(mmfr2);
		break;
	}
	case ISS_MRS_MSR_ID_MMFR3_EL1: {
		ID_MMFR3_EL1_t mmfr3 = ID_MMFR3_EL1_default();
		ID_MMFR3_EL1_set_CMaintVA(&mmfr3, 1U);
		ID_MMFR3_EL1_set_CMaintSW(&mmfr3, 1U);
		ID_MMFR3_EL1_set_BPMaint(&mmfr3, 2U);
		ID_MMFR3_EL1_set_MaintBcst(&mmfr3, 2U);
#if (ARCH_ARM_VER >= 82) || defined(ARCH_ARM_8_2_ATS1E1)
		ID_MMFR3_EL1_set_PAN(&mmfr3, 2U);
#elif (ARCH_ARM_VER >= 81) || defined(ARCH_ARM_8_1_PAN)
		ID_MMFR3_EL1_set_PAN(&mmfr3, 1U);
#endif
		ID_MMFR3_EL1_set_CohWalk(&mmfr3, 1U);
		ID_MMFR3_EL1_set_CMemSz(&mmfr3, 2U);
		val = ID_MMFR3_EL1_raw(mmfr3);
		break;
	}
	case ISS_MRS_MSR_ID_MMFR4_EL1: {
		ID_MMFR4_EL1_t mmfr4 = ID_MMFR4_EL1_default();
		ID_MMFR4_EL1_set_AC2(&mmfr4, 1U);
#if (ARCH_ARM_VER >= 82) || defined(ARCH_ARM_8_2_TTS2UXN)
		ID_MMFR4_EL1_set_XNX(&mmfr4, 1U);
#endif
#if (ARCH_ARM_VER >= 82) || defined(ARCH_ARM_8_2_TTCNP)
		ID_MMFR4_EL1_set_CnP(&mmfr4, 1U);
#endif
#if defined(ARCH_ARM_8_2_TTPBHA)
		ID_MMFR4_EL1_set_HPDS(&mmfr4, 2U);
#elif defined(ARCH_ARM_8_2_AA32HPD)
		ID_MMFR4_EL1_set_HPDS(&mmfr4, 1U);
#endif
#if defined(ARCH_ARM_8_2_LSMAOC)
		ID_MMFR4_EL1_set_LSM(&mmfr4, 1U);
#endif
#if defined(ARCH_ARM_8_3_CCIDX)
		ID_MMFR4_EL1_set_CCIDX(&mmfr4, 1U);
#endif
#if (ARCH_ARM_VER >= 85) || defined(ARCH_ARM_8_2_EVT_TTLB)
		ID_MMFR4_EL1_set_EVT(&mmfr4, 2U);
#elif defined(ARCH_ARM_8_2_EVT)
		ID_MMFR4_EL1_set_EVT(&mmfr4, 1U);
#endif
		val = ID_MMFR4_EL1_raw(mmfr4);
		break;
	}
	case ISS_MRS_MSR_ID_AA64DFR1_EL1:
	case ISS_MRS_MSR_ID_AA64AFR0_EL1:
	case ISS_MRS_MSR_ID_AA64AFR1_EL1:
	case ISS_MRS_MSR_ID_AFR0_EL1:
		// RAZ
		break;
	default:
		handled = false;
		break;
	}

	if (handled) {
		vcpu_gpr_write(thread, reg_num, val);
	}

	return handled;
}
#endif

// For the guests with no AMU access we should trap the AMU registers by setting
// CPTR_EL2.TAM and clearing ACTLR_EL2.AMEN. However the trapped registers
// should be handled in the AMU module, and not here.

vcpu_trap_result_t
sysreg_read(ESR_EL2_ISS_MSR_MRS_t iss)
{
	register_t	   val	  = 0ULL; // Default action is RAZ
	vcpu_trap_result_t ret	  = VCPU_TRAP_RESULT_EMULATED;
	thread_t *	   thread = thread_get_self();

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
	case ISS_MRS_MSR_ID_PFR0_EL1: {
		ID_PFR0_EL1_t id_pfr0 = register_ID_PFR0_EL1_read();

#if (ARCH_ARM_VER < 82) && defined(ARCH_ARM_8_2_RAS)
		// Tell non-HLOS guests that there is no RAS. This field is
		// allowed to be zero only in ARMv8.0 and ARMv8.1.
		if (!vcpu_option_flags_get_hlos_vm(&thread->vcpu_options)) {
			ID_PFR0_EL1_set_RAS(&id_pfr0, 0);
		}
#endif
#if defined(ARCH_ARM_8_4_AMU)
		// Tell non-HLOS guests that there is no AMU
		if (!vcpu_option_flags_get_hlos_vm(&thread->vcpu_options)) {
			ID_PFR0_EL1_set_AMU(&id_pfr0, 0);
		}
#endif

		val = ID_PFR0_EL1_raw(id_pfr0);
		break;
	}
	case ISS_MRS_MSR_ID_PFR1_EL1: {
		ID_PFR1_EL1_t id_pfr1 = register_ID_PFR1_EL1_read();

		val = ID_PFR1_EL1_raw(id_pfr1);
		break;
	}
	case ISS_MRS_MSR_ID_DFR0_EL1: {
		ID_DFR0_EL1_t id_dfr0 = register_ID_DFR0_EL1_read();

		// The debug, trace, PMU and SPE modules must correctly support
		// the values reported by the hardware. All we do here is to
		// zero out fields for features we don't support.

#if !VCPU_DEBUG_CONTEXT_SAVED
		// Note that ARMv8-A does not allow 0 (not implemented) in the
		// CopDbg field. So this configuration is not really supported.
		ID_DFR0_EL1_set_CopDbg(&id_aa64dfr0, 0U);
		ID_DFR0_EL1_set_CopSDbg(&id_aa64dfr0, 0U);
		ID_DFR0_EL1_set_MMapDbg(&id_dfr0, 0U);
		ID_DFR0_EL1_set_MProfDbg(&id_dfr0, 0U);
#endif

#if VCPU_TRACE_CONTEXT_SAVED
		// Only the HLOS VM is allowed to trace
		if (!vcpu_option_flags_get_hlos_vm(&thread->vcpu_options)) {
			ID_DFR0_EL1_set_CopTrc(&id_dfr0, 0U);
			ID_DFR0_EL1_set_MMapTrc(&id_dfr0, 0U);
			ID_DFR0_EL1_set_TraceFilt(&id_dfr0, 0U);
		}
#else
		ID_DFR0_EL1_set_CopTrc(&id_dfr0, 0U);
		ID_DFR0_EL1_set_MMapTrc(&id_dfr0, 0U);
		ID_DFR0_EL1_set_TraceFilt(&id_dfr0, 0U);
#endif

		// FIXME: for now we will give all VMs full control of PMU
		// hardware, therefore the code below is commented out until
		// advance PMU support has been added to the hypervisor.
		// No PMU support
		// ID_DFR0_EL1_set_PerfMon(&id_dfr0, 0);

		val = ID_DFR0_EL1_raw(id_dfr0);
		break;
	}
	case ISS_MRS_MSR_ID_AFR0_EL1:
		sysreg64_read(ID_AFR0_EL1, val);
		break;
	case ISS_MRS_MSR_ID_MMFR0_EL1:
		sysreg64_read(ID_MMFR0_EL1, val);
		break;
	case ISS_MRS_MSR_ID_MMFR1_EL1:
		sysreg64_read(ID_MMFR1_EL1, val);
		break;
	case ISS_MRS_MSR_ID_MMFR2_EL1:
		sysreg64_read(ID_MMFR2_EL1, val);
		break;
	case ISS_MRS_MSR_ID_MMFR3_EL1:
		sysreg64_read(ID_MMFR3_EL1, val);
		break;
	case ISS_MRS_MSR_ID_MMFR4_EL1:
		sysreg64_read(ID_MMFR4_EL1, val);
		break;
	case ISS_MRS_MSR_ID_ISAR0_EL1:
		sysreg64_read(ID_ISAR0_EL1, val);
		break;
	case ISS_MRS_MSR_ID_ISAR1_EL1:
		sysreg64_read(ID_ISAR1_EL1, val);
		break;
	case ISS_MRS_MSR_ID_ISAR2_EL1:
		sysreg64_read(ID_ISAR2_EL1, val);
		break;
	case ISS_MRS_MSR_ID_ISAR3_EL1:
		sysreg64_read(ID_ISAR3_EL1, val);
		break;
	case ISS_MRS_MSR_ID_ISAR4_EL1:
		sysreg64_read(ID_ISAR4_EL1, val);
		break;
	case ISS_MRS_MSR_ID_ISAR5_EL1:
		sysreg64_read(ID_ISAR5_EL1, val);
		break;
	case ISS_MRS_MSR_ID_ISAR6_EL1:
		sysreg64_read(S3_0_C0_C2_7, val);
		break;
	case ISS_MRS_MSR_MVFR0_EL1:
		sysreg64_read(MVFR0_EL1, val);
		break;
	case ISS_MRS_MSR_MVFR1_EL1:
		sysreg64_read(MVFR1_EL1, val);
		break;
	case ISS_MRS_MSR_MVFR2_EL1:
		sysreg64_read(MVFR2_EL1, val);
		break;
	case ISS_MRS_MSR_ID_AA64PFR0_EL1: {
		ID_AA64PFR0_EL1_t id_aa64pfr0 = register_ID_AA64PFR0_EL1_read();

		// No for SVE or MPAM
		ID_AA64PFR0_EL1_set_SVE(&id_aa64pfr0, 0);
		ID_AA64PFR0_EL1_set_MPAM(&id_aa64pfr0, 0);

#if (ARCH_ARM_VER >= 82) || defined(ARCH_ARM_8_2_RAS) ||                       \
	defined(ARCH_ARM_8_4_RAS)
		// Tell non-HLOS guests that there is no RAS
		if (!vcpu_option_flags_get_hlos_vm(&thread->vcpu_options)) {
			ID_AA64PFR0_EL1_set_RAS(&id_aa64pfr0, 0);
		}
#endif
#if defined(ARCH_ARM_8_4_AMU)
		// Tell non-HLOS guests that there is no AMU
		if (!vcpu_option_flags_get_hlos_vm(&thread->vcpu_options)) {
			ID_AA64PFR0_EL1_set_AMU(&id_aa64pfr0, 0);
		}
#endif

		val = ID_AA64PFR0_EL1_raw(id_aa64pfr0);
		break;
	}
	case ISS_MRS_MSR_ID_AA64PFR1_EL1:
		sysreg64_read(ID_AA64PFR1_EL1, val);
		break;
	case ISS_MRS_MSR_ID_AA64DFR0_EL1: {
		ID_AA64DFR0_EL1_t id_aa64dfr0 = register_ID_AA64DFR0_EL1_read();

		// The debug, trace, PMU and SPE modules must correctly support
		// the values reported by the hardware. All we do here is to
		// zero out fields for missing modules.

#if !VCPU_DEBUG_CONTEXT_SAVED
		// Note that ARMv8-A does not allow 0 (not implemented) in this
		// field. So this configuration is not really supported.
		ID_AA64DFR0_EL1_set_DebugVer(&id_aa64dfr0, 0U);
#endif

#if VCPU_TRACE_CONTEXT_SAVED
		// Only the HLOS VM is allowed to trace
		if (!vcpu_option_flags_get_hlos_vm(&thread->vcpu_options)) {
			ID_AA64DFR0_EL1_set_TraceVer(&id_aa64dfr0, 0U);
			ID_AA64DFR0_EL1_set_TraceFilt(&id_aa64dfr0, 0U);
		}
#else
		ID_AA64DFR0_EL1_set_TraceVer(&id_aa64dfr0, 0U);
		ID_AA64DFR0_EL1_set_TraceFilt(&id_aa64dfr0, 0U);
#endif

		// FIXME: for now we will give all VMs full control of PMU
		// hardware, therefore the code below is commented out until
		// advance PMU support has been added to the hypervisor.
		// No PMU support
		// ID_AA64DFR0_EL1_set_PMUVer(&id_aa64dfr0, 0);

#if !defined(MODULE_SPE)
		ID_AA64DFR0_EL1_set_PMSVer(&id_aa64dfr0, 0U);
#endif

		val = ID_AA64DFR0_EL1_raw(id_aa64dfr0);
		break;
	}
	case ISS_MRS_MSR_ID_AA64DFR1_EL1:
		sysreg64_read(ID_AA64DFR1_EL1, val);
		break;
	case ISS_MRS_MSR_ID_AA64AFR0_EL1:
		sysreg64_read(ID_AA64AFR0_EL1, val);
		break;
	case ISS_MRS_MSR_ID_AA64AFR1_EL1:
		break;
	case ISS_MRS_MSR_ID_AA64ISAR0_EL1:
		sysreg64_read(ID_AA64ISAR0_EL1, val);
		break;
	case ISS_MRS_MSR_ID_AA64ISAR1_EL1:
		sysreg64_read(ID_AA64ISAR1_EL1, val);
#if defined(QEMU) && QEMU
		ID_AA64ISAR1_EL1_t isar1 = ID_AA64ISAR1_EL1_cast(val);
		ID_AA64ISAR1_EL1_set_APA(&isar1, 0U);
		ID_AA64ISAR1_EL1_set_GPA(&isar1, 0U);
		val = ID_AA64ISAR1_EL1_raw(isar1);
#endif
		break;
	case ISS_MRS_MSR_ID_AA64MMFR0_EL1:
		sysreg64_read(ID_AA64MMFR0_EL1, val);
		break;
	case ISS_MRS_MSR_ID_AA64MMFR1_EL1:
		sysreg64_read(ID_AA64MMFR1_EL1, val);
		break;
	case ISS_MRS_MSR_ID_AA64MMFR2_EL1:
		sysreg64_read(ID_AA64MMFR2_EL1, val);
		break;
	default: {
		uint8_t opc0, opc1, crn, crm;

		opc0 = ESR_EL2_ISS_MSR_MRS_get_Op0(&iss);
		opc1 = ESR_EL2_ISS_MSR_MRS_get_Op1(&iss);
		crn  = ESR_EL2_ISS_MSR_MRS_get_CRn(&iss);
		crm  = ESR_EL2_ISS_MSR_MRS_get_CRm(&iss);

		if ((opc0 == 3) && (opc1 == 0) && (crn == 0) && (crm >= 2) &&
		    (crm <= 7)) {
			// It is IMPLEMENTATION DEFINED whether HC_EL2.TID3
			// traps MRS accesses to the registers in this range
			// (that have not been handled above). If we ever get
			// here print a debug message so we can investigate.
			TRACE_AND_LOG(DEBUG, WARN,
				      "sysreg_read: unhandled TID3 trap, "
				      "ISS: {:#x}. RAZ",
				      ESR_EL2_ISS_MSR_MRS_raw(iss));
			val = 0U;
		} else {
			ret = VCPU_TRAP_RESULT_UNHANDLED;
		}
		break;
	}
	}

	// Update the thread's register
	if (ret == VCPU_TRAP_RESULT_EMULATED) {
		vcpu_gpr_write(thread, reg_num, val);
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
	thread_t *	   thread = thread_get_self();

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
	thread_t *	   thread = thread_get_self();

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
		thread->vcpu_regs_el2.hcr_el2 = register_HCR_EL2_read();
		HCR_EL2_set_TSW(&thread->vcpu_regs_el2.hcr_el2, false);
		register_HCR_EL2_write(thread->vcpu_regs_el2.hcr_el2);
		ret = VCPU_TRAP_RESULT_RETRY;
		break;
	default:
		ret = VCPU_TRAP_RESULT_UNHANDLED;
		break;
	}

	return ret;
}

vcpu_trap_result_t
sysreg_write_fallback(ESR_EL2_ISS_MSR_MRS_t iss)
{
	vcpu_trap_result_t ret	  = VCPU_TRAP_RESULT_EMULATED;
	thread_t *	   thread = thread_get_self();

	// Read the thread's register
	uint8_t	   reg_num = ESR_EL2_ISS_MSR_MRS_get_Rt(&iss);
	register_t val	   = vcpu_gpr_read(thread, reg_num);

	// Remove the fields that are not used in the comparison
	ESR_EL2_ISS_MSR_MRS_t temp_iss = iss;
	ESR_EL2_ISS_MSR_MRS_set_Rt(&temp_iss, 0U);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&temp_iss, false);

	switch (ESR_EL2_ISS_MSR_MRS_raw(temp_iss)) {
	// The registers trapped with HCR_EL2.TVM
	case ISS_MRS_MSR_SCTLR_EL1:
		// FIXME: Act on the guest's modification of SCTLR_EL1
		register_SCTLR_EL1_write(SCTLR_EL1_cast(val));
		break;
	case ISS_MRS_MSR_TTBR0_EL1:
		register_TTBR0_EL1_write(TTBR0_EL1_cast(val));
		break;
	case ISS_MRS_MSR_TTBR1_EL1:
		register_TTBR1_EL1_write(TTBR1_EL1_cast(val));
		break;
	case ISS_MRS_MSR_TCR_EL1:
		register_TCR_EL1_write(TCR_EL1_cast(val));
		break;
	case ISS_MRS_MSR_ESR_EL1:
		register_ESR_EL1_write(ESR_EL1_cast(val));
		break;
	case ISS_MRS_MSR_FAR_EL1:
		register_FAR_EL1_write(FAR_EL1_cast(val));
		break;
	case ISS_MRS_MSR_AFSR0_EL1:
		register_AFSR0_EL1_write(AFSR0_EL1_cast(val));
		break;
	case ISS_MRS_MSR_AFSR1_EL1:
		register_AFSR1_EL1_write(AFSR1_EL1_cast(val));
		break;
	case ISS_MRS_MSR_MAIR_EL1:
		register_MAIR_EL1_write(MAIR_EL1_cast(val));
		break;
	case ISS_MRS_MSR_AMAIR_EL1:
		// WI
		break;
	case ISS_MRS_MSR_CONTEXTIDR_EL1:
		register_CONTEXTIDR_EL1_write(CONTEXTIDR_EL1_cast(val));
		break;
	default: {
		uint8_t opc0 = ESR_EL2_ISS_MSR_MRS_get_Op0(&iss);
		if (opc0 == 2) {
			// Debug registers, WI by default
		} else {
			ret = VCPU_TRAP_RESULT_UNHANDLED;
		}
		break;
	}
	}

	return ret;
}
