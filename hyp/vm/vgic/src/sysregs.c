// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <panic.h>
#include <preempt.h>
#include <scheduler.h>
#include <thread.h>
#include <util.h>
#include <vcpu.h>

#include <asm/system_registers.h>

#include "event_handlers.h"
#include "gich_lrs.h"
#include "internal.h"

vcpu_trap_result_t
vgic_handle_vcpu_trap_sysreg_write(ESR_EL2_ISS_MSR_MRS_t iss)
{
	vcpu_trap_result_t ret	  = VCPU_TRAP_RESULT_EMULATED;
	thread_t		 *thread = thread_get_self();
	vic_t	      *vic	  = thread->vgic_vic;
	if (vic == NULL) {
		ret = VCPU_TRAP_RESULT_UNHANDLED;
		goto out;
	}

	// Assert this is a write
	assert(!ESR_EL2_ISS_MSR_MRS_get_Direction(&iss));

	// Read the thread's register
	uint8_t	   reg_num = ESR_EL2_ISS_MSR_MRS_get_Rt(&iss);
	register_t val	   = vcpu_gpr_read(thread, reg_num);

	// Remove the fields that are not used in the comparison
	ESR_EL2_ISS_MSR_MRS_set_Rt(&iss, 0);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&iss, false);

	switch (ESR_EL2_ISS_MSR_MRS_raw(iss)) {
	case ISS_MRS_MSR_ICC_DIR_EL1:
		vgic_icc_irq_deactivate(
			vic, ICC_DIR_EL1_get_INTID(&ICC_DIR_EL1_cast(val)));
		break;

	case ISS_MRS_MSR_ICC_ASGI1R_EL1:
		// ICC_ASGI1R_EL1 is treated as an alias of ICC_SGI0R_EL1.
		// This is because virtual accesses are always non-secure, and
		// non-secure writes generate SGIs for group 0 or secure group
		// 1, where the latter is treated as group 0 too because
		// GICD_CTLR.DS=1.
	case ISS_MRS_MSR_ICC_SGI0R_EL1:
		vgic_icc_generate_sgi(vic, ICC_SGIR_EL1_cast(val), false);
		break;

	case ISS_MRS_MSR_ICC_SGI1R_EL1:
		vgic_icc_generate_sgi(vic, ICC_SGIR_EL1_cast(val), true);
		break;

	case ISS_MRS_MSR_ICC_SRE_EL1:
		// WI
		break;

	case ISS_MRS_MSR_ICC_IGRPEN0_EL1:
		vgic_icc_set_group_enable(false, ICC_IGRPEN_EL1_cast(val));
		break;

	case ISS_MRS_MSR_ICC_IGRPEN1_EL1:
		vgic_icc_set_group_enable(true, ICC_IGRPEN_EL1_cast(val));
		break;

#if !defined(ARCH_ARM_8_6_FGT) || !ARCH_ARM_8_6_FGT
	// Trapped by TALL[01] which are set to trap ICC_IGRPEN[01]_EL1
	case ISS_MRS_MSR_ICC_EOIR0_EL1: {
		// Drop the highest active priority (which we are allowed to
		// assume is the priority of the specified IRQ)
		gicv3_ich_ap0r_clear_highest();
		// Deactivate the interrupt, if EOImode is 0
		ICH_VMCR_EL2_t vmcr = register_ICH_VMCR_EL2_read();
		if (!ICH_VMCR_EL2_get_VEOIM(&vmcr)) {
			vgic_icc_irq_deactivate(
				vic, ICC_EOIR_EL1_get_INTID(
					     &ICC_EOIR_EL1_cast(val)));
		}
		break;
	}

	case ISS_MRS_MSR_ICC_BPR0_EL1: {
		preempt_disable();
		ICH_VMCR_EL2_t vmcr = register_ICH_VMCR_EL2_read();
		ICH_VMCR_EL2_set_VBPR0(&vmcr, (uint8_t)val);
		register_ICH_VMCR_EL2_write(vmcr);
		preempt_enable();
		break;
	}
	case ISS_MRS_MSR_ICC_AP0R0_EL1:
#if CPU_GICH_APR_COUNT >= 1U
		register_ICH_AP0R0_EL2_write((uint32_t)val);
#endif
		break;

	case ISS_MRS_MSR_ICC_AP0R1_EL1:
#if CPU_GICH_APR_COUNT >= 2U
		register_ICH_AP0R1_EL2_write((uint32_t)val);
#endif
		break;

	case ISS_MRS_MSR_ICC_AP0R2_EL1:
#if CPU_GICH_APR_COUNT >= 4U
		register_ICH_AP0R2_EL2_write((uint32_t)val);
#endif
		break;

	case ISS_MRS_MSR_ICC_AP0R3_EL1:
#if CPU_GICH_APR_COUNT >= 4U
		register_ICH_AP0R3_EL2_write((uint32_t)val);
#endif
		break;

	case ISS_MRS_MSR_ICC_EOIR1_EL1: {
		// Drop the highest active priority (which we are allowed to
		// assume is the priority of the specified IRQ)
		gicv3_ich_ap1r_clear_highest();
		// Deactivate the interrupt, if EOImode is 0
		ICH_VMCR_EL2_t vmcr = register_ICH_VMCR_EL2_read();
		if (!ICH_VMCR_EL2_get_VEOIM(&vmcr)) {
			vgic_icc_irq_deactivate(
				vic, ICC_EOIR_EL1_get_INTID(
					     &ICC_EOIR_EL1_cast(val)));
		}
		break;
	}

	case ISS_MRS_MSR_ICC_BPR1_EL1: {
		preempt_disable();
		ICH_VMCR_EL2_t vmcr = register_ICH_VMCR_EL2_read();
		ICH_VMCR_EL2_set_VBPR1(&vmcr, (uint8_t)val);
		register_ICH_VMCR_EL2_write(vmcr);
		preempt_enable();
		break;
	}

	case ISS_MRS_MSR_ICC_AP1R0_EL1:
#if CPU_GICH_APR_COUNT >= 1U
		register_ICH_AP1R0_EL2_write((uint32_t)val);
#endif
		break;

	case ISS_MRS_MSR_ICC_AP1R1_EL1:
#if CPU_GICH_APR_COUNT >= 2U
		register_ICH_AP1R1_EL2_write((uint32_t)val);
#endif
		break;

	case ISS_MRS_MSR_ICC_AP1R2_EL1:
#if CPU_GICH_APR_COUNT >= 4U
		register_ICH_AP1R2_EL2_write((uint32_t)val);
#endif
		break;

	case ISS_MRS_MSR_ICC_AP1R3_EL1:
#if CPU_GICH_APR_COUNT >= 4U
		register_ICH_AP1R3_EL2_write((uint32_t)val);
#endif
		break;
#endif

	default:
		ret = VCPU_TRAP_RESULT_UNHANDLED;
		break;
	}

