// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <abort.h>
#include <atomic.h>
#include <attributes.h>
#include <compiler.h>
#include <cpulocal.h>
#include <globals.h>
#include <ipi.h>
#include <log.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <platform_watchdog.h>
#include <preempt.h>
#include <rcu.h>
#include <scheduler.h>
#include <thread.h>
#include <trace.h>
#include <util.h>
#include <vdevice.h>
#include <vic.h>
#include <virq.h>
#include <watchdog.h>

#include <events/watchdog.h>

#include <asm/event.h>

#include "event_handlers.h"
#include "watchdog_queue.h"

// Basic design:
// A hardware watchdog supporting a bark time is programmed with the nearest
// virtual watchdog bark time. Each time a virtual watchdog is patted, we pat
// the HW watchdog (which resets its counter to zero), and re-program it with
// the next earliest bark time.
// When the HW bark time is reached, an interrupt is generated and any expired
// virtual watchdog bark timeouts will signal a virtual interrupt. At this time,
// the virtual watchdog's bite time may then be enqueued.
//
// When a virtual watchdog bite timeout occurs, we trigger an abort. Future
// implementations may instead allow signalling of an interrupt to another VM.
//
// The hardware bite time and bite IRQ are not used, as the hardware bite IRQ
// goes directly to TZ.

static watchdog_t *watchdog_fatal;
static watchdog_t *watchdog_hyp;

watchdog_t *
watchdog_get_fatal(void)
{
	return watchdog_fatal;
}

static void
watchdog_bite(watchdog_t *wdt, watchdog_abs_ticks_t now)
	REQUIRE_LOCK(watchdog_queue.lock) REQUIRE_PREEMPT_DISABLED
{
	assert(wdt != NULL);

	watchdog_dequeue(wdt);

	// disable it to prevent further patting
	wdt->enabled = false;

	// Is this hypervisor's watchdog?
	if (wdt->is_hyp) {
		// Should not happen! Log it anyway.
		LOG(ERROR, PANIC, "WDT: Hypervisor watchdog bite!");
	}

	TRACE_AND_LOG(ERROR, PANIC,
		      "WDT bite: now_ticks {:d}, last_pat {:d}, from VM {:d}",
		      now, wdt->last_pat, wdt->debug_id);

	// If bite fatal is set, it will always trigger a crash.
	if (watchdog_option_flags_get_fatal_bite(&wdt->options)) {
		watchdog_fatal = wdt;
		watchdog_queue_release_lock();
		abort_kernel("Watchdog bite (fatal_bite)",
			     ABORT_REASON_WATCHDOG_BITE);
	}

	// If it was a critical watchdog, set the fatal watchdog while we still
	// have the lock.
	if (watchdog_option_flags_get_critical_bite(&wdt->options)) {
		watchdog_fatal = wdt;
	}

	// Release the lock and trigger the watchdog bite event.
	watchdog_queue_release_lock();
	if (!wdt->is_hyp && trigger_watchdog_non_hyp_bite_event(wdt)) {
		watchdog_queue_acquire_lock();
		goto out;
	}

	// Abort if the watchdog has been marked as critical, otherwise inject
	// the bite VIRQ to the handler VM.
	if (watchdog_option_flags_get_critical_bite(&wdt->options)) {
		abort_kernel("Watchdog bite", ABORT_REASON_WATCHDOG_BITE);
	} else {
		(void)virq_assert(&wdt->bite_virq_src, false);
		watchdog_queue_acquire_lock();
	}

out:
	return;
}

