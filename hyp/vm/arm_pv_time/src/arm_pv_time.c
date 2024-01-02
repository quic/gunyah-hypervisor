// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <atomic.h>
#include <compiler.h>
#include <panic.h>
#include <platform_timer.h>
#include <thread.h>
#include <trace.h>
#include <util.h>

#include "event_handlers.h"

#if !defined(MODULE_VM_VGIC)
#error Unable to determine a unique VCPU index (vgic_gicr_index not present)
#endif

bool
smccc_pv_time_features(uint64_t arg1, uint64_t *ret0)
{
	smccc_function_id_t fn_id    = smccc_function_id_cast((uint32_t)arg1);
	bool		    is_smc64 = smccc_function_id_get_is_smc64(&fn_id);
	bool		    is_fast  = smccc_function_id_get_is_fast(&fn_id);
	uint32_t	    res0     = smccc_function_id_get_res0(&fn_id);
	smccc_function_t    fn	     = smccc_function_id_get_function(&fn_id);
	smccc_owner_id_t    owner_id = smccc_function_id_get_owner_id(&fn_id);
	uint64_t	    ret	     = SMCCC_UNKNOWN_FUNCTION64;

	if ((owner_id == SMCCC_OWNER_ID_STANDARD_HYP) && (res0 == 0U) &&
	    is_fast && is_smc64) {
		switch ((smccc_standard_hyp_function_t)fn) {
		case SMCCC_STANDARD_HYP_FUNCTION_PV_TIME_FEATURES:
			ret = 0;
			break;
		case SMCCC_STANDARD_HYP_FUNCTION_PV_TIME_ST: {
			thread_t *current = thread_get_self();
			if (current->addrspace->info_area.me != NULL) {
				ret = 0;
			}
			break;
		}
		case SMCCC_STANDARD_HYP_FUNCTION_CALL_COUNT:
		case SMCCC_STANDARD_HYP_FUNCTION_CALL_UID:
		case SMCCC_STANDARD_HYP_FUNCTION_REVISION:
		default:
			// Nothing to do.
			break;
		}
	}

	*ret0 = ret;
	return true;
}

bool
smccc_pv_time_st(uint64_t arg1, uint64_t *ret0)
{
	(void)arg1;

	thread_t *current = thread_get_self();
	uint64_t  ret	  = SMCCC_UNKNOWN_FUNCTION64;

	if (current->addrspace->info_area.me != NULL) {
		index_t index = current->vgic_gicr_index;
		assert(index < PLATFORM_MAX_CORES);
		size_t offset = offsetof(addrspace_info_area_layout_t,
					 pv_time_data[index]);
		assert((offset + sizeof(pv_time_data_t)) <=
		       current->addrspace->info_area.me->size);
		ret = current->addrspace->info_area.ipa + offset;
	}

	*ret0 = ret;
	return true;
}

error_t
arm_pv_time_handle_object_create_thread(thread_create_t thread_create)
{
	thread_t *thread = thread_create.thread;
	assert(thread != NULL);

	arm_pv_time_self_block_state_t new_state =
		arm_pv_time_self_block_state_default();
	arm_pv_time_self_block_state_set_block(
		&new_state, SCHEDULER_BLOCK_THREAD_LIFECYCLE);
	atomic_store_relaxed(&thread->arm_pv_time.self_block, new_state);

	return OK;
}

bool
arm_pv_time_handle_vcpu_activate_thread(thread_t *thread)
{
	assert(thread != NULL);

	arm_pv_time_self_block_state_t new_state =
		arm_pv_time_self_block_state_default();
	arm_pv_time_self_block_state_set_block(&new_state,
					       SCHEDULER_BLOCK_VCPU_OFF);
	atomic_store_relaxed(&thread->arm_pv_time.self_block, new_state);

	if ((thread->addrspace->info_area.me != NULL)) {
		index_t index = thread->vgic_gicr_index;
		assert(index < PLATFORM_MAX_CORES);
		assert(thread->addrspace->info_area.hyp_va != NULL);
		thread->arm_pv_time.data = &thread->addrspace->info_area.hyp_va
						    ->pv_time_data[index];

		thread->arm_pv_time.data->revision   = 0U;
		thread->arm_pv_time.data->attributes = 0U;
		atomic_init(&thread->arm_pv_time.data->stolen_ns, 0U);
	}

	return true;
}

