// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypversion.h>

#include <boot.h>
#include <compiler.h>
#include <log.h>
#include <thread_init.h>
#include <trace.h>
#include <util.h>

#include <events/boot.h>

#include "boot_init.h"
#include "event_handlers.h"

#define STR(x)	#x
#define XSTR(x) STR(x)

const char hypervisor_version[] = XSTR(HYP_CONF_STR) "-" XSTR(HYP_GIT_VERSION)
#if defined(QUALITY)
	" " XSTR(QUALITY)
#endif
	;
const char hypervisor_build_date[] = HYP_BUILD_DATE;

noreturn void
boot_cold_init(cpu_index_t cpu)
{
	// We can't trace yet because the CPU index in the thread is possibly
	// wrong (if cpu is nonzero), but the cpulocal handler for
	// boot_cpu_cold_init will do it for us.
	LOG(ERROR, WARN, "Hypervisor cold boot, version: {:s} ({:s})",
	    (register_t)hypervisor_version, (register_t)hypervisor_build_date);

	trigger_boot_cpu_early_init_event();
	trigger_boot_cold_init_event(cpu);
	trigger_boot_cpu_cold_init_event(cpu);
	TRACE(DEBUG, INFO, "boot_cpu_warm_init");
	trigger_boot_cpu_warm_init_event();
	TRACE(DEBUG, INFO, "boot_hypervisor_start");
	trigger_boot_hypervisor_start_event();
	TRACE(DEBUG, INFO, "boot_cpu_start");
	trigger_boot_cpu_start_event();
	TRACE(DEBUG, INFO, "entering idle");
	thread_boot_set_idle();
}

#if defined(VERBOSE) && VERBOSE
#define STACK_GUARD_BYTE 0xb8
#define STACK_GUARD_SIZE 256
#include <string.h>

#include <panic.h>

extern char aarch64_boot_stack[];
#endif

void
boot_handle_boot_cold_init(void)
{
#if defined(VERBOSE) && VERBOSE
	// Add a red-zone to the boot stack
	memset(&aarch64_boot_stack, STACK_GUARD_BYTE, STACK_GUARD_SIZE);
#endif
}

void
boot_handle_idle_start(void)
{
#if defined(VERBOSE) && VERBOSE
	char *stack_bottom = (char *)&aarch64_boot_stack;
	// Check red-zone in the boot stack
	for (index_t i = 0; i < STACK_GUARD_SIZE; i++) {
		if (stack_bottom[i] != STACK_GUARD_BYTE) {
			panic("boot stack overflow!");
		}
	}
#endif
}

noreturn void
boot_secondary_init(cpu_index_t cpu)
{
	// We can't trace yet because the CPU index in the thread is invalid,
	// but the cpulocal handler for boot_cpu_cold_init setup it up for us.
	LOG(ERROR, INFO, "secondary cpu ({:d}) cold boot", (register_t)cpu);

	trigger_boot_cpu_early_init_event();
	trigger_boot_cpu_cold_init_event(cpu);
	TRACE_LOCAL(DEBUG, INFO, "boot_cpu_warm_init");
	trigger_boot_cpu_warm_init_event();
	TRACE_LOCAL(DEBUG, INFO, "boot_cpu_start");
	trigger_boot_cpu_start_event();
	TRACE_LOCAL(DEBUG, INFO, "entering idle");
	thread_boot_set_idle();
}

// Warm (second or later) power-on of any CPU.
noreturn void
boot_warm_init(void)
{
	trigger_boot_cpu_early_init_event();
	TRACE_LOCAL(DEBUG, INFO, "boot_cpu_warm_init");
	trigger_boot_cpu_warm_init_event();
	TRACE_LOCAL(DEBUG, INFO, "boot_cpu_start");
	trigger_boot_cpu_start_event();
	TRACE_LOCAL(DEBUG, INFO, "entering idle");
	thread_boot_set_idle();
}

error_t
boot_add_free_range(paddr_t base, size_t size, void *arg)
{
	error_t		 ret	     = OK;
	boot_env_data_t *env_data    = (boot_env_data_t *)arg;
	bool		 first_entry = true;

	if ((size == 0U) && (util_add_overflows(base, size - 1))) {
		ret = ERROR_ARGUMENT_SIZE;
		goto error;
	}

	index_t index = env_data->free_ranges_count;

	if (index != 0U) {
		index--;
		first_entry = false;
	}

	assert((env_data->free_ranges[index].base +
		env_data->free_ranges[index].size) <= base);

	// Check if the address range to be added is contiguous with the last
	// range added. If so, append range. If not, add in next index.
	if ((env_data->free_ranges[index].base +
	     env_data->free_ranges[index].size) == base) {
		if (util_add_overflows(env_data->free_ranges[index].base,
				       env_data->free_ranges[index].size +
					       size - 1)) {
			ret = ERROR_ARGUMENT_SIZE;
			goto error;
		} else {
			env_data->free_ranges[index].size += size;
		}
	} else {
		if (first_entry == false) {
			index++;
		}
		if (index >= BOOT_ENV_RANGES_NUM) {
			LOG(ERROR, WARN, "env_data: no more free ranges");
		} else {
			env_data->free_ranges[index].base = base;
			env_data->free_ranges[index].size = size;
			env_data->free_ranges_count++;
		}
	}

error:
	return ret;
}
