// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcall_def.h>
#include <hypcontainers.h>
#include <hypregisters.h>

#include <abort.h>
#include <atomic.h>
#include <compiler.h>
#include <cpulocal.h>
#include <ipi.h>
#include <list.h>
#include <log.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <platform_cpu.h>
#include <power.h>
#include <preempt.h>
#include <qcbor.h>
#include <rcu.h>
#include <scheduler.h>
#include <sdei_dispatcher.h>
#include <spinlock.h>
#include <thread.h>
#include <timer_queue.h>
#include <trace.h>
#include <util.h>
#include <vcpu.h>
#include <vgic.h>
#include <virq.h>
#if defined(INTERFACE_WATCHDOG)
#include <watchdog.h>
#endif

#include <events/power.h>
#include <events/sdei.h>

#include "event_handlers.h"

static sdei_vendor_error_event_list_t sdei_vendor_error_event_list;
static sdei_grace_state_t	      sdei_grace;

static bool
sdei_event_valid(sdei_event_number_t ev_num)
{
	return !sdei_event_number_get_mbz(&ev_num) &&
	       (sdei_event_number_get_event(&ev_num) == 0U);
}

static bool
sdei_virq_valid(virq_t virq)
{
	return ((virq & ~VIRQ_SDEI_BIT) == SDEI_VENDOR_EVENT_ERROR);
}

static sdei_ev_type_t
sdei_get_ev_type(sdei_event_number_t ev_num)
{
	return sdei_event_number_get_vendor(&ev_num) ? SDEI_EV_TYPE_SHARED
						     : SDEI_EV_TYPE_PRIVATE;
}

static sdei_ev_signaled_t
sdei_get_ev_signaled(sdei_event_number_t ev_num)
{
	return sdei_event_number_get_vendor(&ev_num) ? SDEI_EV_SIGNALED_NO
						     : SDEI_EV_SIGNALED_YES;
}

static sdei_event_t *
sdei_get_standard_event0(thread_t *vcpu)
{
	return &vcpu->sdei->standard_event0;
}

static sdei_event_t *
sdei_get_vendor_error_event_from_vic(vic_t *vic)
{
	return &vic->sdei_vendor_error_event;
}

static sdei_event_t *
sdei_get_vendor_error_event(thread_t *vcpu)
{
	return sdei_get_vendor_error_event_from_vic(vcpu->vgic_vic);
}

static sdei_event_t *
sdei_get_event(sdei_event_number_t ev_num, thread_t *vcpu)
{
	return sdei_event_number_get_vendor(&ev_num)
		       ? sdei_get_vendor_error_event(vcpu)
		       : sdei_get_standard_event0(vcpu);
}

static bool
sdei_is_error_event(sdei_event_number_t ev_num)
{
	return sdei_event_number_get_vendor(&ev_num);
}

static index_result_t
sdei_get_index_from_mpidr(vic_t *vic, sdei_mpidr_t affinity)
{
	index_t idx;
	error_t err;

	MPIDR_EL1_t mpidr =
		MPIDR_EL1_cast(sdei_mpidr_raw(sdei_mpidr_clean(affinity)));
	const platform_mpidr_mapping_t *mpidr_mapping =
		vgic_get_mpidr_mapping(vic);

	if (platform_cpu_map_mpidr_valid(mpidr_mapping, mpidr)) {
		idx = platform_cpu_map_mpidr_to_index(mpidr_mapping, mpidr);
		err = OK;
	} else {
		idx = 0;
		err = ERROR_ARGUMENT_INVALID;
	}

	return (index_result_t){ .r = idx, .e = err };
}

static thread_t *
sdei_get_vcpu_from_index(vic_t *vic, index_t idx) REQUIRE_RCU_READ
{
	return atomic_load_consume(&vic->gicr_vcpus[idx]);
}

static thread_ptr_result_t
sdei_get_vcpu_from_mpidr(vic_t *vic, sdei_mpidr_t affinity) REQUIRE_RCU_READ
{
	thread_t *vcpu;
	error_t	  err;

	index_result_t idx = sdei_get_index_from_mpidr(vic, affinity);
	if (idx.e != OK) {
		vcpu = NULL;
		err  = idx.e;
	} else {
		vcpu = sdei_get_vcpu_from_index(vic, idx.r);
		err  = (vcpu != NULL) ? OK : ERROR_ARGUMENT_INVALID;
	}

	return (thread_ptr_result_t){ .r = vcpu, .e = err };
}

static sdei_mpidr_t
sdei_get_mpidr_from_index(vic_t *vic, index_t idx)
{
	const platform_mpidr_mapping_t *mpidr_mapping =
		vgic_get_mpidr_mapping(vic);
	uint64_t raw = MPIDR_EL1_raw(
		platform_cpu_map_index_to_mpidr(mpidr_mapping, idx));

	return sdei_mpidr_clean(sdei_mpidr_cast(raw));
}

static bool
sdei_try_deliver_event_to_vcpu(thread_t *vcpu, sdei_event_t *event)
	REQUIRE_SPINLOCK(event -> lock)
{
	// An event can be delivered to a vcpu if it is unmasked and does not
	// already have an active event.
	bool delivered;

	// The lock ensures that if we are racing with a mask call, either:
	// - we observe the vcpu to be masked, or
	// - the mask call handler observes the deliver bit as set if we end up
	//   delivering the event.
	// Otherwise, if we are racing with an unmask call, the unmask call
	// handler will recheck the vcpu events.
	spinlock_acquire_nopreempt(&vcpu->sdei->lock);
	if (atomic_load_relaxed(&vcpu->sdei->masked)) {
		delivered = false;
		spinlock_release_nopreempt(&vcpu->sdei->lock);
		goto out;
	}

	// The acquire syncs with the release when the event is completed or
	// discarded. This is necessary because the deliver bit is reset when
	// the event completes, and it must not happen after we set the deliver
	// bit below.
	sdei_event_t *curr_event = atomic_load_acquire(&vcpu->sdei->curr_event);
	if (curr_event != NULL) {
		delivered = false;
		spinlock_release_nopreempt(&vcpu->sdei->lock);
		goto out;
	}

	// Note: the lock protects against concurrent delivery.
	atomic_store_relaxed(&vcpu->sdei->curr_event, event);

	// At this point the event is committed for delivery to the target vcpu.
	// Update the event state and consume the pending flag. The event can
	// be retriggered at this point, although it won't be committed for
	// delivery again until it completes.
	event->state = SDEI_EVENT_STATE_HANDLER_ENABLED_AND_RUNNING;
	atomic_store_relaxed(&event->pending, false);

	// Set the vcpu pending bit to deliver the event on the next return to
	// userspace.
	sdei_vcpu_pending_bits_t bits = sdei_vcpu_pending_bits_default();
	sdei_vcpu_pending_bits_set_deliver(&bits, true);
	sdei_vcpu_pending_bits_atomic_union(&vcpu->sdei_pending, bits,
					    memory_order_relaxed);

	// Release the lock here so that any mask call is guaranteed to observe
	// the previous stores.
	spinlock_release_nopreempt(&vcpu->sdei->lock);

	// If the vcpu is suspended it needs to be woken up. Also, if the vcpu
	// is currently running then an IPI needs to be sent to invoke a return
	// to userspace event on that vcpu.
	if (vcpu == thread_get_self()) {
		vcpu_wakeup_self();
	} else {
		scheduler_lock_nopreempt(vcpu);
		ipi_one(IPI_REASON_SDEI_DISPATCHER_DELIVER_EVENT,
			scheduler_get_active_affinity(vcpu));
		vcpu_wakeup(vcpu);
		scheduler_unlock_nopreempt(vcpu);
	}

	delivered = true;
out:
	return delivered;
}

static void
sdei_try_deliver_event(sdei_event_t *event, thread_t *hint)
	REQUIRE_SPINLOCK(event -> lock)
{
	if (sdei_get_ev_type(event->ev_num) == SDEI_EV_TYPE_PRIVATE) {
		// If the event is able to be delivered, the hint should be set
		// since the caller of the function knows which vcpu was
		// signalled or which vcpu just completed their event.
		//
		// If the hint is NULL, then it means a private event was
		// discarded, in which case the vcpu should be masked and the
		// delivery would have failed anyway.
		if (hint != NULL) {
			(void)sdei_try_deliver_event_to_vcpu(hint, event);
		}
	} else if (event->routing_mode == SDEI_EV_ROUTING_MODE_RM_PE) {
		// For shared events with RM_PE use the target index (which was
		// mapped from an MPIDR) to get the correct vcpu. Note that it
		// may be NULL since the target may have deactivated.
		rcu_read_start();
		thread_t *target =
			sdei_get_vcpu_from_index(event->vic, event->target_idx);
		if (target != NULL) {
			(void)sdei_try_deliver_event_to_vcpu(target, event);
		}
		rcu_read_finish();
	} else {
		// For shared events with RM_ANY, use the hint if it is set,
		// otherwise loop through the vcpus using round-robin to find a
		// target.
		if (hint != NULL) {
			if (sdei_try_deliver_event_to_vcpu(hint, event)) {
				goto out;
			}
		}

		vic_t *vic = event->vic;

		count_t start_point = atomic_fetch_add_explicit(
			&vic->sdei_rr_start_point, 1U, memory_order_relaxed);
		start_point %= vic->gicr_count;

		rcu_read_start();
		for (index_t i = 0U; i < vic->gicr_count; i++) {
			thread_t *target = atomic_load_consume(
				&vic->gicr_vcpus[(i + start_point) %
						 vic->gicr_count]);
			if (target != NULL) {
				if (sdei_try_deliver_event_to_vcpu(target,
								   event)) {
					break;
				}
			}
		}
		rcu_read_finish();
	}
out:
	return;
}

