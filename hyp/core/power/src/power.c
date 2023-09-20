// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <bitmap.h>
#include <cpulocal.h>
#include <ipi.h>
#include <panic.h>
#include <platform_cpu.h>
#include <power.h>
#include <preempt.h>
#include <rcu.h>
#include <scheduler.h>
#include <spinlock.h>
#include <timer_queue.h>
#include <util.h>

#include <events/power.h>

#include "event_handlers.h"

static ticks_t power_cpu_on_retry_delay_ticks;

static spinlock_t power_system_lock;
static BITMAP_DECLARE(PLATFORM_MAX_CORES, power_system_online_cpus)
	PROTECTED_BY(power_system_lock);
static platform_power_state_t
	power_system_suspend_state PROTECTED_BY(power_system_lock);

CPULOCAL_DECLARE_STATIC(power_voting_t, power_voting);

// This is protected by the lock in the corresponding power_voting_t structure,
// but must remain a separate array because it is exposed in crash minidumps.
CPULOCAL_DECLARE_STATIC(cpu_power_state_t, power_state);

const cpu_power_state_array_t *
power_get_cpu_states_for_debug(void)
{
	return &cpulocal_power_state;
}

void
power_handle_boot_cold_init(cpu_index_t boot_cpu)
{
	power_cpu_on_retry_delay_ticks =
		timer_convert_ns_to_ticks(POWER_CPU_ON_RETRY_DELAY_NS);
	assert(power_cpu_on_retry_delay_ticks != 0U);

	for (cpu_index_t cpu = 0U; cpu < PLATFORM_MAX_CORES; cpu++) {
		spinlock_init(&CPULOCAL_BY_INDEX(power_voting, cpu).lock);
		spinlock_acquire_nopreempt(
			&CPULOCAL_BY_INDEX(power_voting, cpu).lock);

		timer_init_object(
			&CPULOCAL_BY_INDEX(power_voting, cpu).retry_timer,
			TIMER_ACTION_POWER_CPU_ON_RETRY);
		CPULOCAL_BY_INDEX(power_voting, cpu).retry_count = 0U;

		// Initialize the boot CPU's vote count to 1 while booting to
		// prevent the cpu going to suspend. This will be decremented
		// once the rootvm setup is completed and the rootvm VCPU has
		// voted to keep the boot core powered on.
		CPULOCAL_BY_INDEX(power_voting, cpu).vote_count =
			(cpu == boot_cpu) ? 1U : 0U;

		CPULOCAL_BY_INDEX(power_state, cpu) =
			(cpu == boot_cpu) ? CPU_POWER_STATE_COLD_BOOT
					  : CPU_POWER_STATE_OFF;

		spinlock_release_nopreempt(
			&CPULOCAL_BY_INDEX(power_voting, cpu).lock);
	}

	spinlock_init(&power_system_lock);

	// FIXME:
	spinlock_acquire_nopreempt(&power_system_lock);
	bitmap_set(power_system_online_cpus, (index_t)boot_cpu);
	spinlock_release_nopreempt(&power_system_lock);
}

void
power_handle_boot_cpu_warm_init(void)
{
	spinlock_acquire_nopreempt(&CPULOCAL(power_voting).lock);
	cpu_power_state_t state = CPULOCAL(power_state);

	assert((state == CPU_POWER_STATE_COLD_BOOT) ||
	       (state == CPU_POWER_STATE_STARTED) ||
	       (state == CPU_POWER_STATE_SUSPEND));
	CPULOCAL(power_state) = CPU_POWER_STATE_ONLINE;

	if (state == CPU_POWER_STATE_STARTED) {
		trigger_power_cpu_online_event();

#if defined(DISABLE_PSCI_CPU_OFF) && DISABLE_PSCI_CPU_OFF
		power_voting_t *voting = &CPULOCAL(power_voting);
		voting->vote_count++;
#endif
	}
	spinlock_release_nopreempt(&CPULOCAL(power_voting).lock);

	// FIXME:
	spinlock_acquire_nopreempt(&power_system_lock);
	if (bitmap_empty(power_system_online_cpus, PLATFORM_MAX_CORES)) {
		// STARTED could be seen due to a last-cpu-suspend/cpu_on race.
		assert((state == CPU_POWER_STATE_STARTED) ||
		       (state == CPU_POWER_STATE_SUSPEND));
		trigger_power_system_resume_event(power_system_suspend_state);
	}
	bitmap_set(power_system_online_cpus, (index_t)cpulocal_get_index());
	spinlock_release_nopreempt(&power_system_lock);
}

