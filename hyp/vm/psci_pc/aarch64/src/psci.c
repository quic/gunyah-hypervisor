// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <hypconstants.h>
#include <hypregisters.h>

#include <platform_cpu.h>

#include "event_handlers.h"
#include "psci_arch.h"

// The CPU ID values have the same format as MPIDR, but with all other fields
// masked out. This includes a bit that is forced to 1 in MPIDR_EL1_t, so we
// must mask off the affinity fields.
static const register_t mpidr_mask = MPIDR_EL1_AFF0_MASK | MPIDR_EL1_AFF1_MASK |
				     MPIDR_EL1_AFF2_MASK | MPIDR_EL1_AFF3_MASK;

psci_mpidr_t
psci_thread_get_mpidr(thread_t *thread)
{
	return psci_mpidr_cast(MPIDR_EL1_raw(thread->vcpu_regs_mpidr_el1) &
			       mpidr_mask);
}

psci_mpidr_t
psci_thread_set_mpidr_by_index(thread_t *thread, cpu_index_t index)
{
	psci_mpidr_t ret	    = platform_cpu_index_to_mpidr(index);
	MPIDR_EL1_t  real	    = register_MPIDR_EL1_read();
	thread->vcpu_regs_mpidr_el1 = MPIDR_EL1_default();
	MPIDR_EL1_set_Aff0(&thread->vcpu_regs_mpidr_el1,
			   psci_mpidr_get_Aff0(&ret));
	MPIDR_EL1_set_Aff1(&thread->vcpu_regs_mpidr_el1,
			   psci_mpidr_get_Aff1(&ret));
	MPIDR_EL1_set_Aff2(&thread->vcpu_regs_mpidr_el1,
			   psci_mpidr_get_Aff2(&ret));
	MPIDR_EL1_set_Aff3(&thread->vcpu_regs_mpidr_el1,
			   psci_mpidr_get_Aff3(&ret));
	MPIDR_EL1_set_MT(&thread->vcpu_regs_mpidr_el1, MPIDR_EL1_get_MT(&real));
	return ret;
}

void
psci_pc_handle_scheduler_selected_thread(thread_t *thread, bool *can_idle)
{
	if (thread->psci_mode == VPM_MODE_IDLE) {
		// This thread can't be allowed to disable the WFI trap,
		// because WFI votes to suspend the physical CPU.
		*can_idle = false;
	}
}

vcpu_trap_result_t
psci_pc_handle_vcpu_trap_wfi(void)
{
	return psci_pc_handle_trapped_idle() ? VCPU_TRAP_RESULT_EMULATED
					     : VCPU_TRAP_RESULT_UNHANDLED;
}