static void
sdei_recheck_vcpu_event(sdei_event_t *event, thread_t *vcpu)
	EXCLUDE_SPINLOCK(event->lock)
{
	// If we are racing with an event trigger, the lock guarantees that:
	// - we observe the pending flag to be set, or
	// - they observe the vcpu to be unmasked.
	spinlock_acquire(&event->lock);
	if (atomic_load_relaxed(&event->pending) &&
	    (event->state == SDEI_EVENT_STATE_HANDLER_ENABLED)) {
		sdei_try_deliver_event(event, vcpu);
	}
	spinlock_release(&event->lock);
}

static void
sdei_recheck_vcpu_events(thread_t *vcpu)
{
	sdei_recheck_vcpu_event(sdei_get_standard_event0(vcpu), vcpu);
	sdei_recheck_vcpu_event(sdei_get_vendor_error_event(vcpu), vcpu);
}

static void
sdei_trigger_event(sdei_event_t *event, thread_t *hint)
	EXCLUDE_SPINLOCK(event->lock)
{
	// Set the pending flag to true. If it used to be false, then the event
	// is potentially deliverable.
	if (!atomic_exchange_relaxed(&event->pending, true)) {
		spinlock_acquire(&event->lock);
		if (event->state == SDEI_EVENT_STATE_HANDLER_ENABLED) {
			sdei_try_deliver_event(event, hint);
		}
		spinlock_release(&event->lock);
	}
}

void
sdei_trigger_error_event(vic_t *vic, sdei_error_flags_t flags)
{
	if (!atomic_exchange_relaxed(&vic->sdei_error_event_triggered, true)) {
		atomic_store_relaxed(&vic->sdei_error_flags, flags);
		sdei_event_t *event = sdei_get_vendor_error_event_from_vic(vic);
		sdei_trigger_event(event, NULL);
	}
}

void
sdei_trigger_error_event_all_non_pvm(sdei_error_flags_t flags)
{
	sdei_error_flags_set_system_error(&flags, true);

	spinlock_acquire(&sdei_vendor_error_event_list.lock);
	LIST_FOREACH_CONTAINER_BEGIN(vic_t,
				     &sdei_vendor_error_event_list.clients, vic,
				     sdei_list_node, client)
		if (!client->sdei_pvm) {
			sdei_trigger_error_event(client, flags);
		}
	LIST_FOREACH_CONTAINER_END
	spinlock_release(&sdei_vendor_error_event_list.lock);
}

void
sdei_trigger_error_event_pvm(sdei_error_flags_t flags)
{
	// Assume for now that this is a system error.
	sdei_error_flags_set_system_error(&flags, true);

	spinlock_acquire(&sdei_vendor_error_event_list.lock);
	vic_t *client = sdei_vendor_error_event_list.pvm_client;
	if (client != NULL) {
		sdei_trigger_error_event(client, flags);
	}
	spinlock_release(&sdei_vendor_error_event_list.lock);
}

bool
sdei_in_system_grace_period(void)
{
	return atomic_load_relaxed(&sdei_grace.active);
}

noreturn static void
sdei_on_system_grace_period_expiry(void)
{
	const char *msg = atomic_load_acquire(&sdei_grace.abort_msg);

	if (msg != NULL) {
		abort_kernel(msg, sdei_grace.abort_reason);
	} else {
		panic("sdei: system grace period finished (unknown reason)");
	}
}

static bool
sdei_start_system_grace_period_part_2(void)
{
	bool started;
	// This function might be called more than once, e.g. when the first
	// grace period is cancelled early, in which case subsequent calls
	// should be skipped.
	static atomic_bool skip = false;
	if (atomic_exchange_relaxed(&skip, true)) {
		started = false;
		goto out;
	}

	preempt_disable();

	// If the PVM is busy, the second grace period can be started.
	// Otherwise, it is skipped and we move on to the expiry function.
	error_t err = power_vote_cpu_on(cpulocal_get_index());
	if ((err == OK) &&
	    sdei_is_busy(sdei_get_busy_state(), SDEI_BUSY_SELECT_PVM)) {
		TRACE_AND_LOG(ERROR, WARN,
			      "sdei: starting PVM system grace period)");
		ticks_t timeout =
			timer_get_current_timer_ticks() +
			timer_convert_ns_to_ticks(
				SDEI_DEFAULT_SYSTEM_GRACE_PERIOD_2_NS);
		timer_enqueue(&sdei_grace.timer2, timeout);

		sdei_error_flags_t flags =
			atomic_load_relaxed(&sdei_grace.error_flags);
		sdei_trigger_error_event_pvm(flags);
		started = true;
	} else {
		TRACE_AND_LOG(
			ERROR, WARN,
			"sdei: PVM is not busy, skipping its grace period");
		sdei_on_system_grace_period_expiry();
	}

	preempt_enable();
out:
	return started;
}

static void
sdei_start_system_grace_period_part_1(sdei_error_flags_t flags)
{
	atomic_store_relaxed(&sdei_grace.error_flags, flags);

	// If there are any busy non-PVM VMs, then the first grace period is
	// started. Otherwise, it is skipped and we move on to the second grace
	// period.
	if (sdei_is_busy(sdei_get_busy_state(), SDEI_BUSY_SELECT_NON_PVM)) {
		TRACE_AND_LOG(ERROR, WARN,
			      "sdei: starting non-PVM system grace period)");
		ticks_t timeout =
			timer_get_current_timer_ticks() +
			timer_convert_ns_to_ticks(
				SDEI_DEFAULT_SYSTEM_GRACE_PERIOD_1_NS);
		timer_enqueue(&sdei_grace.timer1, timeout);

		sdei_trigger_error_event_all_non_pvm(flags);
	} else {
		TRACE_AND_LOG(
			ERROR, WARN,
			"sdei: all non-PVM VMs are not busy, skipping their grace period");
		sdei_start_system_grace_period_part_2();
	}
}

bool
sdei_start_system_grace_period(const char *msg, abort_reason_t reason,
			       sdei_error_flags_t flags)
{
	bool started;

	if (atomic_exchange_relaxed(&sdei_grace.active, true)) {
		started = false;
		goto out;
	}

	sdei_grace.abort_reason = reason;
	atomic_store_release(&sdei_grace.abort_msg, msg);

	preempt_disable();

	error_t err = power_vote_cpu_on(cpulocal_get_index());
	if (err == OK) {
		TRACE_AND_LOG(ERROR, WARN,
			      "sdei: starting system grace periods");
		sdei_start_system_grace_period_part_1(flags);
		started = true;
	} else {
		TRACE_AND_LOG(ERROR, WARN,
			      "sdei: could not start system grace periods");
		sdei_on_system_grace_period_expiry();
	}

	preempt_enable();
out:
	return started;
}

static void
sdei_check_system_grace_period_early_finish(sdei_busy_state_t busy_state)
{
	if (!sdei_is_busy(busy_state, SDEI_BUSY_SELECT_ANY)) {
		TRACE_AND_LOG(
			ERROR, WARN,
			"sdei: all VMs are not busy, ending all system grace periods");
		sdei_on_system_grace_period_expiry();
	}

	if (!sdei_is_busy(busy_state, SDEI_BUSY_SELECT_NON_PVM)) {
		TRACE_AND_LOG(
			ERROR, WARN,
			"sdei: all non-PVM VMs are not busy, ending their grace period");
		sdei_start_system_grace_period_part_2();
	}
}

static void
sdei_on_vm_grace_period_expiry(timer_t *timer)
{
	vic_t *vic = vic_container_of_sdei_vm_grace_timer(timer);

	if (!atomic_load_relaxed(&vic->sdei_vm_grace_active)) {
		goto out;
	}

#if defined(INTERFACE_WATCHDOG)
	watchdog_t *wdt =
		atomic_exchange_relaxed(&vic->sdei_vm_grace_wdt, NULL);
	if (wdt != NULL) {
// FIXME: QC Gunyah issue #XXX
// Add an interface/watchdog API
#if defined(MODULE_MISC_WATCHDOG)
		(void)virq_assert(&wdt->bite_virq_src, false);
		object_put_watchdog(wdt);
#endif
	}
#endif

	atomic_store_relaxed(&vic->sdei_vm_grace_active, false);
out:
	return;
}