// When hypervisor or a VM pats its watchdog.
// Must be called with the watchdog queue and object's locks held, in that
// order.
static void
watchdog_op_pat(watchdog_t *wdt, bool skip_hw_sync)
	REQUIRE_LOCK(watchdog_queue.lock) REQUIRE_PREEMPT_DISABLED
{
	watchdog_abs_ticks_t bark_abs, bite_abs;

	assert(wdt != NULL);
	assert(wdt->enabled);

	watchdog_abs_ticks_t hw_last_pat = platform_watchdog_get_last_pat();
	watchdog_abs_ticks_t now =
		hw_last_pat + platform_watchdog_get_counter();
	bool was_head = (wdt == watchdog_queue_head());

	// Update when this watchdog was last patted.
	wdt->last_pat = now;

	// Queue it again at the correct position.
	watchdog_dequeue(wdt);
	bark_abs = now + platform_watchdog_ms_to_ticks(wdt->bark_time);
	bite_abs = now + platform_watchdog_ms_to_ticks(wdt->bite_time);
	if (bite_abs <= bark_abs) {
		// Bite time is less than bark time. Queue the bite time
		// instead.
		watchdog_enqueue(wdt, bite_abs, WATCHDOG_ACTION_BITE);
	} else {
		watchdog_enqueue(wdt, bark_abs, WATCHDOG_ACTION_BARK);
	}

	// If the head has changed, program the hardware watchdog with the next
	// timeout. The timeout times in the queue are absolute values, so we
	// need to program the hardware watchdog relative to when it was last
	// patted, which in this case is "now".
	if (was_head && !skip_hw_sync) {
		watchdog_t *head = watchdog_queue_head();
		// The queue should not be empty, as hypervisor watchdog is
		// always in the queue.
		assert(head != NULL);

		if (head->next_timeout > (now + WATCHDOG_WRITE_LATENCY_TICKS)) {
			watchdog_ticks_t new_hw_bark =
				(watchdog_ticks_t)(head->next_timeout - now);
			platform_watchdog_set_bark(new_hw_bark);
			platform_watchdog_pat();
		} else {
			assert_cpulocal_safe();
			cpu_index_t this_cpu = cpulocal_get_index();
			ipi_one_relaxed(IPI_REASON_WATCHDOG_SYNC, this_cpu);
		}
	}

	TRACE_LOCAL(INFO, INFO, "WDT Pat: now_ticks {:d}, VM {:d}", now,
		    wdt->debug_id);
}

// Add a watchdog object to the queue (when hypervisor or a VM enables its
// watchdog), or update a watchdog object that is already queued (when a VM
// updates its bark or bite time).
// Must be called with the watchdog queue and object's locks held, in that
// order.
static void
watchdog_op_add_update(watchdog_t *wdt) REQUIRE_LOCK(watchdog_queue.lock)
REQUIRE_PREEMPT_DISABLED
{
	watchdog_abs_ticks_t bark_abs, bite_abs;

	assert(wdt != NULL);
	assert(wdt->enabled);

	watchdog_abs_ticks_t hw_last_pat = platform_watchdog_get_last_pat();
	watchdog_abs_ticks_t now =
		hw_last_pat + platform_watchdog_get_counter();

	watchdog_ticks_t bark_ticks =
		platform_watchdog_ms_to_ticks(wdt->bark_time);
	watchdog_ticks_t bite_ticks =
		platform_watchdog_ms_to_ticks(wdt->bite_time);

	if (wdt->next_timeout != WATCHDOG_INVALID_ABS_TIMEOUT) {
		// This watchdog was already enabled. Remove it from the queue
		// so we can add it again with the updated times.
		watchdog_dequeue(wdt);
	} else {
		// This watchdog is becoming enabled. Update the last_pat to
		// reflect this.
		if (wdt->remaining_time == WATCHDOG_INVALID_ABS_TIMEOUT) {
			// Watchdog is being enabled for the first time.
			wdt->last_pat = now;
		} else {
			// Watchdog was disabled in the past, and is being
			// enabled again. What we put in last_pat depends on
			// whether its bark time or bite time was queued before
			// it was disabled.
			if (wdt->action == WATCHDOG_ACTION_BARK) {
				wdt->last_pat = now - (bark_ticks -
						       wdt->remaining_time);
			} else {
				wdt->last_pat = now - (bite_ticks -
						       wdt->remaining_time);
			}
		}
	}

	// The new values are relative to the last time the watchdog was patted.
	bark_abs = wdt->last_pat + bark_ticks;
	bite_abs = wdt->last_pat + bite_ticks;

	bool need_sync = false;

	// If the bark or bite time is in the past, we need to handle it, so
	// send a watchdog sync IPI.
	if (bite_abs <= (now + WATCHDOG_WRITE_LATENCY_TICKS)) {
		need_sync = true;
	} else if (bark_abs <= (now + WATCHDOG_WRITE_LATENCY_TICKS)) {
		assert(!wdt->is_hyp);

		need_sync = true;
	} else {
		// Both bark & bite time are in the present. No need to sync.
	}

	if (bite_abs <= bark_abs) {
		assert(!wdt->is_hyp);
		// Bite time is less than bark time. Queue the bite time
		// instead.
		watchdog_enqueue(wdt, bite_abs, WATCHDOG_ACTION_BITE);
	} else {
		watchdog_enqueue(wdt, bark_abs, WATCHDOG_ACTION_BARK);
	}

	if (need_sync) {
		assert_cpulocal_safe();
		cpu_index_t this_cpu = cpulocal_get_index();
		ipi_one_relaxed(IPI_REASON_WATCHDOG_SYNC, this_cpu);
	} else {
		// Is this the new head of the queue? If yes, program the
		// hardware watchdog, relative to the last time it was
		// patted.
		if (wdt == watchdog_queue_head()) {
			watchdog_ticks_t new_hw_bark =
				(watchdog_ticks_t)(wdt->next_timeout -
						   hw_last_pat);
			platform_watchdog_set_bark(new_hw_bark);
		}
	}
}

