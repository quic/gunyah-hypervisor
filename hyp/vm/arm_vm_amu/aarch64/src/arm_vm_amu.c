// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <cpulocal.h>
#include <panic.h>
#include <preempt.h>
#include <scheduler.h>
#include <vcpu.h>

#include <asm/barrier.h>
#include <asm/sysregs.h>
#include <asm/system_registers.h>

#include "arm_vm_amu.h"
#include "event_handlers.h"

// The design:
// Only HLOS is given access to the AMU component. However, HLOS should not see
// how much the counters increment during the execution of the sensitive VMs.
//
// Unfortunately, only the highest EL can write to the AMU control registers and
// counters; therefore we can't protect against the AMU cross-exposure
// by simply disabling the counters dynamically or context switching them.
//
// Therefore we use a set of CPU-local variables to keep track of how much each
// counter increments during the sensitive threads. We do this by subtracting
// the counter value from our variable before switching to a sensitive thread,
// and adding the counter value when switching away from it.
//
// All the AMU accesses from HLOS are trapped. When HLOS tries to read a counter
// we return the hardware value minus our internal offset from above.
// The AMU counters take centuries to overflow, so arithmetic overflows are not
// a concern.
//
// This is not needed for counter 1 ("constant frequency cycles" counter),
// which is essentially defined the same as the ARM physical counter and
// virtualising it does not provide any additional security.

#if defined(ARCH_ARM_FEAT_AMUv1p1)
#error Implement the AMU virtual offset registers
#error Investigate the need to support non-consecutive auxiliary counters
#endif

#if defined(ARCH_ARM_FEAT_AMUv1) || defined(ARCH_ARM_FEAT_AMUv1p1)
CPULOCAL_DECLARE_STATIC(uint64_t, amu_counter_offsets)[PLATFORM_AMU_CNT_NUM];
CPULOCAL_DECLARE_STATIC(uint64_t, amu_aux_counter_offsets)
[PLATFORM_AMU_AUX_CNT_NUM];

void
arm_vm_amu_handle_boot_cpu_cold_init(cpu_index_t cpu_index)
{
	uint64_t *amu_counter_offsets =
		CPULOCAL_BY_INDEX(amu_counter_offsets, cpu_index);
	uint64_t *amu_aux_counter_offsets =
		CPULOCAL_BY_INDEX(amu_aux_counter_offsets, cpu_index);

	for (index_t i = 0; i < PLATFORM_AMU_CNT_NUM; i++) {
		amu_counter_offsets[i] = 0;
	}
	for (index_t i = 0; i < PLATFORM_AMU_AUX_CNT_NUM; i++) {
		amu_aux_counter_offsets[i] = 0;
	}

	AMCFGR_EL0_t amcfgr = register_AMCFGR_EL0_read();
	AMCGCR_EL0_t amcgcr = register_AMCGCR_EL0_read();

#if defined(ARCH_ARM_FEAT_AMUv1p1)
#error TODO: Check the AMU counter bitmap
#else
	if ((AMCFGR_EL0_get_N(&amcfgr) + 1U) !=
	    (PLATFORM_AMU_CNT_NUM + PLATFORM_AMU_AUX_CNT_NUM)) {
		panic("Incorrect CPU AMU count");
	}
#endif
	if ((AMCGCR_EL0_get_CG0NC(&amcgcr) != PLATFORM_AMU_CNT_NUM) ||
	    (AMCGCR_EL0_get_CG1NC(&amcgcr) != PLATFORM_AMU_AUX_CNT_NUM)) {
		panic("Incorrect CPU AMU group counts");
	}
}

bool
arm_vm_amu_handle_vcpu_activate_thread(thread_t		  *thread,
				       vcpu_option_flags_t options)
{
	assert(thread != NULL);
	assert(thread->kind == THREAD_KIND_VCPU);

	// Trap accesses to AMU registers. For HLOS we will emulate
	// them, for the rest of the VMs we will leave them unhandled
	// and inject an abort.
	CPTR_EL2_E2H1_set_TAM(&thread->vcpu_regs_el2.cptr_el2, true);

	vcpu_option_flags_set_amu_counting_disabled(
		&thread->vcpu_options,
		vcpu_option_flags_get_amu_counting_disabled(&options));

	return true;
}