static bool
sdei_cancel_vm_grace_period(vic_t *vic)
{
	bool cancelled =
		atomic_exchange_relaxed(&vic->sdei_vm_grace_active, false);

#if defined(INTERFACE_WATCHDOG)
	if (cancelled) {
		watchdog_t *wdt =
			atomic_exchange_relaxed(&vic->sdei_vm_grace_wdt, NULL);
		if (wdt != NULL) {
			object_put_watchdog(wdt);
		}
	}
#endif

	return cancelled;
}

bool
sdei_start_vm_grace_period(vic_t *vic, sdei_error_flags_t flags)
{
	bool started;

	if (atomic_exchange_relaxed(&vic->sdei_vm_grace_active, true)) {
		started = false;
		goto out;
	}

	preempt_disable();

	timer_t *timer = &vic->sdei_vm_grace_timer;
	error_t	 err   = power_vote_cpu_on(cpulocal_get_index());
	if (err == OK) {
		ticks_t timeout = timer_get_current_timer_ticks() +
				  timer_convert_ns_to_ticks(
					  SDEI_DEFAULT_VM_GRACE_PERIOD_NS);
		timer_enqueue(timer, timeout);
		TRACE_AND_LOG(ERROR, WARN,
			      "sdei: starting grace period for VM");
		started = true;
		sdei_trigger_error_event(vic, flags);
	} else {
		sdei_on_vm_grace_period_expiry(timer);
		TRACE_AND_LOG(ERROR, WARN,
			      "sdei: could not start grace period for VM");
		started = false;
	}

	preempt_enable();
out:
	// If a grace period has started, the caller of the function is
	// responsible for making sure there is an additional reference
	// obtained to the vic.
	return started;
}

sdei_busy_state_t
sdei_get_busy_state(void)
{
	spinlock_acquire(&sdei_grace.busy_lock);
	sdei_busy_state_t state = sdei_grace.busy_state;
	spinlock_release(&sdei_grace.busy_lock);

	return state;
}

bool
sdei_is_busy(sdei_busy_state_t state, sdei_busy_select_t which)
{
	bool ret;

	switch (which) {
	case SDEI_BUSY_SELECT_ANY:
		ret = (sdei_busy_state_get_non_pvm_count(&state) != 0U) ||
		      sdei_busy_state_get_pvm(&state);
		break;
	case SDEI_BUSY_SELECT_PVM:
		ret = sdei_busy_state_get_pvm(&state);
		break;
	case SDEI_BUSY_SELECT_NON_PVM:
		ret = sdei_busy_state_get_non_pvm_count(&state) != 0U;
		break;
	default:
		ret = true;
		break;
	}

	return ret;
}

static sdei_busy_state_t
sdei_vm_update_busy_state(vic_t *vic, bool set_to_busy)
	EXCLUDE_SPINLOCK(sdei_grace.busy_lock)
{
	spinlock_acquire(&sdei_grace.busy_lock);
	sdei_busy_state_t *state = &sdei_grace.busy_state;

	if (set_to_busy && !vic->sdei_busy) {
		vic->sdei_busy = true;
		if (vic->sdei_pvm) {
			sdei_busy_state_set_pvm(state, true);
		} else {
			sdei_busy_state_set_non_pvm_count(
				state,
				sdei_busy_state_get_non_pvm_count(state) + 1U);
		}
	} else if (!set_to_busy && vic->sdei_busy) {
		vic->sdei_busy = false;
		if (vic->sdei_pvm) {
			sdei_busy_state_set_pvm(state, false);
		} else {
			sdei_busy_state_set_non_pvm_count(
				state,
				sdei_busy_state_get_non_pvm_count(state) - 1U);
		}
	}

	sdei_busy_state_t ret = *state;
	spinlock_release(&sdei_grace.busy_lock);

	return ret;
}

#if defined(MODULE_MISC_WATCHDOG)
static bool
sdei_vm_is_busy(vic_t *vic) EXCLUDE_SPINLOCK(sdei_grace.busy_lock)
{
	spinlock_acquire(&sdei_grace.busy_lock);
	bool busy = vic->sdei_busy;
	spinlock_release(&sdei_grace.busy_lock);

	return busy;
}
#endif

static int64_t
sdei_shared_event_routing_set(sdei_event_t *event, bool routing_mode,
			      sdei_mpidr_t affinity)
	REQUIRE_SPINLOCK(event -> lock)
{
	int64_t ret;

	if (routing_mode) {
		index_result_t target_idx =
			sdei_get_index_from_mpidr(event->vic, affinity);

		if (target_idx.e == OK) {
			event->routing_mode = SDEI_EV_ROUTING_MODE_RM_PE;
			event->target_idx   = target_idx.r;
			ret		    = SDEI_RET_SUCCESS;
		} else {
			ret = SDEI_RET_INVALID_PARAMETERS;
		}
	} else {
		event->routing_mode = SDEI_EV_ROUTING_MODE_RM_ANY;
		ret		    = SDEI_RET_SUCCESS;
	}

	return ret;
}

static int64_t
sdei_enable_event(sdei_event_t *event) REQUIRE_SPINLOCK(event -> lock)
{
	int64_t ret;

	switch (event->state) {
	case SDEI_EVENT_STATE_HANDLER_REGISTERED:
		event->state = SDEI_EVENT_STATE_HANDLER_ENABLED;

		if (atomic_load_relaxed(&event->pending)) {
			sdei_try_deliver_event(event, thread_get_self());
		}

		ret = SDEI_RET_SUCCESS;
		break;
	case SDEI_EVENT_STATE_HANDLER_REGISTERED_AND_RUNNING:
		event->state = SDEI_EVENT_STATE_HANDLER_ENABLED_AND_RUNNING;
		ret	     = SDEI_RET_SUCCESS;
		break;
	case SDEI_EVENT_STATE_HANDLER_ENABLED:
	case SDEI_EVENT_STATE_HANDLER_ENABLED_AND_RUNNING:
		ret = SDEI_RET_SUCCESS;
		break;
	case SDEI_EVENT_STATE_HANDLER_UNREGISTERED:
	case SDEI_EVENT_STATE_HANDLER_UNREGISTER_PENDING:
	default: // Should be unreachable
		ret = SDEI_RET_DENIED;
		break;
	}

	return ret;
}

static int64_t
sdei_disable_event(sdei_event_t *event) REQUIRE_SPINLOCK(event -> lock)
{
	int64_t ret;

	switch (event->state) {
	case SDEI_EVENT_STATE_HANDLER_REGISTERED:
	case SDEI_EVENT_STATE_HANDLER_REGISTERED_AND_RUNNING:
		ret = SDEI_RET_SUCCESS;
		break;
	case SDEI_EVENT_STATE_HANDLER_ENABLED:
		event->state = SDEI_EVENT_STATE_HANDLER_REGISTERED;
		ret	     = SDEI_RET_SUCCESS;
		break;
	case SDEI_EVENT_STATE_HANDLER_ENABLED_AND_RUNNING:
		event->state = SDEI_EVENT_STATE_HANDLER_REGISTERED_AND_RUNNING;
		ret	     = SDEI_RET_SUCCESS;
		break;
	case SDEI_EVENT_STATE_HANDLER_UNREGISTERED:
	case SDEI_EVENT_STATE_HANDLER_UNREGISTER_PENDING:
	default: // Should be unreachable
		ret = SDEI_RET_DENIED;
		break;
	}

	return ret;
}

static void
sdei_on_event_transition_to_unregistered(sdei_event_t *event)
	REQUIRE_SPINLOCK(event -> lock)
{
	if (sdei_is_error_event(event->ev_num)) {
		(void)sdei_vm_update_busy_state(event->vic, false);
	}
}

static int64_t
sdei_unregister_event(sdei_event_t *event) REQUIRE_SPINLOCK(event -> lock)
{
	int64_t ret;

	switch (event->state) {
	case SDEI_EVENT_STATE_HANDLER_REGISTERED:
	case SDEI_EVENT_STATE_HANDLER_ENABLED:
		event->state = SDEI_EVENT_STATE_HANDLER_UNREGISTERED;
		ret	     = SDEI_RET_SUCCESS;
		sdei_on_event_transition_to_unregistered(event);
		break;
	case SDEI_EVENT_STATE_HANDLER_ENABLED_AND_RUNNING:
	case SDEI_EVENT_STATE_HANDLER_REGISTERED_AND_RUNNING:
		event->state = SDEI_EVENT_STATE_HANDLER_UNREGISTER_PENDING;
		ret	     = SDEI_RET_PENDING;
		break;
	case SDEI_EVENT_STATE_HANDLER_UNREGISTERED:
	case SDEI_EVENT_STATE_HANDLER_UNREGISTER_PENDING:
	default: // Should be unreachable
		ret = SDEI_RET_DENIED;
		break;
	}

	return ret;
}

