// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <cpulocal.h>
#include <irq.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <preempt.h>
#include <trace.h>

#include <events/platform.h>

#include <asm/barrier.h>
#include <asm/sysregs.h>
#include <asm/system_registers.h>

#include "event_handlers.h"
#include "platform_pmu.h"

static hwirq_t *pmu_hwirq;
CPULOCAL_DECLARE_STATIC(bool, pmu_irq_active);

void
platform_pmu_handle_boot_cpu_cold_init()
{
	// Disable all the interrupts at the startup
	sysreg64_write(PMINTENCLR_EL1, ~0U);
	CPULOCAL(pmu_irq_active) = false;

	if (pmu_hwirq != NULL) {
		irq_enable_local(pmu_hwirq);
	}
}

void
platform_pmu_handle_boot_hypervisor_start()
{
	// Create the PMU IRQ
	hwirq_create_t params = {
		.irq	= PLATFORM_PMU_IRQ,
		.action = HWIRQ_ACTION_PMU,
	};

	hwirq_ptr_result_t ret =
		partition_allocate_hwirq(partition_get_private(), params);

	if (ret.e != OK) {
		panic("Failed to create PMU IRQ");
	}

	if (object_activate_hwirq(ret.r) != OK) {
		panic("Failed to activate PMU IRQ");
	}

	pmu_hwirq = ret.r;

	irq_enable_local(pmu_hwirq);
}

bool
platform_pmu_is_hw_irq_pending()
{
	uint64_t pmovsset, pmintenset;

	sysreg64_read_ordered(PMINTENSET_EL1, pmintenset, asm_ordering);
	sysreg64_read_ordered(PMOVSSET_EL0, pmovsset, asm_ordering);

	return ((pmovsset & pmintenset) != 0);
}

void
platform_pmu_hw_irq_deactivate()
{
	if (CPULOCAL(pmu_irq_active)) {
		CPULOCAL(pmu_irq_active) = false;
		irq_deactivate(pmu_hwirq);
	}
}

error_t
arm_pmu_handle_power_cpu_suspend()
{
	platform_pmu_hw_irq_deactivate();

	return OK;
}

bool
platform_pmu_handle_irq_received(void)
{
	bool deactivate = true;

	if (platform_pmu_is_hw_irq_pending()) {
		CPULOCAL(pmu_irq_active) = true;
		trigger_platform_pmu_counter_overflow_event();

		// Leave the IRQ active until the guest has cleared the
		// corresponding overflow flag.
		deactivate = false;
	} else {
		TRACE(DEBUG, INFO, "Spurious PMU IRQ");
	}

	return deactivate;
}