error_t
arm_vm_amu_handle_thread_context_switch_pre(thread_t *next)
{
	// If about to switch to a sensitive thread, take a snapshot of the AMU
	// counters by subtracting them from the offsets.
	// In theory it is not necessary to do this if we are coming from
	// another sensitive thread, but adding the required extra checks will
	// likely degrade the performance as this will be a rare occurrence.
	if (compiler_unexpected((next->kind == THREAD_KIND_VCPU) &&
				(vcpu_option_flags_get_amu_counting_disabled(
					&next->vcpu_options)))) {
		cpulocal_begin();
		arm_vm_amu_subtract_counters(&CPULOCAL(amu_counter_offsets));
		arm_vm_amu_subtract_aux_counters(
			&CPULOCAL(amu_aux_counter_offsets));
		cpulocal_end();
	}

	return OK;
}

void
arm_vm_amu_handle_thread_context_switch_post(thread_t *prev)
{
	// If about to switch away from a sensitive thread, take a snapshot of
	// the AMU counters by adding them to the offsets.
	// In theory it is not necessary to do this if we are switching to
	// another sensitive thread, but adding the required extra checks will
	// likely degrade the performance as this will be a rare occurrence.
	if (compiler_unexpected((prev->kind == THREAD_KIND_VCPU) &&
				(vcpu_option_flags_get_amu_counting_disabled(
					&prev->vcpu_options)))) {
		cpulocal_begin();
		arm_vm_amu_add_counters(&CPULOCAL(amu_counter_offsets));
		arm_vm_amu_add_aux_counters(&CPULOCAL(amu_aux_counter_offsets));
		cpulocal_end();
	}
}

static vcpu_trap_result_t
arm_vm_amu_get_event_register(ESR_EL2_ISS_MSR_MRS_t iss, uint64_t *val)
{
	vcpu_trap_result_t ret;
	uint8_t		   opc0 = ESR_EL2_ISS_MSR_MRS_get_Op0(&iss);
	uint8_t		   opc1 = ESR_EL2_ISS_MSR_MRS_get_Op1(&iss);
	uint8_t		   crn	= ESR_EL2_ISS_MSR_MRS_get_CRn(&iss);

	if ((opc0 == 3U) && (opc1 == 3U) && (crn == 13U)) {
		uint8_t crm   = ESR_EL2_ISS_MSR_MRS_get_CRm(&iss);
		uint8_t opc2  = ESR_EL2_ISS_MSR_MRS_get_Op2(&iss);
		uint8_t index = (uint8_t)((crm & 1U) << 2U) | opc2;

		if (((crm == 4U) || (crm == 5U)) &&
		    (index < PLATFORM_AMU_CNT_NUM)) {
			// Event counter registers
			cpulocal_begin();
			uint64_t *offsets = CPULOCAL(amu_counter_offsets);
			*val		  = arm_vm_amu_get_counter(index);
			if (index != 1U) {
				// Adjust the counter value
				*val -= offsets[index];
			}
			cpulocal_end();

			ret = VCPU_TRAP_RESULT_EMULATED;
		} else if (((crm == 6U) || (crm == 7U)) &&
			   (index < PLATFORM_AMU_CNT_NUM)) {
			// Event type registers
			*val = arm_vm_amu_get_event_type(index);

			ret = VCPU_TRAP_RESULT_EMULATED;
		} else if (((crm == 12U) || (crm == 13U)) &&
			   (index < PLATFORM_AMU_AUX_CNT_NUM)) {
			// Auxiliary event counter registers
			cpulocal_begin();
			uint64_t *offsets = CPULOCAL(amu_aux_counter_offsets);
			*val		  = arm_vm_amu_get_aux_counter(index);
			// Adjust the counter value
			*val -= offsets[index];
			cpulocal_end();

			ret = VCPU_TRAP_RESULT_EMULATED;
		} else if (((crm == 14U) || (crm == 15U)) &&
			   (index < PLATFORM_AMU_AUX_CNT_NUM)) {
			// Auxiliary event type registers
			*val = arm_vm_amu_get_aux_event_type(index);

			ret = VCPU_TRAP_RESULT_EMULATED;
		} else {
			// Not an AMU event register
			ret = VCPU_TRAP_RESULT_UNHANDLED;
		}
	} else {
		ret = VCPU_TRAP_RESULT_UNHANDLED;
	}

	return ret;
}