static void
sdei_complete_event_update_state(sdei_event_t *event, thread_t *hint,
				 bool check_redeliver)
	REQUIRE_SPINLOCK(event -> lock)
{
	switch (event->state) {
	case SDEI_EVENT_STATE_HANDLER_UNREGISTER_PENDING:
		event->state = SDEI_EVENT_STATE_HANDLER_UNREGISTERED;
		sdei_on_event_transition_to_unregistered(event);
		break;
	case SDEI_EVENT_STATE_HANDLER_REGISTERED_AND_RUNNING:
		event->state = SDEI_EVENT_STATE_HANDLER_REGISTERED;
		break;
	case SDEI_EVENT_STATE_HANDLER_ENABLED_AND_RUNNING:
		event->state = SDEI_EVENT_STATE_HANDLER_ENABLED;
		if (check_redeliver && atomic_load_relaxed(&event->pending)) {
			sdei_try_deliver_event(event, hint);
		}
		break;
	case SDEI_EVENT_STATE_HANDLER_UNREGISTERED:
	case SDEI_EVENT_STATE_HANDLER_REGISTERED:
	case SDEI_EVENT_STATE_HANDLER_ENABLED:
	default:
		// Should be unreachable
		break;
	}
}

static void
sdei_complete_event(thread_t *vcpu, bool resume)
{
	assert(vcpu == thread_get_self());
	// This function should only be called if vcpu is current.

	// It doesn't matter when this gets set to false, since it is only
	// checked or modified by the vcpu when it is current.
	vcpu->sdei->active = false;

	// Defer the actual switching of the context until return to userspace.
	// The deliver bit can be overwritten since it should not be set. A
	// relaxed store is okay since vcpu is always the current thread.
	sdei_vcpu_pending_bits_t bits = sdei_vcpu_pending_bits_default();
	if (resume) {
		sdei_vcpu_pending_bits_set_resume(&bits, true);
	} else {
		sdei_vcpu_pending_bits_set_complete(&bits, true);
	}
	atomic_store_relaxed(&vcpu->sdei_pending, bits);

	// Mark the vcpu as ready to receive events with a release operation.
	sdei_event_t *event = atomic_exchange_explicit(
		&vcpu->sdei->curr_event, NULL, memory_order_release);

	// Update the event state. If the event triggered while it was running
	// and is pending, it may be delivered again.
	spinlock_acquire(&event->lock);
	sdei_complete_event_update_state(event, vcpu, true);
	spinlock_release(&event->lock);

	// The event just completed; check for more events to run.
	sdei_recheck_vcpu_events(vcpu);
}

static void
sdei_discard_event(thread_t *vcpu)
{
	assert(vcpu == thread_get_self());
	// This function is similar to sdei_complete_event, but is intended to
	// be called from threads that are stopped. The vcpu should have events
	// masked at this point.
	vcpu->sdei->active = false;

	atomic_store_relaxed(&vcpu->sdei_pending,
			     sdei_vcpu_pending_bits_default());

	sdei_event_t *event = atomic_exchange_explicit(
		&vcpu->sdei->curr_event, NULL, memory_order_release);

	spinlock_acquire(&event->lock);
	sdei_complete_event_update_state(event, NULL, true);
	spinlock_release(&event->lock);
}

static void
sdei_maybe_redeliver_event(thread_t *vcpu)
{
	assert(vcpu == thread_get_self());
	// This function is conceptually similar to sdei_discard_event, except
	// the injection of the event hasn't happened yet. This function should
	// only be called from the mask handler, and the vcpu should have
	// events masked at this point.

	atomic_store_relaxed(&vcpu->sdei_pending,
			     sdei_vcpu_pending_bits_default());

	sdei_event_t *event = atomic_exchange_explicit(
		&vcpu->sdei->curr_event, NULL, memory_order_release);

	// Retrigger the event and pretend that the event is completed. If the
	// event is private or shared with RM_PE then we can't redeliver the
	// event.
	atomic_store_relaxed(&event->pending, true);

	spinlock_acquire(&event->lock);
	bool redeliver =
		(sdei_get_ev_type(event->ev_num) == SDEI_EV_TYPE_SHARED) &&
		(event->routing_mode == SDEI_EV_ROUTING_MODE_RM_ANY);
	sdei_complete_event_update_state(event, NULL, redeliver);
	spinlock_release(&event->lock);
}

static bool
sdei_mask_vcpu(thread_t *vcpu)
{
	spinlock_acquire(&vcpu->sdei->lock);
	bool old = atomic_exchange_relaxed(&vcpu->sdei->masked, true);
	spinlock_release(&vcpu->sdei->lock);

	// At this point the vcpu is masked, so no other threads will be able
	// to modify our sdei state.

	sdei_vcpu_pending_bits_t bits =
		atomic_load_relaxed(&vcpu->sdei_pending);
	if (sdei_vcpu_pending_bits_get_deliver(&bits)) {
		sdei_maybe_redeliver_event(vcpu);
	}

	return old;
}

static void
sdei_unmask_vcpu(thread_t *vcpu)
{
	if (atomic_exchange_relaxed(&vcpu->sdei->masked, false)) {
		sdei_recheck_vcpu_events(vcpu);
	}
}

void
sdei_dispatcher_handle_boot_cold_init(void)
{
	list_init(&sdei_vendor_error_event_list.clients);
	spinlock_init(&sdei_vendor_error_event_list.lock);
	timer_init_object(&sdei_grace.timer1,
			  TIMER_ACTION_SDEI_SYSTEM_GRACE_PERIOD_1);
	timer_init_object(&sdei_grace.timer2,
			  TIMER_ACTION_SDEI_SYSTEM_GRACE_PERIOD_2);
	spinlock_init(&sdei_grace.busy_lock);
}

void
sdei_dispatcher_handle_rootvm_init(qcbor_enc_ctxt_t *qcbor_enc_ctxt)
{
	QCBOREncode_AddBoolToMap(qcbor_enc_ctxt, "sdei_supported", true);
}

error_t
sdei_dispatcher_handle_object_create_vic(vic_create_t vic_create)
{
	vic_t *vic = vic_create.vic;
	atomic_init(&vic->sdei_error_event_triggered, false);
	atomic_init(&vic->sdei_error_flags, sdei_error_flags_default());
	atomic_init(&vic->sdei_init_done, false);
	atomic_init(&vic->sdei_rr_start_point, 0U);
	atomic_init(&vic->sdei_vm_grace_active, false);
	timer_init_object(&vic->sdei_vm_grace_timer,
			  TIMER_ACTION_SDEI_VM_GRACE_PERIOD);
	atomic_init(&vic->sdei_vm_grace_wdt, NULL);

	return OK;
}

static bool
sdei_create_vcpu_state(thread_t *vcpu)
{
	void_ptr_result_t result = partition_alloc(vcpu->header.partition,
						   sizeof(sdei_vcpu_state_t),
						   alignof(sdei_vcpu_state_t));
	if (result.e == OK) {
		vcpu->sdei = result.r;
		memset_s(vcpu->sdei, sizeof(*vcpu->sdei), 0,
			 sizeof(sdei_vcpu_state_t));
		atomic_init(&vcpu->sdei->masked, true);
		atomic_init(&vcpu->sdei_pending,
			    sdei_vcpu_pending_bits_default());
		atomic_init(&vcpu->sdei->curr_event, NULL);
		spinlock_init(&vcpu->sdei->lock);
	}

	return result.e == OK;
}

static void
sdei_destroy_vcpu_state(thread_t *vcpu)
{
	partition_free(vcpu->header.partition, vcpu->sdei,
		       sizeof(sdei_vcpu_state_t));
	vcpu->sdei = NULL;
}

static void
sdei_init_event(sdei_event_t *event, int32_t ev_num_raw, vic_t *vic)
{
	atomic_init(&event->pending, false);
	event->state	    = SDEI_EVENT_STATE_HANDLER_UNREGISTERED;
	event->target_idx   = 0U;
	event->entry_addr   = 0U;
	event->ep_argument  = 0U;
	event->routing_mode = SDEI_EV_ROUTING_MODE_RM_PE;
	event->ev_num	    = sdei_event_number_cast((uint32_t)ev_num_raw);
	event->vic	    = vic;
	spinlock_init(&event->lock);
}

static void
sdei_vcpu_init_private_events(thread_t *vcpu)
{
	sdei_init_event(sdei_get_standard_event0(vcpu),
			SDEI_STANDARD_EVENT_SOFTWARE_SIGNALED, vcpu->vgic_vic);
}

static void
sdei_vcpu_init_shared_events(vic_t *vic)
{
	sdei_event_t *vendor_error_event =
		sdei_get_vendor_error_event_from_vic(vic);

	sdei_init_event(vendor_error_event, SDEI_VENDOR_EVENT_ERROR, vic);

	spinlock_acquire(&sdei_vendor_error_event_list.lock);
	list_insert_at_tail(&sdei_vendor_error_event_list.clients,
			    &vic->sdei_list_node);
	if (vic->sdei_pvm &&
	    (sdei_vendor_error_event_list.pvm_client == NULL)) {
		sdei_vendor_error_event_list.pvm_client = vic;
	}
	spinlock_release(&sdei_vendor_error_event_list.lock);
}