// Remove a watchdog object from the queue (when a VM disables its watchdog).
// Must be called with the watchdog queue and object's locks held, in that
// order.
static void
watchdog_op_remove(watchdog_t *wdt) REQUIRE_LOCK(watchdog_queue.lock)
REQUIRE_PREEMPT_DISABLED
{
	assert(wdt != NULL);
	assert(!wdt->is_hyp);

	watchdog_abs_ticks_t hw_last_pat = platform_watchdog_get_last_pat();
	watchdog_abs_ticks_t now =
		hw_last_pat + platform_watchdog_get_counter();
	bool was_head = (wdt == watchdog_queue_head());

	// Record the remaining time for re-enablement.
	wdt->remaining_time = wdt->next_timeout - now;

	// If it is queued, remove it
	if (wdt->next_timeout != WATCHDOG_INVALID_ABS_TIMEOUT) {
		watchdog_dequeue(wdt);

		// If the head has changed, program the hardware watchdog with
		// the next timeout.
		if (was_head) {
			watchdog_t *head = watchdog_queue_head();
			// The queue should not be empty, as hypervisor
			// watchdog is always in the queue.
			assert(head != NULL);
			if (head->next_timeout >
			    (now + WATCHDOG_WRITE_LATENCY_TICKS)) {
				platform_watchdog_set_bark(
					(watchdog_ticks_t)(head->next_timeout -
							   hw_last_pat));
			} else {
				// The head of the queue is too close to "now",
				// reprogram the timer. Do nothing, the next IRQ
				// is coming soon.
			}
		}
	}
}

// A VM has had a bark, queue its bite time.
static void
watchdog_op_bark(watchdog_t *wdt, watchdog_abs_ticks_t now)
	REQUIRE_LOCK(watchdog_queue.lock) REQUIRE_PREEMPT_DISABLED
{
	watchdog_abs_ticks_t bite_abs;

	assert(wdt != NULL);
	assert(!wdt->is_hyp);
	assert(wdt->enabled);
	assert(wdt->next_timeout != WATCHDOG_INVALID_ABS_TIMEOUT);

	bite_abs =
		wdt->last_pat + platform_watchdog_ms_to_ticks(wdt->bite_time);

	if (bite_abs <= (now + WATCHDOG_WRITE_LATENCY_TICKS)) {
		// Bite immediately!
		watchdog_bite(wdt, now);
	} else {
		watchdog_dequeue(wdt);
		watchdog_enqueue(wdt, bite_abs, WATCHDOG_ACTION_BITE);
	}

	watchdog_queue_release_lock();
	(void)virq_assert(&wdt->bark_virq_src, false);
	watchdog_queue_acquire_lock();
}

static void
watchdog_control_unlocked(watchdog_t *wdt, bool enable)
	REQUIRE_LOCK(watchdog_queue.lock) REQUIRE_PREEMPT_DISABLED
{
	assert(wdt != NULL);
	assert(!wdt->is_hyp);

	TRACE_LOCAL(INFO, INFO, "WDT Control: VM {:d}, enable {:d}",
		    wdt->debug_id, enable ? 1 : 0);

	// Is the state changing?
	if (wdt->enabled != enable) {
		if (enable) {
			if (wdt->bark_time == WATCHDOG_INVALID_TIME) {
				wdt->bark_time = platform_watchdog_ticks_to_ms(
					WATCHDOG_TIMEOUT_MAX_TICKS);
			}
			if (wdt->bite_time == WATCHDOG_INVALID_TIME) {
				wdt->bite_time = platform_watchdog_ticks_to_ms(
					WATCHDOG_TIMEOUT_MAX_TICKS);
			}
			wdt->enabled = true;
			watchdog_op_add_update(wdt);
		} else {
			wdt->enabled = false;
			watchdog_op_remove(wdt);
		}
	}
}

