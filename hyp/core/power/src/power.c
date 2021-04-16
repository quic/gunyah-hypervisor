// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <cpulocal.h>
#include <power.h>
#include <spinlock.h>
#include <util.h>

#include <events/power.h>

#include "event_handlers.h"

CPULOCAL_DECLARE(cpu_power_state_t, cpu_power_state);

static spinlock_t	      power_system_lock;
static register_t	      power_system_online_cpus;
static platform_power_state_t power_system_suspend_state;

void
power_handle_boot_cold_init(cpu_index_t boot_cpu)
{
	CPULOCAL_BY_INDEX(cpu_power_state, boot_cpu) = CPU_POWER_STATE_BOOT;
	spinlock_init(&power_system_lock);
}

void
power_handle_boot_cpu_cold_init(cpu_index_t cpu)
{
	spinlock_acquire(&power_system_lock);
	power_system_online_cpus |= util_bit(cpu);
	spinlock_release(&power_system_lock);
}

void
power_handle_boot_cpu_warm_init(void)
{
#if PLATFORM_MAX_CORES > 1
	cpu_power_state_t state = CPULOCAL(cpu_power_state);

	assert(state != CPU_POWER_STATE_ONLINE);

	CPULOCAL(cpu_power_state) = CPU_POWER_STATE_ONLINE;

	if (state == CPU_POWER_STATE_OFF) {
		trigger_power_cpu_online_event();
	}
#endif
}

void
power_handle_power_cpu_offline(void)
{
	CPULOCAL(cpu_power_state) = CPU_POWER_STATE_OFFLINE;
}

error_t
power_handle_power_cpu_suspend(platform_power_state_t state)
{
	error_t	   err	   = OK;
	register_t cpu_bit = util_bit(cpulocal_get_index());

	spinlock_acquire(&power_system_lock);
	power_system_online_cpus &= ~cpu_bit;
	if (power_system_online_cpus == 0U) {
		power_system_suspend_state = state;
		err = trigger_power_system_suspend_event(state);
		if (err != OK) {
			power_system_online_cpus |= cpu_bit;
		}
	}
	spinlock_release(&power_system_lock);

	if (err == OK) {
		CPULOCAL(cpu_power_state) = CPU_POWER_STATE_SUSPEND;
	}

	return err;
}

void
power_handle_power_cpu_resume(void)
{
	register_t cpu_bit = util_bit(cpulocal_get_index());

	CPULOCAL(cpu_power_state) = CPU_POWER_STATE_ONLINE;

	spinlock_acquire(&power_system_lock);
	if (power_system_online_cpus == 0U) {
		trigger_power_system_resume_event(power_system_suspend_state);
	}
	power_system_online_cpus |= cpu_bit;
	spinlock_release(&power_system_lock);
}