static void
sdei_vcpu_cleanup_shared_events(vic_t *vic)
{
	spinlock_acquire(&sdei_vendor_error_event_list.lock);
	(void)list_delete_node(&sdei_vendor_error_event_list.clients,
			       &vic->sdei_list_node);
	if (vic->sdei_pvm) {
		sdei_vendor_error_event_list.pvm_client = NULL;
	}
	spinlock_release(&sdei_vendor_error_event_list.lock);
}

bool
sdei_dispatcher_handle_vcpu_activate_thread(thread_t	       *thread,
					    vcpu_option_flags_t options)
{
	bool success;

	if (!vcpu_option_flags_get_sdei_allowed(&options)) {
		success = true;
		goto out;
	}

	// For now it is not an error if the thread doesn't have a vic, but the
	// thread won't be allowed to use SDEI.
	if (thread->vgic_vic == NULL) {
		success = true;
		goto out;
	}

	if (!sdei_create_vcpu_state(thread)) {
		success = false;
		goto out;
	}

	sdei_vcpu_init_private_events(thread);

	if (!atomic_exchange_relaxed(&thread->vgic_vic->sdei_init_done, true)) {
		if (vcpu_option_flags_get_hlos_vm(&options)) {
			thread->vgic_vic->sdei_pvm = true;
		}
		sdei_vcpu_init_shared_events(thread->vgic_vic);
	}

	vcpu_option_flags_set_sdei_allowed(&thread->vcpu_options, true);

#if defined(MODULE_VM_WATCHDOG)
	if (thread->watchdog != NULL) {
		vic_t *expected = NULL;

		if (atomic_compare_exchange_strong_explicit(
			    &thread->watchdog->sdei_vm, &expected,
			    thread->vgic_vic, memory_order_relaxed,
			    memory_order_relaxed)) {
			(void)object_get_vic_additional(thread->vgic_vic);
		}
	}
#endif

	success = true;
out:
	return success;
}

void
sdei_dispatcher_handle_object_deactivate_thread(thread_t *thread)
{
	if (thread->sdei != NULL) {
		sdei_destroy_vcpu_state(thread);
	}
}

void
sdei_dispatcher_handle_object_deactivate_vic(vic_t *vic)
{
	if (atomic_load_relaxed(&vic->sdei_init_done)) {
		sdei_vcpu_cleanup_shared_events(vic);
		(void)sdei_vm_update_busy_state(vic, false);
	}
}

error_t
sdei_dispatcher_handle_object_create_watchdog(watchdog_create_t wdt_create)
{
	watchdog_t *wdt = wdt_create.watchdog;
	atomic_init(&wdt->sdei_vm, NULL);

	return OK;
}

void
sdei_dispatcher_handle_object_deactivate_watchdog(watchdog_t *watchdog)
{
	vic_t *vic = atomic_exchange_relaxed(&watchdog->sdei_vm, NULL);

	if (vic != NULL) {
		object_put_vic(vic);
	}
}

static SPSR_EL2_A64_t
sdei_exit_to_user_deliver_event_prepare_spsr(SPSR_EL2_A64_t spsr)
{
	// Set DAIF = 0b1111, EL = EL1, nRW = 0, SP = 1.
	SPSR_EL2_A64_set_D(&spsr, true);
	SPSR_EL2_A64_set_A(&spsr, true);
	SPSR_EL2_A64_set_I(&spsr, true);
	SPSR_EL2_A64_set_F(&spsr, true);
	SPSR_EL2_A64_set_M(&spsr, SPSR_64BIT_MODE_EL1H);

	// Set the other PSTATE bits as if an exception was taken to EL1.
#if defined(ARCH_ARM_FEAT_GCS)
#error SDEI: FEAT_GCS: PSTATE.EXLOCK not handled.
#endif
	SPSR_EL2_A64_set_SS(&spsr, false);
#if defined(ARCH_ARM_FEAT_NMI)
#error SDEI: FEAT_NMI: PSTATE.ALLINT not handled.
#endif
	SPSR_EL2_A64_set_IL(&spsr, false);
#if defined(ARCH_ARM_FEAT_SSBS) || defined(ARCH_ARM_FEAT_PAN)
	SCTLR_EL1_t sctlr_el1 = register_SCTLR_EL1_read();
#endif
#if defined(ARCH_ARM_FEAT_PAN)
	if (!SCTLR_EL1_get_SPAN(&sctlr_el1)) {
		SPSR_EL2_A64_set_PAN(&spsr, false);
	}
#endif
#if defined(ARCH_ARM_FEAT_UAO)
	SPSR_EL2_A64_set_UAO(&spsr, false);
#endif
#if defined(ARCH_ARM_FEAT_BTI)
	SPSR_EL2_A64_set_BTYPE(&spsr, 0U);
#endif
#if defined(ARCH_ARM_FEAT_SSBS)
	SPSR_EL2_A64_set_SSBS(&spsr, SCTLR_EL1_get_DSSBS(&sctlr_el1));
#endif
#if defined(ARCH_ARM_FEAT_MTE)
	SPSR_EL2_A64_set_TCO(&spsr, true);
#endif
#if defined(ARCH_ARM_FEAT_EBEP)
#error SDEI: FEAT_EBEP: PSTATE.PM not handled.
#endif
#if defined(ARCH_ARM_FEAT_SEBEP)
#error SDEI: FEAT_SEBEP: PSTATE.PPEND not handled.
#endif
	return spsr;
}

static void
sdei_exit_to_user_deliver_event(thread_t *vcpu, sdei_event_t *event)
{
	uint64_t       pc = ELR_EL2_get_ReturnAddress(&vcpu->vcpu_regs_gpr.pc);
	SPSR_EL2_A64_t spsr = vcpu->vcpu_regs_gpr.spsr_el2.a64;

	// Save x0-17 since it won't be preserved by the vcpu event handler.
	// The interrupted pc and spsr is also saved because we need it when
	// the event is completed.
	memscpy(vcpu->sdei->ctx.x0_17, sizeof(vcpu->sdei->ctx.x0_17),
		vcpu->vcpu_regs_gpr.x, 18U * sizeof(register_t));
	vcpu->sdei->ctx.pc     = pc;
	vcpu->sdei->ctx.pstate = spsr;

	// Jump to the SDEI event handler by modifying the normal vcpu context.
	vcpu->vcpu_regs_gpr.x[0] = sdei_event_number_raw(event->ev_num);
	vcpu->vcpu_regs_gpr.x[1] = event->ep_argument;
	vcpu->vcpu_regs_gpr.x[2] = pc;
	vcpu->vcpu_regs_gpr.x[3] = SPSR_EL2_A64_raw(spsr);
	ELR_EL2_set_ReturnAddress(&vcpu->vcpu_regs_gpr.pc, event->entry_addr);
	vcpu->vcpu_regs_gpr.spsr_el2.a64 =
		sdei_exit_to_user_deliver_event_prepare_spsr(spsr);

	// Set the active flag to indicate that the event has actually been
	// injected. This flag is only read or modified if vcpu is current.
	vcpu->sdei->active = true;
}

static void
sdei_exit_to_user_complete_event(thread_t *vcpu, bool resume)
{
	uint64_t       pc   = vcpu->sdei->ctx.pc;
	SPSR_EL2_A64_t spsr = vcpu->sdei->ctx.pstate;

	memscpy(vcpu->vcpu_regs_gpr.x, sizeof(vcpu->vcpu_regs_gpr.x),
		vcpu->sdei->ctx.x0_17, sizeof(vcpu->sdei->ctx.x0_17));

	if (!resume) {
		ELR_EL2_set_ReturnAddress(&vcpu->vcpu_regs_gpr.pc, pc);
		vcpu->vcpu_regs_gpr.spsr_el2.a64 = spsr;
	} else {
		ELR_EL1_set_ReturnAddress(&vcpu->vcpu_regs_el1.elr_el1, pc);
		register_ELR_EL1_write(vcpu->vcpu_regs_el1.elr_el1);

		uint64_t spsr_raw = SPSR_EL2_A64_raw(spsr);
		vcpu->vcpu_regs_el1.spsr_el1 =
			SPSR_EL1_A64_clean(SPSR_EL1_A64_cast(spsr_raw));
		register_SPSR_EL1_A64_write(vcpu->vcpu_regs_el1.spsr_el1);

		ELR_EL2_set_ReturnAddress(&vcpu->vcpu_regs_gpr.pc,
					  vcpu->sdei->resume_addr);

		// Set DAIF = 0b1111, EL = EL1, nRW = 0, SP = 1. The other bits
		// are left unmodified.
		SPSR_EL2_A64_set_D(&spsr, true);
		SPSR_EL2_A64_set_A(&spsr, true);
		SPSR_EL2_A64_set_I(&spsr, true);
		SPSR_EL2_A64_set_F(&spsr, true);
		SPSR_EL2_A64_set_M(&spsr, SPSR_64BIT_MODE_EL1H);
		vcpu->vcpu_regs_gpr.spsr_el2.a64 = spsr;
	}
}