void
watchdog_restore(void)
{
	watchdog_t *wdt = watchdog_queue_head();
	// Hypervisor's watchdog should always be in the queue.
	assert(wdt != NULL);

	watchdog_abs_ticks_t now = platform_watchdog_get_last_pat();

	platform_watchdog_init();

	platform_watchdog_set_bite(
		platform_watchdog_ms_to_ticks(WATCHDOG_DEFAULT_BITE_MS));

	// Program the hardware watchdog to the head of the queue relative
	// to the last time it was patted.
	platform_watchdog_set_bark((watchdog_ticks_t)(wdt->next_timeout - now));
	platform_watchdog_pat();

	// Now enable the watchdog
	platform_watchdog_set_enable(true);
}

void
watchdog_control(watchdog_t *wdt, bool enable)
{
	assert(wdt != NULL);
	assert(!wdt->is_hyp);

	watchdog_queue_acquire_lock();
	if (wdt->freeze_count == 0U) {
		watchdog_control_unlocked(wdt, enable);
	} else {
		TRACE_LOCAL(INFO, INFO,
			    "WDT Control: VM {:d}, enable {:d} (frozen {:d})",
			    wdt->debug_id, enable ? 1 : 0, wdt->freeze_count);
		if (enable && !wdt->was_enabled) {
			if (wdt->bark_time == WATCHDOG_INVALID_TIME) {
				wdt->bark_time = platform_watchdog_ticks_to_ms(
					WATCHDOG_TIMEOUT_MAX_TICKS);
			}
			if (wdt->bite_time == WATCHDOG_INVALID_TIME) {
				wdt->bite_time = platform_watchdog_ticks_to_ms(
					WATCHDOG_TIMEOUT_MAX_TICKS);
			}
		}
		wdt->was_enabled = enable;
	}
	watchdog_queue_release_lock();
}

bool
watchdog_is_enabled(watchdog_t *wdt)
{
	assert(wdt != NULL);

	return wdt->enabled;
}

bool
watchdog_is_expired(watchdog_t *wdt)
{
	assert(wdt != NULL);

	// A watchdog is considered expired if it is enabled and its bite time
	// is queued, meaning its bark time has expired.
	return (wdt->enabled && (wdt->action == WATCHDOG_ACTION_BITE));
}

watchdog_milliseconds_t
watchdog_ms_since_last_pat(watchdog_t *wdt)
{
	assert(wdt != NULL);

	watchdog_queue_acquire_lock();
	watchdog_abs_ticks_t	now = platform_watchdog_get_last_pat() +
				      platform_watchdog_get_counter();
	watchdog_milliseconds_t ms  = platform_watchdog_ticks_to_ms(
		(watchdog_ticks_t)(now - wdt->last_pat));
	watchdog_queue_release_lock();

	return ms;
}

watchdog_milliseconds_t
watchdog_get_bark(watchdog_t *wdt)
{
	assert(wdt != NULL);
	return wdt->bark_time;
}

watchdog_milliseconds_t
watchdog_get_bite(watchdog_t *wdt)
{
	assert(wdt != NULL);
	return wdt->bite_time;
}

static bool
watchdog_set_times_unlocked(watchdog_t *wdt, watchdog_milliseconds_t bark_ms,
			    watchdog_milliseconds_t bite_ms)
	REQUIRE_LOCK(watchdog_queue.lock) REQUIRE_PREEMPT_DISABLED
{
	bool update;

	assert(wdt != NULL);

	update = false;
	if (bark_ms != WATCHDOG_TIMEOUT_NOCHANGE) {
		if (bark_ms >= WATCHDOG_TIMEOUT_RESERVED) {
			bark_ms = WATCHDOG_TIMEOUT_RESERVED;
		}
		wdt->bark_time = bark_ms;
		update	       = true;
	}
	if (bite_ms != WATCHDOG_TIMEOUT_NOCHANGE) {
		if (bite_ms >= WATCHDOG_TIMEOUT_RESERVED) {
			bite_ms = WATCHDOG_TIMEOUT_RESERVED;
		}
		wdt->bite_time = bite_ms;
		update	       = true;
	}

	TRACE_LOCAL(INFO, INFO, "WDT Set times: VM {:d}, bark {:d}, bite {:d}",
		    wdt->debug_id, bark_ms, bite_ms);

	if (update) {
		wdt->remaining_time = WATCHDOG_INVALID_ABS_TIMEOUT;
		if (wdt->enabled) {
			// Propagate the changes immediately
			watchdog_op_add_update(wdt);
		}
	}

	return update;
}

