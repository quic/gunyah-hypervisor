// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <irq.h>
#include <object.h>
#include <panic.h>
#include <preempt.h>

#include <asm/barrier.h>

#include "event_handlers.h"
#include "platform_timer.h"
#include "platform_timer_consts.h"

#if !defined(IRQ_NULL)
#include <partition.h>
#include <partition_alloc.h>

#include <events/platform.h>
#endif

#if !defined(IRQ_NULL)
static hwirq_t *hyp_timer_hwirq;
#endif

static void
platform_timer_enable_and_unmask()
{
	CNT_CTL_t cnthp_ctl;

	CNT_CTL_init(&cnthp_ctl);
	CNT_CTL_set_ENABLE(&cnthp_ctl, true);
	CNT_CTL_set_IMASK(&cnthp_ctl, false);
	register_CNTHP_CTL_EL2_write_ordered(cnthp_ctl, &asm_ordering);
}

void
platform_timer_cancel_timeout()
{
	CNT_CTL_t cnthp_ctl;

	CNT_CTL_init(&cnthp_ctl);
	CNT_CTL_set_ENABLE(&cnthp_ctl, false);
	CNT_CTL_set_IMASK(&cnthp_ctl, true);
	register_CNTHP_CTL_EL2_write_ordered(cnthp_ctl, &asm_ordering);
	__asm__ volatile("isb" : "+m"(asm_ordering));
}

uint32_t
platform_timer_get_frequency()
{
	return PLATFORM_ARCH_TIMER_FREQ;
}

uint64_t
platform_timer_get_current_ticks()
{
	// This register read below is allowed to occur speculatively at any
	// time after the most recent context sync event. If caller the wants
	// it to actually reflect the exact current time, it must execute an
	// ordered ISB before calling this function.
	CNTPCT_EL0_t cntpct =
		register_CNTPCT_EL0_read_volatile_ordered(&asm_ordering);

	return CNTPCT_EL0_get_CountValue(&cntpct);
}

uint64_t
platform_timer_get_timeout()
{
	CNT_CVAL_t cnthp_cval =
		register_CNTHP_CVAL_EL2_read_volatile_ordered(&asm_ordering);

	return CNT_CVAL_get_CompareValue(&cnthp_cval);
}

void
platform_timer_set_timeout(ticks_t timeout)
{
	assert_preempt_disabled();

	register_CNTHP_CVAL_EL2_write_ordered(CNT_CVAL_cast(timeout),
					      &asm_ordering);
	platform_timer_enable_and_unmask();
	__asm__ volatile("isb" : "+m"(asm_ordering));
}

ticks_t
platform_convert_ns_to_ticks(nanoseconds_t ns)
{
	__uint128_t ticks = ((__uint128_t)ns * PLATFORM_TIMER_FREQ_SCALE) >> 64;
	ticks		  = ticks << PLATFORM_TIMER_NS_SHIFT;

	return (ticks_t)ticks;
}

nanoseconds_t
platform_convert_ticks_to_ns(ticks_t ticks)
{
	__uint128_t ns = ((__uint128_t)ticks * PLATFORM_TIMER_NS_SCALE) >> 64;
	ns	       = ns << PLATFORM_TIMER_FREQ_SHIFT;

	return (nanoseconds_t)ns;
}

void
platform_timer_handle_boot_cpu_cold_init()
{
	CNTFRQ_EL0_t cntfrq = register_CNTFRQ_EL0_read();
	assert(CNTFRQ_EL0_get_ClockFrequency(&cntfrq) ==
	       PLATFORM_ARCH_TIMER_FREQ);

#if !defined(IRQ_NULL)
	if (hyp_timer_hwirq != NULL) {
		irq_enable_local(hyp_timer_hwirq);
	}
#endif
}

#if !defined(IRQ_NULL)
void
platform_timer_handle_boot_hypervisor_start()
{
	// Create the hyp arch timer IRQ
	hwirq_create_t params = {
		.irq	= PLATFORM_HYP_ARCH_TIMER_IRQ,
		.action = HWIRQ_ACTION_HYP_TIMER,
	};

	hwirq_ptr_result_t ret =
		partition_allocate_hwirq(partition_get_private(), params);

	if (ret.e != OK) {
		panic("Failed to create Hyp Timer IRQ");
	}

	if (object_activate_hwirq(ret.r) != OK) {
		panic("Failed to activate Hyp Timer IRQ");
	}

	hyp_timer_hwirq = ret.r;

	irq_enable_local(hyp_timer_hwirq);
}

bool
platform_timer_handle_irq_received(void)
{
	trigger_platform_timer_expiry_event();

	return true;
}

void
platform_timer_ndelay(nanoseconds_t duration)
{
	ticks_t cur_ticks      = platform_timer_get_current_ticks();
	ticks_t duration_ticks = platform_convert_ns_to_ticks(duration);
	ticks_t target_ticks   = cur_ticks + duration_ticks;

	// NOTE: assume we don't have overflow case since it covers huge range.
	// And assumes the timer is always enabled/configured correctly.
	while (platform_timer_get_current_ticks() < target_ticks)
		;
}
#endif
