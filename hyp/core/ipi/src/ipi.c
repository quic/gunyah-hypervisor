// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <limits.h>

#include <atomic.h>
#include <compiler.h>
#include <cpulocal.h>
#include <idle.h>
#include <ipi.h>
#include <platform_ipi.h>
#include <platform_timer.h>
#include <preempt.h>
#include <scheduler.h>
#include <thread.h>
#include <util.h>

#include <events/ipi.h>
#include <events/scheduler.h>

#include <asm/barrier.h>
#include <asm/event.h>
#include <asm/interrupt.h>
#include <asm/prefetch.h>

#include "event_handlers.h"

// Set this to 1 to disable the fast idle optimisation for debugging
#define IPI_DEBUG_NO_FAST_IDLE 0

#define REGISTER_BITS (sizeof(register_t) * CHAR_BIT)

// We enable the fast wakeup support by default if asm_event_wait() can sleep
// (as it will busy-wait otherwise) and preemption is enabled. We can possibly
// do it without preemption if asm_event_wait() is woken by pending disabled
// interrupts, but that's not the case on ARMv8.
//
// If interrupts are handled by a VM, we need to be able to ask the VM to send
// an IPI for us. This is not currently implemented, so we force fast wakeups in
// such configurations even though they will block pending interrupts.
#if (!ASM_EVENT_WAIT_IS_NOOP && !defined(PREEMPT_NULL) &&                      \
     !IPI_DEBUG_NO_FAST_IDLE) ||                                               \
	defined(IPI_FORCE_FAST_WAKEUP_HACK)
#define IPI_FAST_WAKEUP 1
#else
#define IPI_FAST_WAKEUP 0
#endif

#if IPI_FAST_WAKEUP
#define IPI_WAITING_IN_IDLE util_bit(REGISTER_BITS - 1U)
static_assert(((size_t)IPI_REASON__MAX + 1U) < (REGISTER_BITS - 1U),
	      "IPI reasons must fit in one word, with a free bit");
#else
static_assert(((size_t)IPI_REASON__MAX + 1U) < REGISTER_BITS,
	      "IPI reasons must fit in one word");
#endif

CPULOCAL_DECLARE_STATIC(ipi_pending_t, ipi_pending);

void
ipi_others_relaxed(ipi_reason_t ipi)
{
	assert(ipi <= IPI_REASON__MAX);
	const register_t  ipi_bit  = util_bit(ipi);
	const cpu_index_t this_cpu = cpulocal_get_index();

	for (cpu_index_t i = 0U; cpulocal_index_valid(i); i++) {
		if (i == this_cpu) {
			continue;
		}
		(void)atomic_fetch_or_explicit(
			&CPULOCAL_BY_INDEX(ipi_pending, i).bits, ipi_bit,
			memory_order_relaxed);
	}
	atomic_thread_fence(memory_order_release);
	asm_event_wake_updated();
}

void
ipi_others(ipi_reason_t ipi)
{
	ipi_others_relaxed(ipi);
#if PLATFORM_IPI_LINES > ENUM_IPI_REASON_MAX_VALUE
	platform_ipi_others(ipi);
#else
	platform_ipi_others();
#endif
}

void
ipi_others_idle(ipi_reason_t ipi)
{
#if IPI_FAST_WAKEUP
	ipi_others_relaxed(ipi);
#else
	ipi_others(ipi);
#endif
}

static bool
ipi_one_and_check_wakeup_needed(ipi_reason_t ipi, cpu_index_t cpu)
{
	assert(ipi <= IPI_REASON__MAX);
	const register_t ipi_bit = util_bit(ipi);

	assert(cpulocal_index_valid(cpu));

	register_t old_val = atomic_fetch_or_explicit(
		&CPULOCAL_BY_INDEX(ipi_pending, cpu).bits, ipi_bit,
		memory_order_release);
	asm_event_wake_updated();

#if IPI_FAST_WAKEUP
	return (old_val & IPI_WAITING_IN_IDLE) == 0U;
#else
	(void)old_val;
	return true;
#endif
}

