// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <compiler.h>
#include <cpulocal.h>
#include <log.h>
#include <platform_features.h>
#include <preempt.h>
#include <rcu.h>
#include <scheduler.h>
#include <trace.h>
#include <vet.h>

#include "event_handlers.h"

asm_ordering_dummy_t vet_ordering;

static bool trace_disabled = false;

void
vet_handle_boot_cold_init(void)
{
	platform_cpu_features_t features = platform_get_cpu_features();

	trace_disabled = platform_cpu_features_get_trace_disable(&features);
	if (trace_disabled) {
		LOG(ERROR, INFO, "trace disabled");
	}
}

error_t
vet_handle_thread_context_switch_pre(void)
{
	thread_t *vcpu = thread_get_self();
	assert(vcpu != NULL);

	if (vcpu_option_flags_get_trace_allowed(&vcpu->vcpu_options)) {
		vet_update_trace_unit_status(vcpu);

		if (vcpu->vet_trace_unit_enabled) {
			vet_flush_trace(vcpu);
			vet_disable_trace();
			vet_save_trace_thread_context(vcpu);
		}

		vet_update_trace_buffer_status(vcpu);

		if (vcpu->vet_trace_buffer_enabled) {
			vet_flush_buffer(vcpu);
			vet_disable_buffer();
			vet_save_buffer_thread_context(vcpu);
		}
	}

	return OK;
}

void
vet_handle_thread_load_state(void)
{
	thread_t *vcpu = thread_get_self();
	assert(vcpu != NULL);

	if (vcpu_option_flags_get_trace_allowed(&vcpu->vcpu_options)) {
		if (vcpu->vet_trace_buffer_enabled) {
			vet_restore_buffer_thread_context(vcpu);
			vet_enable_buffer();
		}

		if (vcpu->vet_trace_unit_enabled) {
			vet_restore_trace_thread_context(vcpu);
			vet_enable_trace();
		}
	}
}

bool
vet_handle_vcpu_activate_thread(thread_t *thread, vcpu_option_flags_t options)
{
	bool ret;

	assert(thread->kind == THREAD_KIND_VCPU);

	bool hlos	   = vcpu_option_flags_get_hlos_vm(&options);
	bool trace_allowed = vcpu_option_flags_get_trace_allowed(&options);

	// TODO: currently we always give HLOS trace access.
	if (trace_allowed && trace_disabled) {
		// Not permitted
		ret = false;
	} else if (hlos && !trace_disabled) {
		// Give HLOS threads trace access
		vcpu_option_flags_set_trace_allowed(&thread->vcpu_options,
						    true);
		ret = true;
	} else if (!hlos && trace_allowed) {
		// Not supported
		ret = false;
	} else {
		ret = true;
	}

	return ret;
}

error_t
vet_handle_power_cpu_suspend(bool may_poweroff)
{
	assert_cpulocal_safe();
	rcu_read_start();

	thread_t *vcpu = scheduler_get_primary_vcpu(cpulocal_get_index());

	if (may_poweroff && (vcpu != NULL) && vcpu->vet_trace_buffer_enabled) {
		vet_save_buffer_power_context();
	}

	if ((vcpu != NULL) && vcpu->vet_trace_unit_enabled) {
		vet_save_trace_power_context(may_poweroff);
	}

	rcu_read_finish();
	return OK;
}

void
vet_unwind_power_cpu_suspend(void)
{
	assert_cpulocal_safe();
	rcu_read_start();

	thread_t *vcpu = scheduler_get_primary_vcpu(cpulocal_get_index());

	if ((vcpu != NULL) && vcpu->vet_trace_unit_enabled) {
		vet_restore_trace_power_context(false);
	}

	rcu_read_finish();
}

void
vet_handle_power_cpu_resume(bool was_poweroff)
{
	assert_cpulocal_safe();
	rcu_read_start();

	thread_t *vcpu = scheduler_get_primary_vcpu(cpulocal_get_index());

	if (was_poweroff && (vcpu != NULL) && vcpu->vet_trace_buffer_enabled) {
		vet_restore_buffer_power_context();
	}

	if ((vcpu != NULL) && vcpu->vet_trace_unit_enabled) {
		vet_restore_trace_power_context(was_poweroff);
	}

	rcu_read_finish();
}