out:
	return ret;
}

vcpu_trap_result_t
vgic_handle_vcpu_trap_sysreg_read(ESR_EL2_ISS_MSR_MRS_t iss)
{
	register_t	   val	  = 0U;
	vcpu_trap_result_t ret	  = VCPU_TRAP_RESULT_EMULATED;
	thread_t		 *thread = thread_get_self();

	// Assert this is a read
	assert(ESR_EL2_ISS_MSR_MRS_get_Direction(&iss));

	uint8_t reg_num = ESR_EL2_ISS_MSR_MRS_get_Rt(&iss);

	// Remove the fields that are not used in the comparison
	ESR_EL2_ISS_MSR_MRS_t temp_iss = iss;
	ESR_EL2_ISS_MSR_MRS_set_Rt(&temp_iss, 0U);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&temp_iss, false);

	switch (ESR_EL2_ISS_MSR_MRS_raw(temp_iss)) {
	case ISS_MRS_MSR_ICC_SRE_EL1: {
		// Return 1 for SRE, DFB and DIB
		ICC_SRE_EL1_t sre;
		ICC_SRE_EL1_init(&sre);
		ICC_SRE_EL1_set_SRE(&sre, true);
		ICC_SRE_EL1_set_DFB(&sre, true);
		ICC_SRE_EL1_set_DIB(&sre, true);
		val = ICC_SRE_EL1_raw(sre);
		break;
	}

	case ISS_MRS_MSR_ICC_IGRPEN0_EL1: {
		ICC_IGRPEN_EL1_t igrpen = ICC_IGRPEN_EL1_default();
		ICC_IGRPEN_EL1_set_Enable(&igrpen, thread->vgic_group0_enabled);
		val = ICC_IGRPEN_EL1_raw(igrpen);
		break;
	}

	case ISS_MRS_MSR_ICC_IGRPEN1_EL1: {
		ICC_IGRPEN_EL1_t igrpen = ICC_IGRPEN_EL1_default();
		ICC_IGRPEN_EL1_set_Enable(&igrpen, thread->vgic_group1_enabled);
		val = ICC_IGRPEN_EL1_raw(igrpen);
		break;
	}

#if !defined(ARCH_ARM_8_6_FGT) || !ARCH_ARM_8_6_FGT
	// Trapped by TALL[01] which are set to trap ICC_IGRPEN[01]_EL1
	case ISS_MRS_MSR_ICC_IAR0_EL1:
	case ISS_MRS_MSR_ICC_HPPIR0_EL1:
		// We should only get this trap when the group is disabled, so
		// there can't be any deliverable IRQs; return 1023, which is
		// the reserved value meaning no pending interrupt.
		//
		// Note that the reserved IAR0 values that indicate a pending
		// group 1 interrupt (1020 or 1021) can only be returned to EL3
		// reads as of GICv3, so we don't need to check group 1.
		assert(!thread->vgic_group0_enabled);
		val = 1023U;
		break;

	case ISS_MRS_MSR_ICC_BPR0_EL1: {
		ICH_VMCR_EL2_t vmcr = register_ICH_VMCR_EL2_read();
		val		    = ICH_VMCR_EL2_get_VBPR0(&vmcr);
		break;
	}

	case ISS_MRS_MSR_ICC_AP0R0_EL1:
#if CPU_GICH_APR_COUNT >= 1U
		val = register_ICH_AP0R0_EL2_read();
#endif
		break;

	case ISS_MRS_MSR_ICC_AP0R1_EL1:
#if CPU_GICH_APR_COUNT >= 2U
		val = register_ICH_AP0R1_EL2_read();
#endif
		break;

	case ISS_MRS_MSR_ICC_AP0R2_EL1:
#if CPU_GICH_APR_COUNT >= 4U
		val = register_ICH_AP0R2_EL2_read();
#endif
		break;

	case ISS_MRS_MSR_ICC_AP0R3_EL1:
#if CPU_GICH_APR_COUNT >= 4U
		val = register_ICH_AP0R3_EL2_read();
#endif
		break;

	case ISS_MRS_MSR_ICC_IAR1_EL1:
	case ISS_MRS_MSR_ICC_HPPIR1_EL1:
		// We should only get this trap when the group is disabled, so
		// there can't be any deliverable IRQs; return 1023, which is
		// the reserved value meaning no pending interrupt.
		assert(!thread->vgic_group1_enabled);
		val = 1023U;
		break;

	case ISS_MRS_MSR_ICC_BPR1_EL1: {
		ICH_VMCR_EL2_t vmcr = register_ICH_VMCR_EL2_read();
		val		    = ICH_VMCR_EL2_get_VBPR1(&vmcr);
		break;
	}

	case ISS_MRS_MSR_ICC_AP1R0_EL1:
#if CPU_GICH_APR_COUNT >= 1U
		val = register_ICH_AP1R0_EL2_read();
#endif
		break;

	case ISS_MRS_MSR_ICC_AP1R1_EL1:
#if CPU_GICH_APR_COUNT >= 2U
		val = register_ICH_AP1R1_EL2_read();
#endif
		break;

	case ISS_MRS_MSR_ICC_AP1R2_EL1:
#if CPU_GICH_APR_COUNT >= 4U
		val = register_ICH_AP1R2_EL2_read();
#endif
		break;

	case ISS_MRS_MSR_ICC_AP1R3_EL1:
#if CPU_GICH_APR_COUNT >= 4U
		val = register_ICH_AP1R3_EL2_read();
#endif
		break;
#endif

	default:
		ret = VCPU_TRAP_RESULT_UNHANDLED;
		break;
	}

	// Update the thread's register
	if (ret == VCPU_TRAP_RESULT_EMULATED) {
		vcpu_gpr_write(thread, reg_num, val);
	}

	return ret;
}
