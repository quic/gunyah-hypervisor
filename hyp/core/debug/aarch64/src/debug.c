// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <cpulocal.h>

#include <events/debug.h>

#include <asm/barrier.h>

#include "debug_bps.h"
#include "event_handlers.h"

#if PLATFORM_DEBUG_SAVE_STATE
static struct asm_ordering_dummy debug_asm_order;

CPULOCAL_DECLARE_STATIC(debug_ext_state_t, debug_ext_state);

static void
debug_os_lock(void)
{
	OSLAR_EL1_t oslar = OSLAR_EL1_default();
	OSLAR_EL1_set_oslk(&oslar, true);
	register_OSLAR_EL1_write_ordered(oslar, &debug_asm_order);
	asm_context_sync_ordered(&debug_asm_order);
}

static void
debug_os_unlock(void)
{
	OSLAR_EL1_t oslar = OSLAR_EL1_default();
	OSLAR_EL1_set_oslk(&oslar, false);
	register_OSLAR_EL1_write_ordered(oslar, &debug_asm_order);
	asm_context_sync_ordered(&debug_asm_order);
}

static inline bool
debug_force_save_ext(void)
{
	return PLATFORM_DEBUG_SAVE_STATE > 1U;
}

void
debug_handle_power_cpu_online(void)
{
	debug_os_unlock();
}

error_t
debug_handle_power_cpu_suspend(bool may_poweroff)
{
	if (may_poweroff) {
		debug_ext_state_t *state = &CPULOCAL(debug_ext_state);

		debug_os_lock();

#if defined(PLATFORM_HAS_NO_DBGCLAIM_EL1) && PLATFORM_HAS_NO_DBGCLAIM_EL1
		state->dbgclaim = DBGCLAIM_EL1_default();
#else
		state->dbgclaim =
			register_DBGCLAIMCLR_EL1_read_ordered(&debug_asm_order);
#endif

		if (debug_force_save_ext() ||
		    DBGCLAIM_EL1_get_debug_ext(&state->dbgclaim)) {
			state->mdccint = register_MDCCINT_EL1_read_ordered(
				&debug_asm_order);
			debug_save_common(&state->common, &debug_asm_order);
			state->dtrrx = register_OSDTRRX_EL1_read_ordered(
				&debug_asm_order);
			state->dtrtx = register_OSDTRTX_EL1_read_ordered(
				&debug_asm_order);
			state->eccr = register_OSECCR_EL1_read_ordered(
				&debug_asm_order);
		}
	}

	return OK;
}

void
debug_unwind_power_cpu_suspend(bool may_poweroff)
{
	if (may_poweroff) {
		debug_os_unlock();
	}
}

void
debug_handle_power_cpu_resume(bool was_poweroff)
{
	if (was_poweroff) {
		debug_ext_state_t *state = &CPULOCAL(debug_ext_state);

		if (debug_force_save_ext() ||
		    DBGCLAIM_EL1_get_debug_ext(&state->dbgclaim)) {
			// Lock just in case; the lock should already be set
			debug_os_lock();

			register_DBGCLAIMSET_EL1_write_ordered(
				state->dbgclaim, &debug_asm_order);
			register_MDCCINT_EL1_write_ordered(state->mdccint,
							   &debug_asm_order);
			debug_load_common(&state->common, &debug_asm_order);
			register_OSDTRRX_EL1_write_ordered(state->dtrrx,
							   &debug_asm_order);
			register_OSDTRTX_EL1_write_ordered(state->dtrtx,
							   &debug_asm_order);
			register_OSECCR_EL1_write_ordered(state->eccr,
							  &debug_asm_order);
			asm_context_sync_ordered(&debug_asm_order);
		}
	}

	debug_os_unlock();
}
#endif
