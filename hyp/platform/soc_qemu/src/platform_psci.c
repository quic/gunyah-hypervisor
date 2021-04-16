// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#if !defined(UNIT_TESTS)
#include <platform_psci.h>
#include <util.h>

#include "event_handlers.h"

bool
platform_psci_is_cpu_active(psci_cpu_state_t cpu_state)
{
	// We will treat a cpu as active if it's none zero since QEMU does not
	// care about cpu states
	return (cpu_state == 0U);
}

bool
platform_psci_is_cpu_poweroff(psci_cpu_state_t cpu_state)
{
	(void)cpu_state;

	// Powerdown not supported in QEMU, it always goes into WFI
	return false;
}

psci_cpu_state_t
platform_psci_get_cpu_state(psci_suspend_powerstate_t suspend_state)
{
	psci_suspend_powerstate_stateid_t stateid =
		psci_suspend_powerstate_get_StateID(&suspend_state);

	return psci_suspend_powerstate_stateid_get_cpu(&stateid);
}

void
platform_psci_set_cpu_state(psci_suspend_powerstate_t *suspend_state,
			    psci_cpu_state_t	       cpu_state)
{
	psci_suspend_powerstate_stateid_t stateid =
		psci_suspend_powerstate_get_StateID(suspend_state);
	psci_suspend_powerstate_stateid_set_cpu(&stateid, cpu_state);
	psci_suspend_powerstate_set_StateID(suspend_state, stateid);
}

psci_cpu_state_t
platform_psci_shallowest_cpu_state(psci_cpu_state_t state1,
				   psci_cpu_state_t state2)
{
	return (psci_cpu_state_t)(util_min(state1, state2));
}

psci_cpu_state_t
platform_psci_deepest_cpu_state(cpu_index_t cpu)
{
	(void)cpu;

	// Since QEMU does not care about cpu suspend states, we will use 0 as
	// active and non-zero as suspended.
	return (psci_cpu_state_t)(1);
}

psci_suspend_powerstate_stateid_t
platform_psci_deepest_cpu_level_stateid(cpu_index_t cpu)
{
	(void)cpu;

	// Since QEMU does not care about cpu suspend states, we'll use 0 as
	// active and non-zero as suspended.
	return psci_suspend_powerstate_stateid_cast(1);
}

psci_ret_t
platform_psci_suspend_state_validation(psci_suspend_powerstate_t suspend_state,
				       cpu_index_t cpu, psci_mode_t psci_mode)
{
	(void)suspend_state;
	(void)cpu;
	(void)psci_mode;

	// QEMU does not care about suspend states since it only goes to WFI.
	return PSCI_RET_SUCCESS;
}
#endif
