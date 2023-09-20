// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <platform_irq.h>

#include "gicv3.h"

irq_t
platform_irq_max(void)
{
	return gicv3_irq_max();
}

error_t
platform_irq_check(irq_t irq)
{
	return gicv3_irq_check(irq);
}

bool
platform_irq_is_percpu(irq_t irq)
{
	return gicv3_irq_is_percpu(irq);
}

void
platform_irq_enable_shared(irq_t irq)
{
	gicv3_irq_enable_shared(irq);
}

void
platform_irq_enable_local(irq_t irq)
{
	gicv3_irq_enable_local(irq);
}

void
platform_irq_disable_shared(irq_t irq)
{
	gicv3_irq_disable_shared(irq);
}

void
platform_irq_disable_local(irq_t irq)
{
	gicv3_irq_disable_local(irq);
}

void
platform_irq_enable_percpu(irq_t irq, cpu_index_t cpu)
{
	gicv3_irq_enable_percpu(irq, cpu);
}

void
platform_irq_disable_percpu(irq_t irq, cpu_index_t cpu)
{
	gicv3_irq_disable_percpu(irq, cpu);
}

void
platform_irq_disable_local_nowait(irq_t irq)
{
	gicv3_irq_disable_local_nowait(irq);
}

irq_result_t
platform_irq_acknowledge(void)
{
	irq_result_t ret;

	ret = gicv3_irq_acknowledge();

	return ret;
}

void
platform_irq_priority_drop(irq_t irq)
{
	gicv3_irq_priority_drop(irq);
}

void
platform_irq_deactivate(irq_t irq)
{
	gicv3_irq_deactivate(irq);
}

void
platform_irq_deactivate_percpu(irq_t irq, cpu_index_t cpu)
{
	gicv3_irq_deactivate_percpu(irq, cpu);
}

irq_trigger_result_t
platform_irq_set_mode_percpu(irq_t irq, irq_trigger_t trigger, cpu_index_t cpu)
{
	return gicv3_irq_set_trigger_percpu(irq, trigger, cpu);
}