bool
watchdog_set_times(watchdog_t *wdt, watchdog_milliseconds_t bark_ms,
		   watchdog_milliseconds_t bite_ms)
{
	assert(wdt != NULL);

	watchdog_queue_acquire_lock();
	bool ret = watchdog_set_times_unlocked(wdt, bark_ms, bite_ms);
	watchdog_queue_release_lock();

	return ret;
}

bool
watchdog_set_active_time(watchdog_t *wdt, watchdog_milliseconds_t timeout_ms)
{
	bool ret;
	assert(wdt != NULL);

	watchdog_queue_acquire_lock();
	if (wdt->action == WATCHDOG_ACTION_BARK) {
		ret = watchdog_set_times_unlocked(wdt, timeout_ms,
						  WATCHDOG_TIMEOUT_NOCHANGE);
	} else {
		ret = watchdog_set_times_unlocked(
			wdt, WATCHDOG_TIMEOUT_NOCHANGE, timeout_ms);
	}
	watchdog_queue_release_lock();

	return ret;
}

void
watchdog_pat(watchdog_t *wdt)
{
	assert(wdt != NULL);

	watchdog_queue_acquire_lock();
	trigger_watchdog_pat_pre_event(wdt);

	if (wdt->enabled) {
		watchdog_op_pat(wdt, false);
	}
	wdt->remaining_time = WATCHDOG_INVALID_ABS_TIMEOUT;
	watchdog_queue_release_lock();
}

void
watchdog_pat_hyp(void)
{
	assert(watchdog_hyp != NULL);

	watchdog_pat(watchdog_hyp);
}