void
ipi_one(ipi_reason_t ipi, cpu_index_t cpu)
{
	if (ipi_one_and_check_wakeup_needed(ipi, cpu)) {
#if PLATFORM_IPI_LINES > ENUM_IPI_REASON_MAX_VALUE
		platform_ipi_one(ipi, cpu);
#else
		platform_ipi_one(cpu);
#endif
	}
}

void
ipi_one_relaxed(ipi_reason_t ipi, cpu_index_t cpu)
{
	(void)ipi_one_and_check_wakeup_needed(ipi, cpu);
}

void
ipi_one_idle(ipi_reason_t ipi, cpu_index_t cpu)
{
#if IPI_FAST_WAKEUP
	ipi_one_relaxed(ipi, cpu);
#else
	ipi_one(ipi, cpu);
#endif
}

bool
ipi_clear_relaxed(ipi_reason_t ipi)
{
	assert(ipi <= IPI_REASON__MAX);

	const register_t ipi_bit = util_bit(ipi);

	register_t old_val = atomic_fetch_and_explicit(
		&CPULOCAL(ipi_pending).bits, ~ipi_bit, memory_order_acquire);

	return ((old_val & ipi_bit) != 0U);
}

bool
ipi_clear(ipi_reason_t ipi)
{
#if PLATFORM_IPI_LINES > ENUM_IPI_REASON_MAX_VALUE
	platform_ipi_clear(ipi);
#endif
	return ipi_clear_relaxed(ipi);
}

#if IPI_FAST_WAKEUP || (PLATFORM_IPI_LINES <= ENUM_IPI_REASON_MAX_VALUE)
static bool
ipi_handle_pending(register_t pending) REQUIRE_PREEMPT_DISABLED
{
	bool reschedule = false;

	while (pending != 0U) {
		index_t bit = REGISTER_BITS - 1U - compiler_clz(pending);
		pending &= ~util_bit(bit);
		if (bit <= (index_t)IPI_REASON__MAX) {
			ipi_reason_t ipi = (ipi_reason_t)bit;
			if (trigger_ipi_received_event(ipi)) {
				reschedule = true;
			}
		}
	}

	return reschedule;
}
#endif

#if PLATFORM_IPI_LINES > ENUM_IPI_REASON_MAX_VALUE
bool
ipi_handle_platform_ipi(ipi_reason_t ipi)
{
	if (ipi_clear_relaxed(ipi) && trigger_ipi_received_event(ipi)) {
		// We can't reschedule immediately as that might leave other
		// IRQs unhandled, so defer the reschedule.
		//
		// This may trigger a local reschedule relaxed IPI, even if that
		// is the IPI we just tried to handle. That is OK; since it is
		// relaxed, we will pick it up before returning to userspace or
		// going idle.
		scheduler_trigger();
	}

	return true;
}
#else
bool
ipi_handle_platform_ipi(void)
{
	register_t pending = atomic_exchange_explicit(
		&CPULOCAL(ipi_pending).bits, 0U, memory_order_acquire);
	if (ipi_handle_pending(pending)) {
		scheduler_trigger();
	}

	return true;
}
#endif

bool
ipi_handle_relaxed(void)
{
	assert_preempt_disabled();
	bool reschedule = false;

	_Atomic register_t *local_pending = &CPULOCAL(ipi_pending).bits;
	prefetch_store_keep(local_pending);
	register_t pending = atomic_load_relaxed(local_pending);
	while (compiler_unexpected(pending != 0U)) {
		ipi_reason_t ipi = (ipi_reason_t)(REGISTER_BITS - 1U -
						  compiler_clz(pending));
		if (ipi_clear_relaxed(ipi) && trigger_ipi_received_event(ipi)) {
			reschedule = true;
		}
		pending = atomic_load_relaxed(local_pending);
	}

	return reschedule;
}