vcpu_trap_result_t
arm_vm_amu_handle_vcpu_trap_sysreg_read(ESR_EL2_ISS_MSR_MRS_t iss)
{
	register_t	   val	  = 0U;
	vcpu_trap_result_t ret	  = VCPU_TRAP_RESULT_EMULATED;
	thread_t	  *thread = thread_get_self();

	if (!vcpu_option_flags_get_hlos_vm(&thread->vcpu_options)) {
		// Only HLOS is allowed to read the AMU registers
		ret = VCPU_TRAP_RESULT_UNHANDLED;
		goto out;
	}

	// Assert this is a read
	assert(ESR_EL2_ISS_MSR_MRS_get_Direction(&iss));

	uint8_t reg_num = ESR_EL2_ISS_MSR_MRS_get_Rt(&iss);

	// Remove the fields that are not used in the comparison
	ESR_EL2_ISS_MSR_MRS_t temp_iss = iss;
	ESR_EL2_ISS_MSR_MRS_set_Rt(&temp_iss, 0U);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&temp_iss, false);

	switch (ESR_EL2_ISS_MSR_MRS_raw(temp_iss)) {
	case ISS_MRS_MSR_AMCR_EL0: {
		AMCR_EL0_t amcr = AMCR_EL0_default();
		val		= AMCR_EL0_raw(amcr);
		break;
	}
	case ISS_MRS_MSR_AMCFGR_EL0: {
		AMCFGR_EL0_t amcfgr    = AMCFGR_EL0_default();
		AMCFGR_EL0_t amcfgr_hw = register_AMCFGR_EL0_read();

		AMCFGR_EL0_copy_HDBG(&amcfgr, &amcfgr_hw);
		AMCFGR_EL0_set_Size(&amcfgr, 63U);
		// With traps, it is possible to virtualise the number of HW
		// counters; return the number of emulated counters.
		AMCFGR_EL0_set_N(&amcfgr,
				 (uint16_t)(PLATFORM_AMU_CNT_NUM +
					    PLATFORM_AMU_AUX_CNT_NUM - 1U));
		AMCFGR_EL0_set_NCG(&amcfgr,
				   PLATFORM_AMU_AUX_CNT_NUM > 0U ? 1U : 0U);
		val = AMCFGR_EL0_raw(amcfgr);
		break;
	}
	case ISS_MRS_MSR_AMCGCR_EL0: {
		AMCGCR_EL0_t amcgcr = AMCGCR_EL0_default();

		// With traps, it is possible to virtualise the number of HW
		// counters; return the number of emulated counters.
		AMCGCR_EL0_set_CG0NC(&amcgcr, PLATFORM_AMU_CNT_NUM);
		AMCGCR_EL0_set_CG1NC(&amcgcr, PLATFORM_AMU_AUX_CNT_NUM);
		val = AMCGCR_EL0_raw(amcgcr);
		break;
	}
	case ISS_MRS_MSR_AMUSERENR_EL0:
		val = register_AMUSERENR_EL0_read();
		break;
#if defined(ARCH_ARM_FEAT_AMUv1p1)
	case ISS_MRS_MSR_AMCG1IDR_EL0:
		val = register_AMCG1IDR_EL0_read();
		break;
#endif
	default:
		ret = arm_vm_amu_get_event_register(iss, &val);
		break;
	}

	// Update the thread's register
	if (ret == VCPU_TRAP_RESULT_EMULATED) {
		vcpu_gpr_write(thread, reg_num, val);
	}

out:
	return ret;
}

vcpu_trap_result_t
arm_vm_amu_handle_vcpu_trap_sysreg_write(ESR_EL2_ISS_MSR_MRS_t iss)
{
	vcpu_trap_result_t ret	  = VCPU_TRAP_RESULT_UNHANDLED;
	thread_t	  *thread = thread_get_self();

	if (!vcpu_option_flags_get_hlos_vm(&thread->vcpu_options)) {
		// Only HLOS is allowed to modify the AMU registers
		goto out;
	}

	// Assert this is a write
	assert(!ESR_EL2_ISS_MSR_MRS_get_Direction(&iss));

	// Read the thread's register
	uint8_t	   reg_num = ESR_EL2_ISS_MSR_MRS_get_Rt(&iss);
	register_t val	   = vcpu_gpr_read(thread, reg_num);

	// Remove the fields that are not used in the comparison
	ESR_EL2_ISS_MSR_MRS_t temp_iss = iss;
	ESR_EL2_ISS_MSR_MRS_set_Rt(&temp_iss, 0U);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&temp_iss, false);

	// Only AMUSERNR_EL0 may be written by the guest
	switch (ESR_EL2_ISS_MSR_MRS_raw(temp_iss)) {
	case ISS_MRS_MSR_AMUSERENR_EL0: {
		SPSR_EL2_A64_t	  spsr_el2 = thread->vcpu_regs_gpr.spsr_el2.a64;
		spsr_64bit_mode_t spsr_m   = SPSR_EL2_A64_get_M(&spsr_el2);
		// A trap from EL0 means a HW bug
		assert((spsr_m & 0xfU) != 0U);

		register_AMUSERENR_EL0_write(val);
		ret = VCPU_TRAP_RESULT_EMULATED;
		break;
	}
	default:
		// Do Nothing
		break;
	}

out:
	return ret;
}
#endif
