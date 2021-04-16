// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <hypregisters.h>

#include <irq.h>

#include <asm/barrier.h>

#include "idle_arch.h"

bool
idle_arch_wait(void)
{
	bool must_reschedule = false;

	__asm__ volatile("dsb ish; wfi; isb" : "+m"(asm_ordering));

	ISR_EL1_t isr = register_ISR_EL1_read_volatile_ordered(&asm_ordering);
	if (ISR_EL1_get_I(&isr)) {
		must_reschedule = irq_interrupt_dispatch();
	}

	return must_reschedule;
}