error_t
power_handle_power_cpu_suspend(platform_power_state_t state)
{
	error_t	    err	   = OK;
	cpu_index_t cpu_id = cpulocal_get_index();

	// FIXME:
	spinlock_acquire_nopreempt(&power_system_lock);
	bitmap_clear(power_system_online_cpus, (index_t)cpu_id);
	if (bitmap_empty(power_system_online_cpus, PLATFORM_MAX_CORES)) {
		power_system_suspend_state = state;
		err = trigger_power_system_suspend_event(state);
		if (err != OK) {
			bitmap_set(power_system_online_cpus, (index_t)cpu_id);
		}
	}
	spinlock_release_nopreempt(&power_system_lock);

	if (err == OK) {
		spinlock_acquire_nopreempt(&CPULOCAL(power_voting).lock);
		assert(CPULOCAL(power_state) == CPU_POWER_STATE_ONLINE);
		CPULOCAL(power_state) = CPU_POWER_STATE_SUSPEND;
		spinlock_release_nopreempt(&CPULOCAL(power_voting).lock);
	}

	return err;
}

void
power_handle_power_cpu_resume(bool was_poweroff)
{
	// A cpu that was warm booted updates its state in the cpu warm-boot
	// event.
	if (!was_poweroff) {
		spinlock_acquire_nopreempt(&CPULOCAL(power_voting).lock);
		assert(CPULOCAL(power_state) == CPU_POWER_STATE_SUSPEND);
		CPULOCAL(power_state) = CPU_POWER_STATE_ONLINE;
		spinlock_release_nopreempt(&CPULOCAL(power_voting).lock);

		// FIXME:
		spinlock_acquire_nopreempt(&power_system_lock);
		if (bitmap_empty(power_system_online_cpus,
				 PLATFORM_MAX_CORES)) {
			trigger_power_system_resume_event(
				power_system_suspend_state);
		}
		bitmap_set(power_system_online_cpus,
			   (index_t)cpulocal_get_index());
		spinlock_release_nopreempt(&power_system_lock);
	} else {
		spinlock_acquire_nopreempt(&power_system_lock);
		// power_system_online_cpus should be updated in the warm init
		// event.
		assert(!bitmap_empty(power_system_online_cpus,
				     PLATFORM_MAX_CORES));
		spinlock_release_nopreempt(&power_system_lock);
	}
}