error_t
watchdog_configure(watchdog_t		  *watchdog,
		   watchdog_option_flags_t watchdog_options)
{
	error_t ret = OK;

	assert(watchdog != NULL);

	// Check for unknown option flags
	if (!watchdog_option_flags_is_clean(watchdog_options)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	watchdog->options = watchdog_options;
out:
	return ret;
}

error_t
watchdog_handle_object_create_watchdog(watchdog_create_t watchdog_create)
{
	watchdog_t *wdt = watchdog_create.watchdog;
	assert(wdt != NULL);

	wdt->enabled	    = false;
	wdt->was_enabled    = false;
	wdt->is_hyp	    = false;
	wdt->bark_logged    = false;
	wdt->freeze_count   = 0;
	wdt->debug_id	    = 0;
	wdt->action	    = WATCHDOG_ACTION_BARK;
	wdt->bark_time	    = WATCHDOG_INVALID_TIME;
	wdt->bite_time	    = WATCHDOG_INVALID_TIME;
	wdt->next_timeout   = WATCHDOG_INVALID_ABS_TIMEOUT;
	wdt->remaining_time = WATCHDOG_INVALID_ABS_TIMEOUT;
	wdt->last_pat	    = 0U;

	return OK;
}

void
watchdog_handle_object_deactivate_watchdog(watchdog_t *watchdog)
{
	assert(watchdog != NULL);

	vic_unbind(&watchdog->bark_virq_src);
	vic_unbind(&watchdog->bite_virq_src);
}

static_assert(WATCHDOG_DEFAULT_BITE_MS > (watchdog_milliseconds_t)1000,
	      "bite time too short");
static_assert(WATCHDOG_DEFAULT_BITE_MS > WATCHDOG_DEFAULT_BARK_GRACE,
	      "invalid watchdog grace time");
static_assert(WATCHDOG_DEFAULT_HYP_BARK_MS > (watchdog_milliseconds_t)1000,
	      "bark_time too short");

void
watchdog_handle_boot_hypervisor_start(void)
{
	const global_options_t *options = globals_get_options();
	if (!global_options_get_watchdog_disabled(options)) {
		// Create the hypervisor watchdog.
		watchdog_create_t     params = { 0 };
		watchdog_ptr_result_t result = partition_allocate_watchdog(
			partition_get_private(), params);
		if (result.e != OK) {
			panic("WDT: Failed to create hypervisor watchdog");
		}
		watchdog_t *wdt = result.r;

		watchdog_option_flags_t watchdog_options =
			watchdog_option_flags_default();
		watchdog_option_flags_set_critical_bite(&watchdog_options,
							true);

		if (watchdog_configure(wdt, watchdog_options) != OK) {
			panic("WDT: Failed to configure hypervisor watchdog");
		}

		if (object_activate_watchdog(wdt) != OK) {
			panic("WDT: Failed to activate hypervisor watchdog");
		}

		// Set the hypervisor watchdog bark and bite times. The bark is
		// less than the default bite time. This way VMs can have bark
		// times longer than default bite time.
		wdt->bark_time = WATCHDOG_DEFAULT_HYP_BARK_MS;
		wdt->bite_time = WATCHDOG_DEFAULT_BITE_MS;
		wdt->is_hyp    = true;
		wdt->enabled   = true;

		watchdog_queue_acquire_lock();
		watchdog_op_add_update(wdt);
		watchdog_queue_release_lock();

		watchdog_hyp = wdt;

		// Program the hardware bite register with the default value
		platform_watchdog_set_bite(platform_watchdog_ms_to_ticks(
			WATCHDOG_DEFAULT_BITE_MS));
		platform_watchdog_reset();
	}
}

error_t
watchdog_freeze(watchdog_t *wdt, bool reset)
{
	error_t err;
	assert(wdt != NULL);

	watchdog_queue_acquire_lock();
	if (util_add_overflows(wdt->freeze_count, 1U)) {
		err = ERROR_BUSY;
		goto out;
	}
	if (wdt->freeze_count == 0U) {
		wdt->was_enabled = wdt->enabled;
		if (wdt->enabled) {
			TRACE_LOCAL(INFO, INFO, "WDT: Turn off VM {:d}",
				    wdt->debug_id);

			// Turn the VM's watchdog off before going to sleep
			watchdog_control_unlocked(wdt, false);
		}
	} else {
		assert(!wdt->enabled);
	}
	if (reset) {
		// Give the VM the full bark period when it wakes up
		wdt->remaining_time = WATCHDOG_INVALID_ABS_TIMEOUT;
	}
	wdt->freeze_count++;
	TRACE(INFO, INFO, "WDT: Frozen for VM {:d} (count {:d})", wdt->debug_id,
	      wdt->freeze_count);
	err = OK;
out:
	watchdog_queue_release_lock();
	return err;
}

error_t
watchdog_unfreeze(watchdog_t *wdt)
{
	error_t err;
	assert(wdt != NULL);

	watchdog_queue_acquire_lock();
	if (wdt->freeze_count == 0U) {
		err = ERROR_BUSY;
		goto out;
	}
	wdt->freeze_count--;
	TRACE(INFO, INFO, "WDT: Unfrozen for VM {:d} (count {:d})",
	      wdt->debug_id, wdt->freeze_count);
	if (wdt->freeze_count == 0U) {
		if (wdt->was_enabled) {
			TRACE_LOCAL(INFO, INFO, "WDT: Turn on VM {:d}",
				    wdt->debug_id);

			// VM's watchdog was on before going to sleep, turn it
			// back on
			watchdog_control_unlocked(wdt, true);
		}
	}
	err = OK;
out:
	watchdog_queue_release_lock();
	return err;
}

void
watchdog_handle_vpm_system_suspend(thread_t *vcpu)
{
	assert(vcpu != NULL);
	watchdog_t *wdt = vcpu->watchdog;

	if (wdt == NULL) {
		// This VM doesn't have a watchdog object, nothing to do
		goto out;
	}

	(void)watchdog_freeze(wdt, true);

out:
	return;
}

void
watchdog_handle_vpm_system_resume(thread_t *vcpu)
{
	assert(vcpu != NULL);
	watchdog_t *wdt = vcpu->watchdog;

	if (wdt == NULL) {
		// This VM doesn't have a watchdog object, nothing to do
		goto out;
	}

	(void)watchdog_unfreeze(wdt);
out:
	return;
}

bool
watchdog_handle_virq_check_pending_bark(virq_source_t *source)
{
	assert(source != NULL);
	watchdog_t *wdt = watchdog_container_of_bark_virq_src(source);

	watchdog_queue_acquire_lock();
	bool pending = watchdog_is_expired(wdt);
	watchdog_queue_release_lock();

	return pending;
}

bool
watchdog_handle_virq_check_pending_bite(virq_source_t *source)
{
	assert(source != NULL);

	watchdog_t *wdt = watchdog_container_of_bite_virq_src(source);

	// A bite has occurred if the watchdog is expired but not queued.
	watchdog_queue_acquire_lock();
	bool pending = watchdog_is_expired(wdt) &&
		       (wdt->next_timeout == WATCHDOG_INVALID_ABS_TIMEOUT);
	watchdog_queue_release_lock();

	return pending;
}

void
watchdog_handle_abort_kernel(abort_reason_t reason)
{
#if RESET_ON_ABORT
	if (reason == ABORT_REASON_WATCHDOG_BITE) {
		TRACE_AND_LOG(ERROR, WARN, "WDT: Triggering NS Watchdog Bite");

		// Cause an immediate watchdog bite
		platform_watchdog_set_bite(1);
		platform_watchdog_reset();

		void *mem = NULL;
		while (1) {
			asm_event_wait(&mem);
		}
	}
#else
	(void)reason;
#endif
}

static void
watchdog_sync(void) REQUIRE_LOCK(watchdog_queue.lock) REQUIRE_PREEMPT_DISABLED
{
	watchdog_abs_ticks_t now = platform_watchdog_get_last_pat() +
				   platform_watchdog_get_counter();
	watchdog_t	    *wdt = watchdog_queue_head();

	if (wdt == NULL) {
		panic("WDT: Watchdog sync with an empty queue");
	}

	if (wdt->next_timeout > (now + WATCHDOG_WRITE_LATENCY_TICKS)) {
		// Its possible that there was a race. Re-program the head of
		// the queue and reset the counter
		goto out;
	}

	do {
		if (wdt->action == WATCHDOG_ACTION_BITE) {
			watchdog_bite(wdt, now);
		} else if (wdt->is_hyp) {
			// If this is hypervisor's watchdog, simply pat and move
			// on.
			watchdog_op_pat(wdt, true);
		} else {
			// It is a VM watchdog that has barked. Inject vIRQ and
			// queue the bite time.
			TRACE_LOCAL(INFO, INFO,
				    "WDT Bark: now_ticks {:d}, last_pat {:d},"
				    " VM {:d}",
				    now, wdt->last_pat, wdt->debug_id);
			// If we have logged a bark for this watchdog object in
			// the past, don't log it again. Some VMs pat their
			// watchdogs in the bark ISR and this prevents them from
			// quickly filling up the log buffer. The behaviour
			// stays the same for the VMs that treat the bark event
			// as fatal.
			if (!wdt->bark_logged) {
				LOG(INFO, INFO,
				    "WDT Bark: now_ticks {:d}, last_pat {:d},"
				    " VM {:d}. Suppressing VM's further bark logs",
				    now, wdt->last_pat, wdt->debug_id);
				wdt->bark_logged = true;
			}

			watchdog_op_bark(wdt, now);
		}

		// Check the next watchdog in the queue.
		wdt = watchdog_queue_head();
		// Hypervisor's watchdog should always be in the queue.
		assert(wdt != NULL);
	} while (wdt->next_timeout <= (now + WATCHDOG_WRITE_LATENCY_TICKS));

out:
	// Program the hardware watchdog to the new head of the queue relative
	// to the last time it was patted.
	// Hypervisor's watchdog should always be in the queue.
	platform_watchdog_set_bark((watchdog_ticks_t)(wdt->next_timeout - now));
	platform_watchdog_pat();
}

// A watchdog operation has occurred that requires synchronisation, process the
// watchdog queue and handle any expired timers and reprogram the platform
// watchdog timer as required.
bool NOINLINE
watchdog_handle_ipi_received(void)
{
	watchdog_queue_acquire_lock();
	watchdog_sync();
	watchdog_queue_release_lock();

	// Any interrupts delivered in virq_assert causing wakeups would have
	// been delivered or done a scheduler_trigger already.
	return false;
}

void
watchdog_handle_platform_watchdog_bark(void)
{
	// We've had a bark interrupt from the hardware watchdog.
	watchdog_queue_acquire_lock();
	watchdog_sync();
	watchdog_queue_release_lock();
}