void
arm_pv_time_handle_scheduler_schedule(thread_t *current, thread_t *yielded_from,
				      ticks_t schedtime, ticks_t curticks)
{
	assert(current == thread_get_self());
	assert(curticks >= schedtime);

	// Avoid counting time in directed yields as stolen
	if (yielded_from != NULL) {
		yielded_from->arm_pv_time.yield_time += curticks - schedtime;
		TRACE(DEBUG, INFO,
		      "arm_pv_time: {:#x} added yield time {:d}, curticks {:d}",
		      (uintptr_t)yielded_from, curticks - schedtime,
		      yielded_from->arm_pv_time.yield_time);
	}
}

void
arm_pv_time_handle_thread_context_switch_post(ticks_t curticks,
					      ticks_t prevticks)
{
	thread_t *current = thread_get_self();

	// The start of the stolen time period is the absolute time the thread
	// stopped running plus the relative time it spent yielding, or the
	// absolute time the thread was last unblocked after blocking itself,
	// whichever is later.
	ticks_t adjusted_last_run = prevticks + current->arm_pv_time.yield_time;
	// Note that the scheduler reads curticks before acquiring any locks,
	// so it is possible that the last unblock occurred on a remote CPU
	// _after_ curticks. Checking for this explicitly is probably cheaper
	// than the synchronisation required to prevent it.
	arm_pv_time_self_block_state_t state =
		atomic_load_relaxed(&current->arm_pv_time.self_block);
	ticks_t last_self_unblock = util_min(
		curticks,
		arm_pv_time_self_block_state_get_last_unblocked(&state));
	ticks_t steal_start = util_max(last_self_unblock, adjusted_last_run);

	TRACE(DEBUG, INFO,
	      "arm_pv_time: {:#x} increment steal time by {:d}ns; "
	      "last run at {:d}ns (+ {:d}ns yielding), unblocked at {:d}ns",
	      (uintptr_t)current,
	      platform_timer_convert_ticks_to_ns(curticks - steal_start),
	      platform_timer_convert_ticks_to_ns(prevticks),
	      platform_timer_convert_ticks_to_ns(
		      current->arm_pv_time.yield_time),
	      platform_timer_convert_ticks_to_ns(last_self_unblock));

	assert((curticks >= adjusted_last_run) &&
	       (curticks >= last_self_unblock));

	current->arm_pv_time.yield_time = 0;
	current->arm_pv_time.stolen_ticks += curticks - steal_start;
	if (current->arm_pv_time.data != NULL) {
		uint64_t stolen_ns = platform_timer_convert_ticks_to_ns(
			current->arm_pv_time.stolen_ticks);
		atomic_store_relaxed(&current->arm_pv_time.data->stolen_ns,
				     stolen_ns);
	}
}

void
arm_pv_time_handle_scheduler_blocked(thread_t *thread, scheduler_block_t block)
{
	if (thread == thread_get_self()) {
		// Thread has blocked itself, presumably voluntarily. Reset the
		// last-unblock time.
		TRACE(DEBUG, INFO, "arm_pv_time: blocking self {:#x} ({:d})",
		      (uintptr_t)thread, (register_t)block);
		arm_pv_time_self_block_state_t new_state =
			arm_pv_time_self_block_state_default();
		arm_pv_time_self_block_state_set_block(&new_state, block);
		atomic_store_relaxed(&thread->arm_pv_time.self_block,
				     new_state);
	}
}

void
arm_pv_time_handle_scheduler_unblocked(thread_t		*thread,
				       scheduler_block_t block)
{
	arm_pv_time_self_block_state_t state =
		atomic_load_relaxed(&thread->arm_pv_time.self_block);
	ticks_t last_self_unblock =
		arm_pv_time_self_block_state_get_last_unblocked(&state);
	scheduler_block_t self_block =
		arm_pv_time_self_block_state_get_block(&state);
	if ((last_self_unblock == 0U) && (block == self_block)) {
		// Thread has been woken after blocking itself, or is becoming
		// runnable for the first time.
		TRACE(DEBUG, INFO, "arm_pv_time: unblocking {:#x}",
		      (uintptr_t)thread);
		arm_pv_time_self_block_state_t new_state =
			arm_pv_time_self_block_state_default();
		arm_pv_time_self_block_state_set_last_unblocked(
			&new_state, platform_timer_get_current_ticks());
		atomic_store_relaxed(&thread->arm_pv_time.self_block,
				     new_state);
	}
}
