// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <bitmap.h>
#include <panic.h>
#include <platform_cpu.h>
#include <platform_security.h>
#include <smccc_platform.h>

#include "event_handlers.h"

bool
platform_security_state_debug_disabled(void)
{
	return false;
}

uint32_t
platform_cpu_stack_size(void)
{
	return 0;
}

bool
smccc_handle_smc_platform_call(register_t args[7], bool is_hvc)
	EXCLUDE_PREEMPT_DISABLED
{
	(void)is_hvc;
	args[0] = (register_t)SMCCC_UNKNOWN_FUNCTION64;
	return true;
}

// Overrides the weak imlementation in core_id.c
core_id_t
platform_cpu_get_coreid(MIDR_EL1_t midr)
{
	(void)midr;
	return CORE_ID_QEMU;
}

#if !defined(UNIT_TESTS)
static _Atomic BITMAP_DECLARE(PLATFORM_MAX_CORES, hlos_vm_cpus);

bool
soc_qemu_handle_vcpu_activate_thread(thread_t		*thread,
				     vcpu_option_flags_t options)
{
	bool ret = true;

	assert(thread != NULL);
	assert(thread->kind == THREAD_KIND_VCPU);

	if (vcpu_option_flags_get_hlos_vm(&options)) {
		bool already_set = bitmap_atomic_test_and_set(
			hlos_vm_cpus, thread->scheduler_affinity,
			memory_order_relaxed);
		if (already_set) {
			ret = false;
			goto out;
		}

		// Validated, set the flag in the thread
		vcpu_option_flags_set_hlos_vm(&thread->vcpu_options, true);
	}

out:
	return ret;
}
#endif