static error_t
power_try_cpu_on(power_voting_t *voting, cpu_index_t cpu)
	REQUIRE_LOCK(voting->lock)
{
	error_t ret;

	if (!platform_cpu_exists(cpu)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	cpu_power_state_t *state = &CPULOCAL_BY_INDEX(power_state, cpu);
	if ((*state != CPU_POWER_STATE_OFF) &&
	    (*state != CPU_POWER_STATE_OFFLINE)) {
		// CPU has already been started, or didn't get to power off.
		ret = OK;
		goto out;
	}

	ret = platform_cpu_on(cpu);

	if (ret == OK) {
		// Mark the CPU as started so we don't call cpu_on twice.
		*state		    = CPU_POWER_STATE_STARTED;
		voting->retry_count = 0U;
		goto out;
	} else if ((ret == ERROR_RETRY) &&
		   (voting->retry_count < MAX_CPU_ON_RETRIES)) {
		// We are racing with a power-off, and it is too late to prevent
		// the power-off completing. We need to wait until power-off is
		// complete and then retry. Enqueue the retry timer, if it is
		// not already queued.
		if (!timer_is_queued(&voting->retry_timer)) {
			timer_enqueue(&voting->retry_timer,
				      timer_get_current_timer_ticks() +
					      power_cpu_on_retry_delay_ticks);
		}

		// If we're racing with power-off, that means the CPU is
		// functional and the power-on should not fail, so report
		// success to the caller. If the retry does fail, we panic.
		ret = OK;
	} else if (ret == ERROR_RETRY) {
		// We ran out of retry attempts.
		ret = ERROR_FAILURE;
	} else {
		// platform_cpu_on() failed and cannot be retried; just return
		// the error status.
	}

out:
	return ret;
}

error_t
power_vote_cpu_on(cpu_index_t cpu)
{
	error_t ret;

	assert(cpulocal_index_valid(cpu));
	power_voting_t *voting = &CPULOCAL_BY_INDEX(power_voting, cpu);

	spinlock_acquire(&voting->lock);
	if (voting->vote_count == 0U) {
		ret = power_try_cpu_on(voting, cpu);
		if (ret != OK) {
			goto out;
		}
	}

	voting->vote_count++;
	ret = OK;

out:
	spinlock_release(&voting->lock);
	return ret;
}

void
power_vote_cpu_off(cpu_index_t cpu)
{
	assert(cpulocal_index_valid(cpu));
	power_voting_t *voting = &CPULOCAL_BY_INDEX(power_voting, cpu);

	spinlock_acquire(&voting->lock);
	assert(voting->vote_count > 0U);
	voting->vote_count--;

	if (voting->vote_count == 0U) {
		// Any outstanding retries can be cancelled.
		voting->retry_count = 0U;
		timer_dequeue(&voting->retry_timer);

		// Send an IPI to rerun the idle handlers in case the CPU
		// is already idle in WFI or suspend.
		ipi_one(IPI_REASON_IDLE, cpu);
	}
	spinlock_release(&voting->lock);
}

idle_state_t
power_handle_idle_yield(bool in_idle_thread)
{
	idle_state_t idle_state = IDLE_STATE_IDLE;

	if (!in_idle_thread) {
		goto out;
	}

	if (rcu_has_pending_updates()) {
		goto out;
	}

	power_voting_t *voting = &CPULOCAL(power_voting);
	spinlock_acquire_nopreempt(&voting->lock);
	if (voting->vote_count == 0U) {
		error_t err = OK;

		spinlock_acquire_nopreempt(&power_system_lock);
		cpu_index_t cpu_id = cpulocal_get_index();
		bitmap_clear(power_system_online_cpus, (index_t)cpu_id);
		if (bitmap_empty(power_system_online_cpus,
				 PLATFORM_MAX_CORES)) {
			power_system_suspend_state =
				(platform_power_state_t){ 0 };
			err = trigger_power_system_suspend_event(
				power_system_suspend_state);
			if (err != OK) {
				bitmap_set(power_system_online_cpus,
					   (index_t)cpu_id);
			}
		}
		spinlock_release_nopreempt(&power_system_lock);

		if (err == OK) {
			assert(CPULOCAL(power_state) == CPU_POWER_STATE_ONLINE);
			trigger_power_cpu_offline_event();
			CPULOCAL(power_state) = CPU_POWER_STATE_OFFLINE;
			spinlock_release_nopreempt(&voting->lock);

			platform_cpu_off();

			idle_state = IDLE_STATE_WAKEUP;
		} else {
			spinlock_release_nopreempt(&voting->lock);
		}
	} else {
		spinlock_release_nopreempt(&voting->lock);
	}

out:
	return idle_state;
}

bool
power_handle_timer_action(timer_t *timer)
{
	assert(timer != NULL);

	power_voting_t *voting = power_voting_container_of_retry_timer(timer);
	cpu_index_t	cpu    = CPULOCAL_PTR_INDEX(power_voting, voting);

	spinlock_acquire_nopreempt(&voting->lock);
	error_t ret = OK;
	if (voting->vote_count > 0U) {
		voting->retry_count++;
		ret = power_try_cpu_on(voting, cpu);
	}
	spinlock_release_nopreempt(&voting->lock);

	if (ret != OK) {
		panic("Failed to power on a CPU that was previously on");
	}

	return true;
}

#if defined(MODULE_VM_ROOTVM)
// The Boot CPU power count is initialised to 1. Decrement the count after the
// root VM initialization.
void
power_handle_rootvm_started(void)
{
	power_vote_cpu_off(cpulocal_get_index());
}
#endif

void
power_handle_boot_hypervisor_handover(void)
{
	// Ensure the running core is the only core online. There is no easy way
	// to do this race-free, but it doesn't really matter for our purpose.
	count_t on_count = 0;
	for (cpu_index_t cpu = 0U; cpu < PLATFORM_MAX_CORES; cpu++) {
		cpu_power_state_t state = CPULOCAL_BY_INDEX(power_state, cpu);
		if ((state != CPU_POWER_STATE_OFF) &&
		    (state != CPU_POWER_STATE_OFFLINE)) {
			on_count++;
		}
	}

	if (on_count != 1U) {
		panic("Hypervisor hand-over requested with multiple CPUs on");
	}
}

#if defined(POWER_START_ALL_CORES)
void
power_handle_boot_hypervisor_start(void)
{
	cpu_index_t boot_cpu = cpulocal_get_index();

	for (cpu_index_t cpu = 0U; cpulocal_index_valid(cpu); cpu++) {
		if (cpu == boot_cpu) {
			continue;
		}

		power_vote_cpu_on(cpu);
	}
}
#endif