static void
sdei_exit_to_user_check_pending(thread_t *vcpu, sdei_vcpu_pending_bits_t bits)
	REQUIRE_PREEMPT_DISABLED
{
	sdei_vcpu_pending_bits_t clear_bits = sdei_vcpu_pending_bits_default();

	// It is possible for both the deliver and complete / resume bits to be
	// set. This means that an event was committed for delivery after the
	// event was completed, because event completion is denied if an event
	// has been committed for delivery but hasn't actually been injected
	// yet.
	//
	// Therefore, the correct behaviour is to complete the event first, and
	// then deliver the new event.
	//
	// If we are completing and resuming, the resume address should have
	// been set.
	if (sdei_vcpu_pending_bits_get_complete(&bits)) {
		sdei_exit_to_user_complete_event(vcpu, false);
	} else if (sdei_vcpu_pending_bits_get_resume(&bits)) {
		sdei_exit_to_user_complete_event(vcpu, true);
	} else {
		// The deliver bit is checked below.
	}

	if (sdei_vcpu_pending_bits_get_deliver(&bits)) {
		sdei_event_t *event =
			atomic_load_relaxed(&vcpu->sdei->curr_event);

		if (event != NULL) {
			// Do not recheck any event delivery conditions here;
			// just let the client race.
			//
			// Note: if the vcpu has events masked it should have
			// been cancelled by the mask call handler.
			sdei_exit_to_user_deliver_event(vcpu, event);
		} else {
			// It is possible for event to be NULL if we haven't yet
			// synchronised with the IPI. In this case we should not
			// clear the deliver bit.
			sdei_vcpu_pending_bits_set_deliver(&clear_bits, true);
		}
	}

	atomic_store_relaxed(&vcpu->sdei_pending, clear_bits);
}

void
sdei_dispatcher_handle_thread_exit_to_user_final(thread_t	      *current,
						 thread_entry_reason_t reason)
{
	sdei_vcpu_pending_bits_t bits =
		atomic_load_relaxed(&current->sdei_pending);
	if (compiler_unexpected(!sdei_vcpu_pending_bits_is_empty(bits))) {
		switch (reason) {
		case THREAD_ENTRY_REASON_NONE:
		case THREAD_ENTRY_REASON_HYPERCALL:
			ipi_one(IPI_REASON_SDEI_DISPATCHER_DELIVER_EVENT,
				cpulocal_get_index());
			break;
		case THREAD_ENTRY_REASON_EXCEPTION:
		case THREAD_ENTRY_REASON_INTERRUPT:
			sdei_exit_to_user_check_pending(current, bits);
			break;
		default:
			break;
		}
	}
}

bool
sdei_dispatcher_handle_ipi_received(void)
{
	return false;
}

static bool
sdei_vcpu_check_pending_delivery(thread_t *vcpu)
{
	sdei_vcpu_pending_bits_t bits =
		atomic_load_acquire(&vcpu->sdei_pending);
	return sdei_vcpu_pending_bits_get_deliver(&bits);
}

bool
sdei_dispatcher_handle_vcpu_pending_wakeup(void)
{
	return sdei_vcpu_check_pending_delivery(thread_get_self());
}

error_t
sdei_dispatcher_handle_thread_context_switch_pre(void)
{
	thread_t *current = thread_get_self();

	if (sdei_vcpu_check_pending_delivery(current)) {
		scheduler_lock_nopreempt(current);
		vcpu_wakeup(current);
		scheduler_unlock_nopreempt(current);
	}

	return OK;
}

vcpu_trap_result_t
sdei_dispatcher_handle_vcpu_trap_wfi(void)
{
	return sdei_vcpu_check_pending_delivery(thread_get_self())
		       ? VCPU_TRAP_RESULT_RETRY
		       : VCPU_TRAP_RESULT_UNHANDLED;
}

void
sdei_dispatcher_handle_vcpu_stopped(void)
{
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei != NULL) {
		// Mask events for the vcpu first to stop delivery of new
		// events. This also means that events will be masked on
		// power-on as intended (if this is from a poweroff call).
		(void)sdei_mask_vcpu(vcpu);

		// It is fine to leave any shared events targeting this vcpu
		// because the index is used to identify vcpus.
		//
		// If there is an event active on this vcpu it must be
		// discarded, so that it may be delivered to other vcpus in the
		// future.
		//
		// Note that the above mask call will handle the case where the
		// the deliver bit is set (and active isn't set).
		if (vcpu->sdei->active) {
			sdei_discard_event(vcpu);
		}
	}
}

void
sdei_dispatcher_handle_vcpu_warm_reset(thread_t *vcpu)
{
	if (vcpu->sdei != NULL) {
		// Events must be masked on warm reset. If we were woken up by
		// an event then it will run once the vcpu unmasks itself.
		(void)sdei_mask_vcpu(vcpu);
	}
}

bool
sdei_dispatcher_handle_timer_action_system_grace_period_1(void)
{
	sdei_start_system_grace_period_part_2();
	return true;
}

noreturn void
sdei_dispatcher_handle_timer_action_system_grace_period_2(void)
{
	sdei_on_system_grace_period_expiry();
}

bool
sdei_dispatcher_handle_timer_action_vm_grace_period(timer_t *timer)
{
	power_vote_cpu_off(cpulocal_get_index()); // FIXME: Is this safe?
	sdei_on_vm_grace_period_expiry(timer);

	return true;
}

bool
sdei_dispatcher_handle_sdei_version(uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	sdei_version_t ver = sdei_version_default();
	sdei_version_set_major(&ver, SDEI_DISPATCHER_VERSION_MAJOR);
	sdei_version_set_minor(&ver, SDEI_DISPATCHER_VERSION_MINOR);
	sdei_version_set_vendor(&ver, SDEI_DISPATCHER_VERSION_VENDOR);
	ret = (int64_t)sdei_version_raw(ver);
out:
	*ret0 = (uint64_t)ret;
	return true;
}

bool
sdei_dispatcher_handle_sdei_event_register(uint64_t arg1, uint64_t arg2,
					   uint64_t arg3, uint64_t arg4,
					   uint64_t arg5, uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	gvaddr_t entry_addr = (gvaddr_t)arg2;
	if (!(util_is_baligned(entry_addr, 4U))) {
		ret = SDEI_RET_INVALID_PARAMETERS;
		goto out;
	}

	sdei_event_register_flags_t flags =
		sdei_event_register_flags_cast(arg4);
	if (sdei_event_register_flags_get_relative_mode(&flags)) {
		// FIXME: Relative mode is not supported for now.
		ret = SDEI_RET_INVALID_PARAMETERS;
		goto out;
	}

	sdei_event_number_t ev_num = sdei_event_number_cast((uint32_t)arg1);
	if (!sdei_event_valid(ev_num)) {
		ret = SDEI_RET_INVALID_PARAMETERS;
		goto out;
	}

	sdei_event_t *event = sdei_get_event(ev_num, vcpu);
	spinlock_acquire(&event->lock);

	if (event->state != SDEI_EVENT_STATE_HANDLER_UNREGISTERED) {
		ret = SDEI_RET_DENIED;
		goto out_unlock;
	}

	if (sdei_get_ev_type(ev_num) == SDEI_EV_TYPE_SHARED) {
		bool rm = sdei_event_register_flags_get_routing_mode(&flags);
		sdei_mpidr_t affinity = sdei_mpidr_cast(arg5);

		ret = sdei_shared_event_routing_set(event, rm, affinity);
		if (ret != SDEI_RET_SUCCESS) {
			goto out_unlock;
		}
	}

	if (sdei_is_error_event(ev_num)) {
		(void)sdei_vm_update_busy_state(vcpu->vgic_vic, true);
	}

	event->state	   = SDEI_EVENT_STATE_HANDLER_REGISTERED;
	event->entry_addr  = entry_addr;
	event->ep_argument = arg3;
	ret		   = SDEI_RET_SUCCESS;
out_unlock:
	spinlock_release(&event->lock);
out:
	*ret0 = (uint64_t)ret;
	return true;
}

bool
sdei_dispatcher_handle_sdei_event_enable(uint64_t arg1, uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	sdei_event_number_t ev_num = sdei_event_number_cast((uint32_t)arg1);
	if (!sdei_event_valid(ev_num)) {
		ret = SDEI_RET_INVALID_PARAMETERS;
		goto out;
	}

	sdei_event_t *event = sdei_get_event(ev_num, vcpu);
	spinlock_acquire(&event->lock);
	ret = sdei_enable_event(event);
	spinlock_release(&event->lock);
out:
	*ret0 = (uint64_t)ret;
	return true;
}

bool
sdei_dispatcher_handle_sdei_event_disable(uint64_t arg1, uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	sdei_event_number_t ev_num = sdei_event_number_cast((uint32_t)arg1);
	if (!sdei_event_valid(ev_num)) {
		ret = SDEI_RET_INVALID_PARAMETERS;
		goto out;
	}

	sdei_event_t *event = sdei_get_event(ev_num, vcpu);
	spinlock_acquire(&event->lock);
	ret = sdei_disable_event(event);
	spinlock_release(&event->lock);
out:
	*ret0 = (uint64_t)ret;
	return true;
}

