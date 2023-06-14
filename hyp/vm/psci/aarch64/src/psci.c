// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <hypconstants.h>
#include <hypregisters.h>

#include <platform_cpu.h>

#include "event_handlers.h"
#include "psci_arch.h"

void
psci_handle_scheduler_selected_thread(thread_t *thread, bool *can_idle)
{
	if (thread->vpm_mode == VPM_MODE_IDLE) {
		// This thread can't be allowed to disable the WFI trap,
		// because WFI votes to suspend the physical CPU.
		*can_idle = false;
	}
}

vcpu_trap_result_t
psci_handle_vcpu_trap_wfi(void)
{
	return psci_handle_trapped_idle() ? VCPU_TRAP_RESULT_EMULATED
					  : VCPU_TRAP_RESULT_UNHANDLED;
}