void
ipi_handle_thread_exit_to_user(thread_entry_reason_t reason)
{
	// Relaxed IPIs are handled directly by the IRQ module for interrupts.
	if (reason != THREAD_ENTRY_REASON_INTERRUPT) {
		if (ipi_handle_relaxed()) {
			scheduler_schedule();
		}
	}
}

idle_state_t
ipi_handle_idle_yield(bool in_idle_thread)
{
	_Atomic register_t *local_pending = &CPULOCAL(ipi_pending).bits;

	prefetch_store_keep(local_pending);
#if IPI_FAST_WAKEUP
	bool	   must_schedule;
	register_t pending;
	do {
		// Mark ourselves as waiting in idle.
		atomic_fetch_or_explicit(local_pending, IPI_WAITING_IN_IDLE,
					 memory_order_relaxed);

		// Sleep until there is at least one event to handle or a
		// preemption clears IPI_WAITING_IN_IDLE.
		//
		// We must enable interrupts while waiting, because there is no
		// guarantee that asm_event_wait() will be woken by pending
		// interrupts. The ARM implementation of it, a WFE instruction,
		// is not woken. This means that preempt_interrupt_dispatch
		// needs to check the preempt disable count, and avoid context
		// switching if it is nonzero!
		asm_interrupt_enable_release(&local_pending);
		pending = asm_event_load_before_wait(local_pending);
		while (pending == IPI_WAITING_IN_IDLE) {
			asm_event_wait(local_pending);
			pending = asm_event_load_before_wait(local_pending);
		}
		asm_interrupt_disable_acquire(&local_pending);

		// Fetch and clear the events to handle; also clear the
		// IPI_WAITING_IN_IDLE bit if it is still set.
		pending = atomic_exchange_explicit(local_pending, 0U,
						   memory_order_acquire);

		// Handle the pending events, checking if a reschedule is
		// required.
		must_schedule =
			ipi_handle_pending(pending & ~IPI_WAITING_IN_IDLE);

		// Exit the loop if we must reschedule, we were preempted,
		// or we weren't triggered by the idle thread.
	} while (in_idle_thread && !must_schedule &&
		 ((pending & IPI_WAITING_IN_IDLE) != 0U));

	// Return and ensure we don't continue to WFI.
	return must_schedule ? IDLE_STATE_RESCHEDULE : IDLE_STATE_WAKEUP;
#else
	(void)in_idle_thread;
	return ipi_handle_relaxed() ? IDLE_STATE_RESCHEDULE : IDLE_STATE_IDLE;
#endif
}

error_t
ipi_handle_power_cpu_suspend(void)
{
	assert_preempt_disabled();

	bool reschedule = ipi_handle_relaxed();
	if (reschedule) {
		scheduler_trigger();
	}

	// Abort the suspend if we need to reschedule
	return reschedule ? ERROR_BUSY : OK;
}

#if !defined(PREEMPT_NULL)
bool
ipi_handle_preempt_interrupt(void)
{
#if IPI_FAST_WAKEUP
	// Clear the waiting-in-idle flag, to force idle_yield to exit.
	atomic_fetch_and_explicit(&CPULOCAL(ipi_pending).bits,
				  ~IPI_WAITING_IN_IDLE, memory_order_relaxed);
	// Note that IPIs are always handled by the caller after this event
	// completes, regardless of its result.
#endif
	return false;
}
#endif

void
ipi_handle_scheduler_stop(void)
{
	ipi_others(IPI_REASON_ABORT_STOP);

	// Delay approx 1ms to allow other cores to complete saving state.
	// We don't wait for acknowledgement since they may be unresponsive.
	uint32_t freq = platform_timer_get_frequency();

	uint64_t now = platform_timer_get_current_ticks();
	uint64_t end = now + (freq / 1024);

	while (now < end) {
		asm_yield();
		now = platform_timer_get_current_ticks();
	}
}