bool
sdei_dispatcher_handle_sdei_event_context(uint64_t arg1, uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	if (!vcpu->sdei->active) {
		ret = SDEI_RET_DENIED;
		goto out;
	}

	uint32_t param_id = (uint32_t)arg1;
	if (param_id <= 17U) {
		ret = (int64_t)vcpu->sdei->ctx.x0_17[param_id];
	} else {
		ret = SDEI_RET_INVALID_PARAMETERS;
	}
out:
	*ret0 = (uint64_t)ret;
	return true;
}

bool
sdei_dispatcher_handle_sdei_event_complete(uint64_t arg1, uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	if (!vcpu->sdei->active) {
		ret = SDEI_RET_DENIED;
		goto out;
	}

	// FIXME: The status code is ignored for now.
	(void)arg1;

	sdei_complete_event(vcpu, false);
	ret = SDEI_RET_SUCCESS; // Doesn't actually return on success.
out:
	*ret0 = (uint64_t)ret;
	return true;
}

bool
sdei_dispatcher_handle_sdei_event_complete_and_resume(uint64_t	arg1,
						      uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	if (!vcpu->sdei->active) {
		ret = SDEI_RET_DENIED;
		goto out;
	}

	gvaddr_t resume_addr = (gvaddr_t)arg1;
	if (!util_is_baligned(resume_addr, 4U)) {
		ret = SDEI_RET_INVALID_PARAMETERS;
		goto out;
	}
	vcpu->sdei->resume_addr = resume_addr;

	sdei_complete_event(vcpu, true);
	ret = SDEI_RET_SUCCESS; // Doesn't actually return on success.
out:
	*ret0 = (uint64_t)ret;
	return true;
}

bool
sdei_dispatcher_handle_sdei_event_unregister(uint64_t arg1, uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	sdei_event_number_t ev_num = sdei_event_number_cast((uint32_t)arg1);
	if (!sdei_event_valid(ev_num)) {
		ret = SDEI_RET_INVALID_PARAMETERS;
		goto out;
	}

	sdei_event_t *event = sdei_get_event(ev_num, vcpu);
	spinlock_acquire(&event->lock);
	ret = sdei_unregister_event(event);
	spinlock_release(&event->lock);
out:
	*ret0 = (uint64_t)ret;
	return true;
}

bool
sdei_dispatcher_handle_sdei_event_status(uint64_t arg1, uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	sdei_event_number_t ev_num = sdei_event_number_cast((uint32_t)arg1);
	if (!sdei_event_valid(ev_num)) {
		ret = SDEI_RET_INVALID_PARAMETERS;
		goto out;
	}

	sdei_event_t *event = sdei_get_event(ev_num, vcpu);
	spinlock_acquire(&event->lock);
	ret = event->state;
	spinlock_release(&event->lock);
out:
	*ret0 = (uint64_t)ret;
	return true;
}

bool
sdei_dispatcher_handle_sdei_event_get_info(uint64_t arg1, uint64_t arg2,
					   uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	sdei_event_number_t ev_num = sdei_event_number_cast((uint32_t)arg1);
	if (!sdei_event_valid(ev_num)) {
		ret = SDEI_RET_INVALID_PARAMETERS;
		goto out;
	}

	uint32_t info = (uint32_t)arg2;
	// The validity of info is checked implicitly below.
	ret = trigger_sdei_dispatcher_event_get_info_event(info, ev_num);
out:
	*ret0 = (uint64_t)ret;
	return true;
}

bool
sdei_dispatcher_handle_sdei_event_routing_set(uint64_t arg1, uint64_t arg2,
					      uint64_t arg3, uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	sdei_event_routing_set_routing_mode_t routing_mode =
		sdei_event_routing_set_routing_mode_cast(arg2);
	if (sdei_event_routing_set_routing_mode_get_res0(&routing_mode) != 0U) {
		ret = SDEI_RET_INVALID_PARAMETERS;
		goto out;
	}

	sdei_event_number_t ev_num = sdei_event_number_cast((uint32_t)arg1);
	if (!sdei_event_valid(ev_num) ||
	    (sdei_get_ev_type(ev_num) != SDEI_EV_TYPE_SHARED)) {
		ret = SDEI_RET_INVALID_PARAMETERS;
		goto out;
	}

	sdei_event_t *event = sdei_get_event(ev_num, vcpu);
	spinlock_acquire(&event->lock);

	if (event->state != SDEI_EVENT_STATE_HANDLER_REGISTERED) {
		ret = SDEI_RET_DENIED;
		goto out_unlock;
	}

	bool rm = sdei_event_routing_set_routing_mode_get_routing_mode(
		&routing_mode);
	sdei_mpidr_t affinity = sdei_mpidr_cast(arg3);
	ret = sdei_shared_event_routing_set(event, rm, affinity);
out_unlock:
	spinlock_release(&event->lock);
out:
	*ret0 = (uint64_t)ret;
	return true;
}

bool
sdei_dispatcher_handle_sdei_pe_mask(uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	ret = sdei_mask_vcpu(vcpu) ? 1 : 0;
out:
	*ret0 = (uint64_t)ret;
	return true;
}

bool
sdei_dispatcher_handle_sdei_pe_unmask(uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	sdei_unmask_vcpu(vcpu);

	ret = SDEI_RET_SUCCESS;
out:
	*ret0 = (uint64_t)ret;
	return true;
}

bool
sdei_dispatcher_handle_sdei_interrupt_bind(uint64_t arg1, uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	(void)arg1;
	ret = SDEI_RET_OUT_OF_RESOURCE;
out:
	*ret0 = (uint64_t)ret;
	return true;
}

bool
sdei_dispatcher_handle_sdei_interrupt_release(uint64_t arg1, uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	(void)arg1;
	ret = SDEI_RET_INVALID_PARAMETERS;
out:
	*ret0 = (uint64_t)ret;
	return true;
}

bool
sdei_dispatcher_handle_sdei_event_signal(uint64_t arg1, uint64_t arg2,
					 uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	sdei_event_number_t ev_num = sdei_event_number_cast((uint32_t)arg1);
	if (!sdei_event_valid(ev_num) ||
	    (sdei_get_ev_type(ev_num) != SDEI_EV_TYPE_PRIVATE) ||
	    (sdei_get_ev_signaled(ev_num) != SDEI_EV_SIGNALED_YES)) {
		ret = SDEI_RET_INVALID_PARAMETERS;
		goto out;
	}

	rcu_read_start();
	thread_ptr_result_t target =
		sdei_get_vcpu_from_mpidr(vcpu->vgic_vic, sdei_mpidr_cast(arg2));
	if (target.e != OK) {
		ret = SDEI_RET_INVALID_PARAMETERS;
		rcu_read_finish();
		goto out;
	}

	sdei_event_t *event = sdei_get_event(ev_num, target.r);
	sdei_trigger_event(event, target.r);
	rcu_read_finish();

	ret = SDEI_RET_SUCCESS;
out:
	*ret0 = (uint64_t)ret;
	return true;
}

bool
sdei_dispatcher_handle_sdei_features(uint64_t arg1, uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	uint32_t feature = (uint32_t)arg1;
	// The validity of feature is checked in the features event handlers.
	ret = trigger_sdei_dispatcher_features_event(feature);
out:
	*ret0 = (uint64_t)ret;
	return true;
}

bool
sdei_dispatcher_handle_sdei_private_reset(uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	sdei_event_t *event = sdei_get_standard_event0(vcpu);

	spinlock_acquire(&event->lock);
	(void)sdei_unregister_event(event);
	spinlock_release(&event->lock);

	// No sync needed, any race conditions are the client's problem.
	ret = (atomic_load_relaxed(&vcpu->sdei->curr_event) != NULL)
		      ? SDEI_RET_DENIED
		      : SDEI_RET_SUCCESS;
out:
	*ret0 = (uint64_t)ret;
	return true;
}

bool
sdei_dispatcher_handle_sdei_shared_reset(uint64_t *ret0)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		ret = SDEI_RET_NOT_SUPPORTED;
		goto out;
	}

	sdei_event_t *event = sdei_get_vendor_error_event(vcpu);
	spinlock_acquire(&event->lock);
	(void)sdei_unregister_event(event);
	spinlock_release(&event->lock);

	// No sync needed, any race conditions are the client's problem.
	ret = (atomic_load_relaxed(&vcpu->sdei->curr_event) != NULL)
		      ? SDEI_RET_DENIED
		      : SDEI_RET_SUCCESS;
out:
	*ret0 = (uint64_t)ret;
	return true;
}

int64_t
sdei_dispatcher_handle_sdei_event_get_info_ev_type(sdei_event_number_t ev_num)
{
	return sdei_get_ev_type(ev_num);
}

int64_t
sdei_dispatcher_handle_sdei_event_get_info_ev_signaled(
	sdei_event_number_t ev_num)
{
	return sdei_get_ev_signaled(ev_num);
}

int64_t
sdei_dispatcher_handle_sdei_event_get_info_ev_priority(
	sdei_event_number_t ev_num)
{
	(void)ev_num;
	return SDEI_EV_PRIORITY_CRITICAL;
}

int64_t
sdei_dispatcher_handle_sdei_event_get_info_ev_routing_mode(
	sdei_event_number_t ev_num)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (sdei_get_ev_type(ev_num) != SDEI_EV_TYPE_SHARED) {
		ret = SDEI_RET_INVALID_PARAMETERS;
		goto out;
	}

	sdei_event_t *event = sdei_get_event(ev_num, vcpu);
	spinlock_acquire(&event->lock);

	if (event->state != SDEI_EVENT_STATE_HANDLER_REGISTERED) {
		ret = SDEI_RET_DENIED;
		goto out_unlock;
	}

	ret = event->routing_mode;
out_unlock:
	spinlock_release(&event->lock);
out:
	return ret;
}

int64_t
sdei_dispatcher_handle_sdei_event_get_info_ev_routing_aff(
	sdei_event_number_t ev_num)
{
	int64_t	  ret;
	thread_t *vcpu = thread_get_self();

	if (sdei_get_ev_type(ev_num) != SDEI_EV_TYPE_SHARED) {
		ret = SDEI_RET_INVALID_PARAMETERS;
		goto out;
	}

	sdei_event_t *event = sdei_get_event(ev_num, vcpu);
	spinlock_acquire(&event->lock);

	if (event->routing_mode != SDEI_EV_ROUTING_MODE_RM_PE) {
		ret = SDEI_RET_INVALID_PARAMETERS;
		goto out_unlock;
	}

	if (event->state != SDEI_EVENT_STATE_HANDLER_REGISTERED) {
		ret = SDEI_RET_DENIED;
		goto out_unlock;
	}

	ret = (int64_t)sdei_mpidr_raw(
		sdei_get_mpidr_from_index(event->vic, event->target_idx));
out_unlock:
	spinlock_release(&event->lock);
out:
	return ret;
}

int64_t
sdei_dispatcher_handle_sdei_features_bind_slots(void)
{
	// FIXME: Bind slots not supported.
	sdei_num_bind_slots_t slots = sdei_num_bind_slots_default();
	sdei_num_bind_slots_set_private(&slots, 0U);
	sdei_num_bind_slots_set_shared(&slots, 0U);

	return (int64_t)sdei_num_bind_slots_raw(slots);
}

int64_t
sdei_dispatcher_handle_sdei_features_relative_mode(void)
{
	return 0; // FIXME: Not supported.
}

void
sdei_dispatcher_handle_vcpu_vm_off_request(thread_t *vcpu, bool *defer)
{
	// FIXME: Only called if psci group exists.

	// If a VM makes a reset (or similar call), it is no longer considered
	// busy. The busy state is updated regardless of whether we are in a
	// system grace period.
	sdei_busy_state_t busy_state;

	if (vcpu->sdei != NULL) {
		busy_state = sdei_vm_update_busy_state(vcpu->vgic_vic, false);

		// A (non-critical) VM can cancel its VM-specific grace period
		// by making a reset call. In this case, the VM is treated as if
		// the VM-specific error had not occurred.
		if (sdei_cancel_vm_grace_period(vcpu->vgic_vic)) {
			TRACE_AND_LOG(
				ERROR, WARN,
				"sdei: cancelled grace period for VM {:#x}",
				trace_ids_raw(vcpu->trace_ids));
		}
	} else {
		busy_state = sdei_get_busy_state();
	}

	// If we aren't in a system grace period, no further handling is
	// required.
	if (!sdei_in_system_grace_period()) {
		goto out;
	}

	// We are in a system grace period. Reset calls made by a VM during a
	// system grace period are handled as follows:
	//
	// 1. Critical VMs which usually have their reset calls translated to
	// physical ones, are blocked instead.
	//
	// 2. If all relevant VMs for the current grace period are no longer
	// busy, the grace period is finished early.
	//
	// 3. Under no circumstances will a physical reset take place during or
	// after a system grace period.
	sdei_check_system_grace_period_early_finish(busy_state);

	if (vcpu_option_flags_get_critical(&vcpu->vcpu_options)) {
		TRACE_AND_LOG(ERROR, WARN,
			      "sdei: critical VM {:d}, blocking system reset",
			      trace_ids_get_vmid(&vcpu->trace_ids));

		scheduler_lock_nopreempt(vcpu);
		scheduler_block(vcpu, SCHEDULER_BLOCK_SDEI_DEFER);
		scheduler_unlock_nopreempt(vcpu);
		*defer = true;
	}
out:
	return;
}

void
sdei_dispatcher_handle_vcpu_vm_off_failed(thread_t *vcpu)
{
	if (vcpu->sdei != NULL) {
		vic_t	     *vic   = vcpu->vgic_vic;
		sdei_event_t *event = sdei_get_vendor_error_event_from_vic(vic);

		spinlock_acquire(&event->lock);
		bool busy = event->state !=
			    SDEI_EVENT_STATE_HANDLER_UNREGISTERED;
		(void)sdei_vm_update_busy_state(vic, busy);
		spinlock_release(&event->lock);
	}
}

hypercall_sdei_get_error_flags_result_t
hypercall_sdei_get_error_flags(void)
{
	error_t		   err;
	sdei_error_flags_t flags;
	thread_t	  *vcpu = thread_get_self();

	if (vcpu->sdei == NULL) {
		err   = ERROR_UNIMPLEMENTED;
		flags = sdei_error_flags_default();
		goto out;
	}

	if (!vcpu->sdei->active) {
		err   = ERROR_DENIED;
		flags = sdei_error_flags_default();
		goto out;
	}

	sdei_event_t *event = atomic_load_relaxed(&vcpu->sdei->curr_event);
	if (event != sdei_get_vendor_error_event(vcpu)) {
		err   = ERROR_DENIED;
		flags = sdei_error_flags_default();
		goto out;
	}

	flags = atomic_load_relaxed(&event->vic->sdei_error_flags);
	err   = OK;
out:
	return (hypercall_sdei_get_error_flags_result_t){ .error  = err,
							  .result = flags };
}

error_t
sdei_dispatcher_virq_bind_shared(virq_source_t *source, vic_t *vic, virq_t virq,
				 virq_trigger_t trigger)
{
	error_t ret;

	if (!sdei_virq_valid(virq)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (atomic_fetch_or_explicit(&source->vgic_is_bound, true,
				     memory_order_acquire)) {
		ret = ERROR_VIRQ_BOUND;
		goto out;
	}

	// Allow multiple sources to bind to the same virq.
	source->virq		= virq;
	source->trigger		= trigger;
	source->is_private	= false;
	source->vgic_gicr_index = CPU_INDEX_INVALID;
	atomic_store_release(&source->vic, vic);
	ret = OK;
out:
	return ret;
}

bool_result_t
sdei_dispatcher_virq_assert(const virq_source_t *source, vic_t *vic)
{
	(void)source;

	// Only the vendor error event can be bound.
	sdei_error_flags_t flags = sdei_error_flags_default();
	sdei_error_flags_set_reason(&flags, SDEI_ERROR_REASON_SENT_BY_USER);
	sdei_trigger_error_event(vic, flags);

	return (bool_result_t){ .r = true, .e = OK };
}

#if defined(MODULE_MISC_WATCHDOG)
static void
sdei_maybe_start_vm_grace_period_watchdog(watchdog_t *wdt, vic_t *vic)
{
	sdei_error_flags_t flags = sdei_error_flags_default();
	sdei_error_flags_set_reason(&flags,
				    SDEI_ERROR_REASON_VIRTUAL_WATCHDOG_BITE);

	if (sdei_start_vm_grace_period(vic, flags)) {
		watchdog_t *old_wdt =
			atomic_exchange_relaxed(&vic->sdei_vm_grace_wdt, wdt);
		if (old_wdt != NULL) {
			object_put_watchdog(old_wdt);
		}

		// The watchdog holds a reference to the vic, so this
		// will also keep the vic alive.
		object_get_watchdog_additional(wdt);
	}
}
#endif

bool
sdei_dispatcher_handle_watchdog_non_hyp_bite(watchdog_t *wdt)
{
	bool handled;

#if defined(MODULE_MISC_WATCHDOG)
	if (watchdog_option_flags_get_critical_bite(&wdt->options)) {
		if (sdei_is_busy(sdei_get_busy_state(), SDEI_BUSY_SELECT_ANY)) {
			sdei_error_flags_t flags = sdei_error_flags_default();
			sdei_error_flags_set_reason(
				&flags,
				SDEI_ERROR_REASON_VIRTUAL_WATCHDOG_BITE);
			sdei_start_system_grace_period(
				"sdei: critical watchdog bite",
				ABORT_REASON_WATCHDOG_BITE, flags);
			handled = true;
		} else {
			handled = false;
		}
	} else {
		vic_t *vic = atomic_load_relaxed(&wdt->sdei_vm);

		if ((vic != NULL) && sdei_vm_is_busy(vic)) {
			sdei_maybe_start_vm_grace_period_watchdog(wdt, vic);
			handled = true;
		} else {
			handled = false;
		}
	}
#else
	(void)wdt;
	handled = false;
#endif

	return handled;
}
