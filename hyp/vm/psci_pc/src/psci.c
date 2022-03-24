// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>
#include <hyprights.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <cpulocal.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <idle.h>
#include <ipi.h>
#include <irq.h>
#include <list.h>
#include <log.h>
#include <object.h>
#include <panic.h>
#include <partition_alloc.h>
#include <platform_cpu.h>
#include <platform_psci.h>
#include <preempt.h>
#include <psci.h>
#include <rcu.h>
#include <scheduler.h>
#include <spinlock.h>
#include <task_queue.h>
#include <thread.h>
#include <timer_queue.h>
#include <trace.h>
#include <trace_helpers.h>
#include <vcpu.h>
#include <vic.h>
#include <virq.h>
#include <vpm.h>

#include <events/power.h>
#include <events/psci.h>
#include <events/vpm.h>

#include "event_handlers.h"
#include "psci_arch.h"
#include "psci_pm_list.h"

// In this psci simple version, no affinity levels are supported

CPULOCAL_DECLARE_STATIC(_Atomic count_t, vpm_active_vcpus);
static _Atomic register_t vpm_active_pcpus_bitmap;

extern list_t partition_list;

// Set to 1 to boot enable the PSCI tracepoints
#if defined(VERBOSE_TRACE) && VERBOSE_TRACE
#define DEBUG_PSCI_TRACES 1
#else
#define DEBUG_PSCI_TRACES 0
#endif

void
psci_pc_handle_boot_cold_init(void)
{
#if !defined(NDEBUG) && DEBUG_PSCI_TRACES
	register_t flags = 0U;
	TRACE_SET_CLASS(flags, PSCI);
	trace_set_class_flags(flags);
#endif

	psci_pm_list_init();

#if !defined(PSCI_SET_SUSPEND_MODE_NOT_SUPPORTED) ||                           \
	!PSCI_SET_SUSPEND_MODE_NOT_SUPPORTED
	error_t ret = platform_psci_set_suspend_mode(PSCI_MODE_OSI);
	assert(ret == OK);
#endif
}

static bool
psci_set_vpm_active_pcpus_bit(index_t bit)
{
	register_t old = atomic_fetch_or_explicit(
		&vpm_active_pcpus_bitmap, util_bit(bit), memory_order_relaxed);

	return old == 0U;
}

// Returns true if bitmap becomes zero after clearing bit
static bool
psci_clear_vpm_active_pcpus_bit(index_t bit)
{
	register_t cleared_bit = ~util_bit(bit);

	register_t old = atomic_fetch_and_explicit(
		&vpm_active_pcpus_bitmap, cleared_bit, memory_order_relaxed);

	return (old & cleared_bit) == 0U;
}

void
psci_pc_handle_boot_cpu_cold_init(cpu_index_t cpu)
{
	atomic_store_relaxed(&CPULOCAL_BY_INDEX(vpm_active_vcpus, cpu), 0U);
	(void)psci_set_vpm_active_pcpus_bit(cpu);
}

static void
psci_vpm_active_vcpus_get(cpu_index_t cpu, thread_t *vcpu)
{
	assert(cpulocal_index_valid(cpu));
	assert(vcpu->psci_inactive_count != 0U);

	vcpu->psci_inactive_count--;
	if (vcpu->psci_inactive_count == 0U) {
		(void)atomic_fetch_add_explicit(
			&CPULOCAL_BY_INDEX(vpm_active_vcpus, cpu), 1U,
			memory_order_relaxed);
	}
}

static void
psci_vpm_active_vcpus_put(cpu_index_t cpu, thread_t *vcpu)
{
	assert(cpulocal_index_valid(cpu));

	vcpu->psci_inactive_count++;
	if (vcpu->psci_inactive_count == 1U) {
		count_t old = atomic_fetch_sub_explicit(
			&CPULOCAL_BY_INDEX(vpm_active_vcpus, cpu), 1U,
			memory_order_relaxed);
		assert(old != 0U);
	}
}

static bool
psci_vpm_active_vcpus_is_zero(cpu_index_t cpu)
{
	assert(cpulocal_index_valid(cpu));

	return atomic_load_relaxed(&CPULOCAL_BY_INDEX(vpm_active_vcpus, cpu)) ==
	       0;
}

bool
psci_pc_handle_vcpu_activate_thread(thread_t *thread)
{
	bool ret = true;

	assert(thread != NULL);
	assert(thread->kind == THREAD_KIND_VCPU);

	scheduler_lock(thread);

	// Determine the initial inactive count for the VCPU.
	thread->psci_inactive_count = 0U;

	if (scheduler_is_blocked(thread, SCHEDULER_BLOCK_VCPU_OFF)) {
		// VCPU is inactive because it is powered off.
		thread->psci_inactive_count++;
	}
	// VCPU can't be suspended or in WFI yet.
	assert(!scheduler_is_blocked(thread, SCHEDULER_BLOCK_VCPU_SUSPEND));
	assert(!scheduler_is_blocked(thread, SCHEDULER_BLOCK_VCPU_WFI));

	cpu_index_t cpu = scheduler_get_affinity(thread);
	if (cpulocal_index_valid(cpu)) {
		if (thread->psci_group != NULL) {
			psci_pm_list_insert(cpu, thread);
		}
	} else {
		// VCPU is inactive because it has no valid affinity.
		thread->psci_inactive_count++;
	}

	// If the VCPU is initially active, make sure the CPU stays awake.
	if (thread->psci_inactive_count == 0U) {
		assert(cpulocal_index_valid(cpu));
		(void)atomic_fetch_add_explicit(
			&CPULOCAL_BY_INDEX(vpm_active_vcpus, cpu), 1U,
			memory_order_relaxed);
	}

	scheduler_unlock(thread);

	return ret;
}

void
psci_pc_handle_scheduler_affinity_changed(thread_t   *thread,
					  cpu_index_t prev_cpu, bool *need_sync)
{
	object_state_t state = atomic_load_acquire(&thread->header.state);

	if ((thread->kind == THREAD_KIND_VCPU) &&
	    (state == OBJECT_STATE_ACTIVE)) {
		if (cpulocal_index_valid(prev_cpu)) {
			if (thread->psci_group != NULL) {
				psci_pm_list_delete(prev_cpu, thread);
			}
			if ((thread->psci_mode == VPM_MODE_PSCI) ||
			    (thread->psci_mode == VPM_MODE_IDLE)) {
				psci_vpm_active_vcpus_put(prev_cpu, thread);
			}
		}

		thread->psci_migrate = true;
		*need_sync	     = true;
	}
}

void
psci_pc_handle_scheduler_affinity_changed_sync(thread_t	*thread,
					       cpu_index_t next_cpu)
{
	if (thread->psci_migrate) {
		assert(thread->kind == THREAD_KIND_VCPU);

		if (cpulocal_index_valid(next_cpu)) {
			if (thread->psci_group != NULL) {
				psci_pm_list_insert(next_cpu, thread);
			}
			if ((thread->psci_mode == VPM_MODE_PSCI) ||
			    (thread->psci_mode == VPM_MODE_IDLE)) {
				scheduler_lock(thread);
				psci_vpm_active_vcpus_get(next_cpu, thread);
				scheduler_unlock(thread);
			}
		}

		thread->psci_migrate = false;
	}
}

static thread_t *
psci_get_thread_by_mpidr(psci_mpidr_t mpidr)
{
	thread_t *current = thread_get_self();
	thread_t *result  = NULL;

	if (psci_mpidr_is_equal(psci_thread_get_mpidr(current), mpidr)) {
		result = object_get_thread_additional(current);
	} else {
		vpm_group_t *psci_group = current->psci_group;
		if (psci_group != NULL) {
			cpu_index_result_t index =
				platform_cpu_mpidr_to_index(mpidr);
			if (index.e == OK) {
				// RCU protects psci_group->psci_cpus[i]
				rcu_read_start();

				result = atomic_load_consume(
					&psci_group->psci_cpus[index.r]);
				if ((result != NULL) &&
				    !object_get_thread_safe(result)) {
					result = NULL;
				}

				rcu_read_finish();
			}
		}
	}

	if (result != NULL) {
		assert(psci_mpidr_is_equal(psci_thread_get_mpidr(result),
					   mpidr));
	}

	return result;
}

static bool
psci_is_hlos(void)
{
	thread_t *vcpu = thread_get_self();

	return vcpu_option_flags_get_hlos_vm(&vcpu->vcpu_options);
}

bool
psci_version(uint32_t *ret0)
{
	*ret0 = PSCI_VERSION;
	return true;
}

static psci_ret_t
psci_suspend(psci_suspend_powerstate_t suspend_state,
	     paddr_t entry_point_address, register_t context_id)
	EXCLUDE_PREEMPT_DISABLED
{
	psci_ret_t ret	   = PSCI_RET_SUCCESS;
	thread_t	 *current = thread_get_self();

	assert(current->psci_group != NULL);

	current->psci_suspend_state = suspend_state;

	error_t err = vcpu_suspend();
	if (err == ERROR_DENIED) {
		TRACE(PSCI, PSCI_PSTATE_VALIDATION,
		      "psci_suspend: DENIED - pstate {:#x} - VM {:d}",
		      psci_suspend_powerstate_raw(suspend_state),
		      current->addrspace->vmid);
		ret = PSCI_RET_DENIED;
		goto out;
	} else if (err == ERROR_ARGUMENT_INVALID) {
		TRACE(PSCI, PSCI_PSTATE_VALIDATION,
		      "psci suspend: INVALID_PARAMETERS - pstate {:#x} - VM {:d}",
		      psci_suspend_powerstate_raw(suspend_state),
		      current->addrspace->vmid);
		ret = PSCI_RET_INVALID_PARAMETERS;
		goto out;
	} else if (err == ERROR_BUSY) {
		// It did not suspend due to a pending interrupt
		ret = PSCI_RET_SUCCESS;
		goto out;
	} else if (err == OK) {
		ret = PSCI_RET_SUCCESS;
	} else {
		panic("unhandled vcpu_suspend error");
	}

	// Warm reset VCPU unconditionally from the psci mode to make the
	// cpuidle stats work
	if ((psci_suspend_powerstate_get_StateType(&suspend_state) ==
	     PSCI_SUSPEND_POWERSTATE_TYPE_POWERDOWN)) {
		vcpu_warm_reset(entry_point_address, context_id);
	}

out:
	return ret;
}

static psci_ret_t
psci_cpu_suspend(psci_suspend_powerstate_t suspend_state,
		 paddr_t entry_point_address, register_t context_id)
	EXCLUDE_PREEMPT_DISABLED
{
	psci_ret_t ret;
	thread_t	 *current = thread_get_self();

	if (current->psci_group == NULL) {
		ret = PSCI_RET_NOT_SUPPORTED;
		goto out;
	}

	ret = psci_suspend(suspend_state, entry_point_address, context_id);

out:
	return ret;
}

uint32_t
psci_cpu_suspend_32_features(void)
{
	uint32_t ret;

	// Only Platform-coordinated mode, extended StateID
	ret = 2U;

	return ret;
}

uint32_t
psci_cpu_suspend_64_features(void)
{
	return psci_cpu_suspend_32_features();
}

bool
psci_cpu_suspend_32(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t *ret0)
{
	psci_ret_t ret = psci_cpu_suspend(psci_suspend_powerstate_cast(arg1),
					  arg2, arg3);
	*ret0	       = (uint32_t)ret;
	return true;
}

bool
psci_cpu_suspend_64(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t *ret0)
{
	psci_ret_t ret = psci_cpu_suspend(
		psci_suspend_powerstate_cast((uint32_t)arg1), arg2, arg3);
	*ret0 = (uint64_t)ret;
	return true;
}

// Same as psci_cpu_suspend, but it sets the suspend state to the deepest
// cpu-level.
static psci_ret_t
psci_cpu_default_suspend(paddr_t entry_point_address, register_t context_id)
	EXCLUDE_PREEMPT_DISABLED
{
	psci_ret_t  ret;
	thread_t	 *current = thread_get_self();
	cpu_index_t cpu	    = cpulocal_get_index();

	if (current->psci_group == NULL) {
		ret = PSCI_RET_NOT_SUPPORTED;
		goto out;
	}

	psci_suspend_powerstate_t pstate = psci_suspend_powerstate_default();
	psci_suspend_powerstate_stateid_t stateid =
		platform_psci_deepest_cpu_level_stateid(cpu);
	psci_suspend_powerstate_set_StateID(&pstate, stateid);
	psci_suspend_powerstate_set_StateType(
		&pstate, PSCI_SUSPEND_POWERSTATE_TYPE_POWERDOWN);

	ret = psci_suspend(pstate, entry_point_address, context_id);
out:
	return ret;
}

bool
psci_cpu_default_suspend_32(uint32_t arg1, uint32_t arg2, uint32_t *ret0)
{
	psci_ret_t ret = psci_cpu_default_suspend(arg1, arg2);
	*ret0	       = (uint32_t)ret;
	return true;
}

bool
psci_cpu_default_suspend_64(uint64_t arg1, uint64_t arg2, uint64_t *ret0)
{
	psci_ret_t ret = psci_cpu_default_suspend(arg1, arg2);
	*ret0	       = (uint64_t)ret;
	return true;
}

static psci_ret_t
psci_switch_suspend_mode(psci_mode_t new_mode)
{
	psci_ret_t ret = PSCI_RET_SUCCESS;

	thread_t	 *thread	= thread_get_self();
	vpm_group_t *psci_group = thread->psci_group;
	cpu_index_t  vcpu_id	= thread->psci_index;

	assert(psci_group != NULL);

	rcu_read_start();

	vpm_group_suspend_state_t vm_state =
		atomic_load_acquire(&psci_group->psci_vm_suspend_state);

	cpu_index_t	 cpu_index = 0U;
	psci_cpu_state_t cpu_state = 0U;

	vpm_vcpus_state_foreach (
		cpu_index, cpu_state,
		vpm_group_suspend_state_get_vcpus_state(&vm_state)) {
		if (vcpu_id == cpu_index) {
			continue;
		}
		if (((new_mode == PSCI_MODE_OSI) &&
		     !platform_psci_is_cpu_poweroff(cpu_state) &&
		     !platform_psci_is_cpu_active(cpu_state)) ||
		    ((new_mode == PSCI_MODE_PC) &&
		     !platform_psci_is_cpu_poweroff(cpu_state))) {
			ret = PSCI_RET_DENIED;
			goto out;
		}
	}

	// If conditions met, change suspend mode of vpm group
	psci_group->psci_mode = new_mode;

out:
	rcu_read_finish();

	return ret;
}

bool
psci_set_suspend_mode(uint32_t arg1, uint32_t *ret0)
{
	psci_ret_t ret;

	thread_t *current = thread_get_self();

	if (arg1 == current->psci_group->psci_mode) {
		ret = PSCI_RET_SUCCESS;
		goto out;
	}

	switch (arg1) {
	case PSCI_MODE_PC:
	case PSCI_MODE_OSI:
		ret = psci_switch_suspend_mode(arg1);
		if (ret == PSCI_RET_DENIED) {
			TRACE(PSCI, INFO,
			      "psci_set_suspend_mode - DENIED - VM {:d}",
			      current->addrspace->vmid);
		}

		if (ret == PSCI_RET_DENIED) {
			TRACE(PSCI, INFO,
			      "psci_set_suspend_mode - DENIED - VM {:d}",
			      current->addrspace->vmid);
		}

		break;
	default:
		ret = PSCI_RET_INVALID_PARAMETERS;
		TRACE(PSCI, INFO,
		      "psci_set_suspend_mode - INVALID_PARAMETERS - VM {:d}",
		      current->addrspace->vmid);
		break;
	}

out:
	*ret0 = (uint32_t)ret;
	return true;
}

bool
psci_cpu_off(uint32_t *ret0)
{
	thread_t	 *current	= thread_get_self();
	cpu_index_t  cpu	= cpulocal_get_index();
	vpm_group_t *psci_group = current->psci_group;

	if (psci_group != NULL) {
		psci_suspend_powerstate_t pstate =
			psci_suspend_powerstate_default();
		psci_suspend_powerstate_set_StateType(
			&pstate, PSCI_SUSPEND_POWERSTATE_TYPE_POWERDOWN);
		psci_suspend_powerstate_stateid_t stateid;

		stateid = platform_psci_deepest_cpu_level_stateid(cpu);

		psci_suspend_powerstate_set_StateID(&pstate, stateid);
		current->psci_suspend_state = pstate;

		error_t ret = vcpu_poweroff();
		// If we return, the only reason should be DENIED
		assert(ret == ERROR_DENIED);
	}
	*ret0 = (uint32_t)PSCI_RET_DENIED;
	return true;
}

static psci_ret_t
psci_cpu_on(psci_mpidr_t cpu, paddr_t entry_point_address,
	    register_t context_id)
{
	thread_t	 *thread = psci_get_thread_by_mpidr(cpu);
	psci_ret_t ret;

	if (thread == NULL) {
		ret = PSCI_RET_INVALID_PARAMETERS;
	} else {
		bool reschedule = false;

		scheduler_lock(thread);
		if (scheduler_is_blocked(thread, SCHEDULER_BLOCK_VCPU_OFF)) {
			reschedule = vcpu_poweron(thread, entry_point_address,
						  context_id);
			ret	   = PSCI_RET_SUCCESS;
		} else {
			ret = PSCI_RET_ALREADY_ON;
		}
		scheduler_unlock(thread);
		object_put_thread(thread);

		if (reschedule) {
			scheduler_schedule();
		}
	}

	return ret;
}

bool
psci_cpu_on_32(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t *ret0)
{
	psci_ret_t ret = psci_cpu_on(psci_mpidr_cast(arg1), arg2, arg3);
	*ret0	       = (uint32_t)ret;
	return true;
}

bool
psci_cpu_on_64(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t *ret0)
{
	psci_ret_t ret = psci_cpu_on(psci_mpidr_cast(arg1), arg2, arg3);
	*ret0	       = (uint64_t)ret;
	return true;
}

static psci_ret_t
psci_affinity_info(psci_mpidr_t affinity, uint32_t lowest_affinity_level)
{
	psci_ret_t ret;

	thread_t *thread = psci_get_thread_by_mpidr(affinity);
	if (thread == NULL) {
		ret = PSCI_RET_INVALID_PARAMETERS;
	} else if (lowest_affinity_level != 0U) {
		// lowest_affinity_level is legacy from PSCI 0.2; we are
		// allowed to fail if it is nonzero (which indicates a
		// query of the cluster-level state).
		ret = PSCI_RET_INVALID_PARAMETERS;
	} else {
		// Don't bother locking, this is inherently racy anyway
		psci_ret_affinity_info_t info =
			scheduler_is_blocked(thread, SCHEDULER_BLOCK_VCPU_OFF)
				? PSCI_RET_AFFINITY_INFO_OFF
				: PSCI_RET_AFFINITY_INFO_ON;
		ret = (psci_ret_t)info;
	}

	if (thread != NULL) {
		object_put_thread(thread);
	}

	return ret;
}

bool
psci_affinity_info_32(uint32_t arg1, uint32_t arg2, uint32_t *ret0)
{
	psci_ret_t ret = psci_affinity_info(psci_mpidr_cast(arg1), arg2);
	*ret0	       = (uint32_t)ret;
	return true;
}

bool
psci_affinity_info_64(uint64_t arg1, uint64_t arg2, uint64_t *ret0)
{
	psci_ret_t ret =
		psci_affinity_info(psci_mpidr_cast(arg1), (uint32_t)arg2);
	*ret0 = (uint64_t)ret;
	return true;
}

bool
psci_system_off(void)
{
	if (!psci_is_hlos()) {
		return (uint32_t)PSCI_RET_NOT_SUPPORTED;
	}

	trigger_power_system_off_event();

	panic("system_off event returned");
}

bool
psci_system_reset(void)
{
	if (!psci_is_hlos()) {
		return (uint32_t)PSCI_RET_NOT_SUPPORTED;
	}

	error_t ret = OK;
	(void)trigger_power_system_reset_event(PSCI_REQUEST_SYSTEM_RESET, 0U,
					       &ret);
	panic("system_reset event returned");
}

static uint32_t
psci_system_reset2(uint64_t reset_type, uint64_t cookie)
{
	uint32_t ret;

	if (psci_is_hlos()) {
		error_t error = OK;
		trigger_power_system_reset_event(reset_type, cookie, &error);

		if (error == ERROR_ARGUMENT_INVALID) {
			ret = (uint32_t)PSCI_RET_INVALID_PARAMETERS;
		} else {
			ret = (uint32_t)PSCI_RET_NOT_SUPPORTED;
		}
	} else {
		ret = (uint32_t)PSCI_RET_NOT_SUPPORTED;
	}

	return ret;
}

bool
psci_system_reset2_32(uint32_t arg1, uint32_t arg2, uint32_t *ret0)
{
	*ret0 = psci_system_reset2(arg1, arg2);
	return true;
}

bool
psci_system_reset2_64(uint64_t arg1, uint64_t arg2, uint64_t *ret0)
{
	*ret0 = psci_system_reset2(
		(uint32_t)arg1 | PSCI_REQUEST_SYSTEM_RESET2_64, arg2);
	return true;
}

bool
psci_features(uint32_t arg1, uint32_t *ret0)
{
	smccc_function_id_t fn_id = smccc_function_id_cast(arg1);
	uint32_t	    ret	  = SMCCC_UNKNOWN_FUNCTION32;
	smccc_function_t    fn	  = smccc_function_id_get_function(&fn_id);

	// We need to handle discovery of SMCCC_VERSION here

	if ((smccc_function_id_get_interface_id(&fn_id) ==
	     SMCCC_INTERFACE_ID_STANDARD) &&
	    smccc_function_id_get_is_fast(&fn_id) &&
	    (smccc_function_id_get_res0(&fn_id) == 0U)) {
		ret = smccc_function_id_get_is_smc64(&fn_id)
			      ? trigger_psci_features64_event(fn)
			      : trigger_psci_features32_event(fn);
	}

	*ret0 = ret;
	return true;
}

error_t
psci_pc_handle_object_create_thread(thread_create_t thread_create)
{
	thread_t *thread = thread_create.thread;
	assert(thread != NULL);

	// Default thread to be of IDLE mode
	thread->psci_mode = VPM_MODE_IDLE;

	psci_suspend_powerstate_t pstate = psci_suspend_powerstate_default();
	psci_suspend_powerstate_stateid_t stateid =
		platform_psci_deepest_cpu_level_stateid(
			thread->scheduler_affinity);
	psci_suspend_powerstate_set_StateID(&pstate, stateid);
	psci_suspend_powerstate_set_StateType(
		&pstate, PSCI_SUSPEND_POWERSTATE_TYPE_POWERDOWN);

	// Initialize to deepest possible state
	thread->psci_suspend_state = pstate;

	return OK;
}

error_t
psci_pc_handle_object_activate_thread(thread_t *thread)
{
	error_t err;
	if (thread->kind != THREAD_KIND_VCPU) {
		err = OK;
	} else if (thread->psci_group == NULL) {
		err = OK;
	} else {
		assert(scheduler_is_blocked(thread, SCHEDULER_BLOCK_VCPU_OFF));

		cpu_index_t index    = thread->psci_index;
		thread_t	 *tmp_null = NULL;

		if (!cpulocal_index_valid(index)) {
			err = ERROR_OBJECT_CONFIG;
		} else if (!atomic_compare_exchange_strong_explicit(
				   &thread->psci_group->psci_cpus[index],
				   &tmp_null, thread, memory_order_release,
				   memory_order_relaxed)) {
			err = ERROR_DENIED;
		} else {
			psci_thread_set_mpidr_by_index(thread, index);
			err = OK;
		}
	}
	return err;
}

void
psci_pc_handle_object_deactivate_thread(thread_t *thread)
{
	assert(thread != NULL);

	if (thread->psci_group != NULL) {
		thread_t	 *tmp	  = thread;
		cpu_index_t index = thread->psci_index;

		atomic_compare_exchange_strong_explicit(
			&thread->psci_group->psci_cpus[index], &tmp, NULL,
			memory_order_relaxed, memory_order_relaxed);
		object_put_vpm_group(thread->psci_group);

		scheduler_lock(thread);
		psci_pm_list_delete(scheduler_get_affinity(thread), thread);
		scheduler_unlock(thread);
	}
}

error_t
vpm_bind_virq(vpm_group_t *vpm_group, vic_t *vic, virq_t virq)
{
	error_t ret = OK;

	assert(vpm_group != NULL);
	assert(vic != NULL);

	ret = vic_bind_shared(&vpm_group->psci_system_suspend_virq, vic, virq,
			      VIRQ_TRIGGER_VPM_GROUP);

	return ret;
}

void
vpm_unbind_virq(vpm_group_t *vpm_group)
{
	assert(vpm_group != NULL);

	vic_unbind_sync(&vpm_group->psci_system_suspend_virq);
}

static bool
vcpus_state_is_any_awake(vpm_group_suspend_state_t vm_state)
{
	bool		 vcpu_awake = false;
	cpu_index_t	 cpu_index  = 0U;
	psci_cpu_state_t cpu_state  = 0U;

	vpm_vcpus_state_foreach (
		cpu_index, cpu_state,
		vpm_group_suspend_state_get_vcpus_state(&vm_state)) {
		if (platform_psci_is_cpu_active(cpu_state)) {
			vcpu_awake = true;
			goto out;
		}
	}

out:
	return vcpu_awake;
}

static void
vcpus_state_set(vpm_group_suspend_state_t *vm_state, cpu_index_t cpu,
		psci_cpu_state_t cpu_state)
{
	uint32_t vcpus_state =
		vpm_group_suspend_state_get_vcpus_state(vm_state);

	vcpus_state &= (uint32_t) ~(PSCI_VCPUS_STATE_PER_VCPU_MASK
				    << (cpu * PSCI_VCPUS_STATE_PER_VCPU_BITS));
	vcpus_state |=
		(uint32_t)(cpu_state << (cpu * PSCI_VCPUS_STATE_PER_VCPU_BITS));

	vpm_group_suspend_state_set_vcpus_state(vm_state, vcpus_state);
}

static void
vcpus_state_clear(vpm_group_suspend_state_t *vm_state, cpu_index_t cpu)
{
	uint32_t vcpus_state =
		vpm_group_suspend_state_get_vcpus_state(vm_state);

	vcpus_state &= (uint32_t) ~(PSCI_VCPUS_STATE_PER_VCPU_MASK
				    << (cpu * PSCI_VCPUS_STATE_PER_VCPU_BITS));

	vpm_group_suspend_state_set_vcpus_state(vm_state, vcpus_state);
}

error_t
psci_pc_handle_object_activate_vpm_group(vpm_group_t *pg)
{
	spinlock_init(&pg->psci_lock);
	pg->psci_system_suspend_count = 0;
	task_queue_init(&pg->psci_virq_task, TASK_QUEUE_CLASS_VPM_GROUP_VIRQ);

	// Default psci mode to be platfom-coordinated
	pg->psci_mode = PSCI_MODE_PC;

	// Initialize vcpus states of the vpm to the deepest suspend state
	psci_cpu_state_t cpu_state =
		platform_psci_deepest_cpu_state(cpulocal_get_index());
	vpm_group_suspend_state_t vm_state = vpm_group_suspend_state_default();

	for (cpu_index_t i = 0;
	     i < (PSCI_VCPUS_STATE_BITS / PSCI_VCPUS_STATE_PER_VCPU_BITS);
	     i++) {
		vcpus_state_set(&vm_state, i, cpu_state);
	}

	atomic_store_release(&pg->psci_vm_suspend_state, vm_state);

	return OK;
}

void
psci_pc_handle_object_deactivate_vpm_group(vpm_group_t *pg)
{
	for (cpu_index_t i = 0; cpulocal_index_valid(i); i++) {
		assert(atomic_load_relaxed(&pg->psci_cpus[i]) == NULL);
	}

	ipi_one_idle(IPI_REASON_IDLE, cpulocal_get_index());
}

error_t
vpm_attach(vpm_group_t *pg, thread_t *thread, index_t index)
{
	assert(pg != NULL);
	assert(thread != NULL);
	assert(atomic_load_relaxed(&thread->header.state) == OBJECT_STATE_INIT);
	assert(atomic_load_relaxed(&pg->header.state) == OBJECT_STATE_ACTIVE);

	error_t err;

	if (!cpulocal_index_valid((cpu_index_t)index)) {
		err = ERROR_ARGUMENT_INVALID;
	} else if (thread->kind != THREAD_KIND_VCPU) {
		err = ERROR_ARGUMENT_INVALID;
	} else {
		if (thread->psci_group != NULL) {
			object_put_vpm_group(thread->psci_group);
		}

		thread->psci_group = object_get_vpm_group_additional(pg);
		thread->psci_index = (cpu_index_t)index;
		trace_ids_set_vcpu_index(&thread->trace_ids,
					 (cpu_index_t)index);

		thread->psci_mode = VPM_MODE_PSCI;

		err = OK;
	}

	return err;
}

error_t
psci_pc_handle_task_queue_execute(task_queue_entry_t *task_entry)
{
	assert(task_entry != NULL);
	vpm_group_t *vpm_group =
		vpm_group_container_of_psci_virq_task(task_entry);

	virq_assert(&vpm_group->psci_system_suspend_virq, true);
	object_put_vpm_group(vpm_group);

	return OK;
}

vpm_state_t
vpm_get_state(vpm_group_t *vpm_group)
{
	vpm_state_t vpm_state = VPM_STATE_NO_STATE;

	vpm_group_suspend_state_t vm_state =
		atomic_load_acquire(&vpm_group->psci_vm_suspend_state);

	if (vcpus_state_is_any_awake(vm_state)) {
		vpm_state = VPM_STATE_RUNNING;
	} else {
		vpm_state = VPM_STATE_CPUS_SUSPENDED;
	}

	return vpm_state;
}

static void
psci_vcpu_wakeup(thread_t *thread, cpu_index_t target_cpu)
{
	if (cpulocal_index_valid(target_cpu)) {
		psci_vpm_active_vcpus_get(target_cpu, thread);
	}

	if (thread->psci_mode != VPM_MODE_PSCI) {
		goto out;
	}

	assert(thread->psci_group != NULL);

	vpm_group_t *vpm_group = thread->psci_group;
	cpu_index_t  vcpu_id   = thread->psci_index;

	thread->psci_suspend_state = psci_suspend_powerstate_default();

	vpm_group_suspend_state_t old_state =
		atomic_load_relaxed(&vpm_group->psci_vm_suspend_state);
	vpm_group_suspend_state_t new_state;

	do {
		new_state = old_state;

		vcpus_state_clear(&new_state, vcpu_id);

	} while (!atomic_compare_exchange_strong_explicit(
		&vpm_group->psci_vm_suspend_state, &old_state, new_state,
		memory_order_relaxed, memory_order_relaxed));

out:
	// Nothing to do for non PSCI threads
	return;
}

static error_t
psci_vcpu_suspend(thread_t *current)
{
	if (current->psci_mode != VPM_MODE_PSCI) {
		goto out;
	}

	assert(current->psci_group != NULL);

	// Decrement refcount of the PCPU
	psci_vpm_active_vcpus_put(cpulocal_get_index(), current);

	vpm_group_t	    *vpm_group = current->psci_group;
	cpu_index_t	 vcpu_id   = current->psci_index;
	psci_cpu_state_t cpu_state =
		platform_psci_get_cpu_state(current->psci_suspend_state);

	vpm_group_suspend_state_t new_state;
	vpm_group_suspend_state_t old_state;

	// Set vcpus_state of corresponding cpu.
	old_state = atomic_load_relaxed(&vpm_group->psci_vm_suspend_state);

	do {
		new_state = old_state;
		vcpus_state_set(&new_state, vcpu_id, cpu_state);

	} while (!atomic_compare_exchange_strong_explicit(
		&vpm_group->psci_vm_suspend_state, &old_state, new_state,
		memory_order_relaxed, memory_order_relaxed));

out:
	return OK;
}

error_t
psci_pc_handle_vcpu_suspend(void)
{
	error_t	  ret	  = OK;
	thread_t *current = thread_get_self();

	ret = psci_vcpu_suspend(current);
	if (ret == OK) {
		TRACE(PSCI, PSCI_VPM_VCPU_SUSPEND,
		      "psci vcpu suspend: {:#x} - VM {:d}", (uintptr_t)current,
		      current->addrspace->vmid);
	}

	return ret;
}

void
psci_pc_unwind_vcpu_suspend(void)
{
	thread_t *current = thread_get_self();

	psci_vcpu_wakeup(current, cpulocal_get_index());
}

bool
psci_pc_handle_trapped_idle(void)
{
	thread_t *current = thread_get_self();
	bool	  handled = false;

	if (current->psci_mode == VPM_MODE_IDLE) {
		psci_vpm_active_vcpus_put(cpulocal_get_index(), current);
		error_t err = vcpu_suspend();
		if ((err != OK) && (err != ERROR_BUSY)) {
			panic("unhandled vcpu_suspend error (WFI)");
		}
		handled = true;
	}

	return handled;
}

void
psci_pc_handle_vcpu_resume(void)
{
	thread_t *vcpu = thread_get_self();

	TRACE(PSCI, PSCI_VPM_VCPU_RESUME,
	      "psci vcpu resume: {:#x} - VM {:d} - VCPU {:d}", (uintptr_t)vcpu,
	      vcpu->addrspace->vmid, vcpu->psci_index);

	psci_vcpu_wakeup(vcpu, cpulocal_get_index());
}

void
psci_pc_handle_vcpu_started(void)
{
	thread_t *current = thread_get_self();

	// If the VCPU has been warm-reset, it has already called
	// psci_vcpu_wakeup in the above vcpu_resume event handler.
	if (!current->vcpu_warm_reset) {
		TRACE(PSCI, PSCI_VPM_VCPU_RESUME,
		      "psci vcpu started: {:#x} - VM {:d}", (uintptr_t)current,
		      current->addrspace->vmid);

		scheduler_lock(current);
		psci_vcpu_wakeup(current, cpulocal_get_index());
		scheduler_unlock(current);
	}
}

void
psci_pc_handle_vcpu_wakeup(thread_t *vcpu)
{
	if (scheduler_is_blocked(vcpu, SCHEDULER_BLOCK_VCPU_SUSPEND)) {
		vcpu_resume(vcpu);
	}
}

void
psci_pc_handle_vcpu_wakeup_self(void)
{
	assert(!scheduler_is_blocked(thread_get_self(),
				     SCHEDULER_BLOCK_VCPU_SUSPEND));
}

bool
psci_pc_handle_vcpu_expects_wakeup(const thread_t *thread)
{
	return scheduler_is_blocked(thread, SCHEDULER_BLOCK_VCPU_SUSPEND);
}

void
psci_pc_handle_vcpu_poweron(thread_t *vcpu)
{
	if (vcpu->psci_group == NULL) {
		goto out;
	}

	(void)atomic_fetch_add_explicit(&vcpu->psci_group->psci_online_count,
					1U, memory_order_relaxed);

	cpu_index_t cpu = vcpu->scheduler_affinity;
	if (cpulocal_index_valid(cpu)) {
		if (platform_cpu_on(cpu) != OK) {
			// Note that already-on and on-pending results
			// from EL3 PSCI are treated as success.
			panic("Failed to power on secondary CPU");
		}
	}

out:
	return;
}

error_t
psci_pc_handle_vcpu_poweroff(thread_t *vcpu, bool force)
{
	error_t	     ret	= OK;
	vpm_group_t *psci_group = vcpu->psci_group;

	if (psci_group != NULL) {
		count_t online_cpus =
			atomic_load_relaxed(&psci_group->psci_online_count);
		do {
			assert(online_cpus > 0U);
			if (!force && (online_cpus == 1U)) {
				ret = ERROR_DENIED;
				goto out;
			}
		} while (!atomic_compare_exchange_weak_explicit(
			&psci_group->psci_online_count, &online_cpus,
			online_cpus - 1U, memory_order_relaxed,
			memory_order_relaxed));

		ret = psci_vcpu_suspend(vcpu);
	}
out:
	return ret;
}

idle_state_t
psci_pc_handle_idle_yield(bool in_idle_thread)
{
	assert_preempt_disabled();

	idle_state_t idle_state = IDLE_STATE_IDLE;

	if (!in_idle_thread) {
		goto out;
	}

	cpu_index_t cpu = cpulocal_get_index();

	// Check if there is any vcpu running in this cpu
	if (!psci_vpm_active_vcpus_is_zero(cpu)) {
		goto out;
	}

	thread_t *vcpu	       = NULL;
	list_t   *psci_pm_list = psci_pm_list_get_self();
	assert(psci_pm_list != NULL);

	psci_suspend_powerstate_t pstate = psci_suspend_powerstate_default();
	psci_cpu_state_t cpu_state	 = platform_psci_deepest_cpu_state(cpu);

	// Iterate through affine VCPUs and get the shallowest cpu-level state
	rcu_read_start();
	list_foreach_container_consume (vcpu, psci_pm_list, thread,
					psci_pm_list_node) {
		psci_cpu_state_t cpu1 =
			platform_psci_get_cpu_state(vcpu->psci_suspend_state);
		cpu_state = platform_psci_shallowest_cpu_state(cpu_state, cpu1);
	}
	rcu_read_finish();

	// Do not go to suspend if shallowest cpu state is zero. This may happen
	// if a vcpu has started after doing the initial check of 'any vcpu
	// running in this cpu' and therefore has been added to the psci_pm_list
	// with a psci_suspend_state of 0.
	if (cpu_state == 0U) {
		goto out;
	}

	platform_psci_set_cpu_state(&pstate, cpu_state);

	if (platform_psci_is_cpu_poweroff(cpu_state)) {
		psci_suspend_powerstate_set_StateType(
			&pstate, PSCI_SUSPEND_POWERSTATE_TYPE_POWERDOWN);
	} else {
		psci_suspend_powerstate_set_StateType(
			&pstate,
			PSCI_SUSPEND_POWERSTATE_TYPE_STANDBY_OR_RETENTION);
	}

	bool last_cpu = false;

	if (psci_clear_vpm_active_pcpus_bit(cpu)) {
		last_cpu = true;
	}

	// Fence to prevent any power_cpu_suspend event handlers conditional on
	// last_cpu (especially the trigger of power_system_suspend) being
	// reordered before the psci_clear_vpm_active_pcpus_bit() above. This
	// matches the fence before the resume event below.
	atomic_thread_fence(memory_order_seq_cst);

	error_t suspend_result = trigger_power_cpu_suspend_event(
		pstate,
		psci_suspend_powerstate_get_StateType(&pstate) ==
			PSCI_SUSPEND_POWERSTATE_TYPE_POWERDOWN,
		last_cpu);
	if (suspend_result == OK) {
		bool_result_t ret;

		TRACE(PSCI, INFO, "psci power_cpu_suspend {:#x}",
		      psci_suspend_powerstate_raw(pstate));

		ret = platform_cpu_suspend(pstate);

		// Check if this is the first cpu to wake up
		bool first_cpu = psci_set_vpm_active_pcpus_bit(cpu);
		suspend_result = ret.e;

		// Fence to prevent any power_cpu_resume event handlers
		// conditional on first_cpu (especially the trigger of
		// power_system_resume) being reordered before the
		// psci_set_vpm_active_pcpus_bit() above. This matches the
		// fence before the suspend event above.
		atomic_thread_fence(memory_order_seq_cst);

		trigger_power_cpu_resume_event((ret.e == OK) && ret.r,
					       first_cpu);
		TRACE(PSCI, INFO,
		      "psci power_cpu_suspend wakeup; poweroff {:d} system_resume {:d} error {:d}",
		      ret.r, first_cpu, (register_t)ret.e);
	} else {
		TRACE(PSCI, INFO, "psci power_cpu_suspend failed: {:d}",
		      (unsigned int)suspend_result);
		(void)psci_set_vpm_active_pcpus_bit(cpu);
	}

	if (suspend_result == OK) {
		// Return from successful suspend. We were presumably woken by
		// an interrupt; handle it now and reschedule if required.
		idle_state = irq_interrupt_dispatch() ? IDLE_STATE_RESCHEDULE
						      : IDLE_STATE_WAKEUP;
	} else if (suspend_result == ERROR_BUSY) {
		// An interrupt will arrive soon, continue with idle.
	} else if (suspend_result != ERROR_DENIED) {
		TRACE_AND_LOG(ERROR, WARN, "ERROR: psci suspend error {:d}",
			      (register_t)suspend_result);
		panic("unhandled suspend error");
	} else {
		// suspend state was denied, re-run psci aggregation.
		idle_state = IDLE_STATE_WAKEUP;
	}

out:
	return idle_state;
}

#if defined(ROOTVM_IS_HLOS) && ROOTVM_IS_HLOS
void
psci_pc_handle_rootvm_init_early(partition_t *root_partition,
				 thread_t *root_thread, cspace_t *root_cspace,
				 boot_env_data_t *env_data)
{
	// Create the PSCI group for the root VM
	vpm_group_create_t     pg_params = { 0 };
	vpm_group_ptr_result_t pg_r =
		partition_allocate_vpm_group(root_partition, pg_params);
	if (pg_r.e != OK) {
		panic("Unable to create root VM's PSCI group");
	}

	if (object_activate_vpm_group(pg_r.r) != OK) {
		panic("Error activating root PSCI group");
	}

	// Create a master cap for the PSCI group
	object_ptr_t	optr  = { .vpm_group = pg_r.r };
	cap_id_result_t cid_r = cspace_create_master_cap(root_cspace, optr,
							 OBJECT_TYPE_VPM_GROUP);
	if (cid_r.e != OK) {
		panic("Unable to create cap to root VM's PSCI group");
	}
	env_data->psci_group = cid_r.r;

	// Attach the root VM's main VCPU to the group
	assert(root_thread->scheduler_affinity == cpulocal_get_index());
	if (vpm_attach(pg_r.r, root_thread, root_thread->scheduler_affinity) !=
	    OK) {
		panic("Unable to attach root thread to its PSCI group");
	}

	// Create new powered-off VCPUs for every other CPU
	for (cpu_index_t i = 0; cpulocal_index_valid(i); i++) {
		if (i == root_thread->scheduler_affinity) {
			env_data->psci_secondary_vcpus[i] = CSPACE_CAP_INVALID;
			continue;
		}

		thread_create_t thread_params = {
			.scheduler_affinity	  = i,
			.scheduler_affinity_valid = true,
			.kind			  = THREAD_KIND_VCPU,
		};

		thread_ptr_result_t thread_r = partition_allocate_thread(
			root_partition, thread_params);
		if (thread_r.e != OK) {
			panic("Unable to create root VM secondary VCPU");
		}

		vcpu_option_flags_t vcpu_options = vcpu_option_flags_default();
		vcpu_option_flags_set_hlos_vm(&vcpu_options, true);

		if (vcpu_configure(thread_r.r, vcpu_options) != OK) {
			panic("Error configuring secondary VCPU");
		}

		// Attach thread to root cspace
		if (cspace_attach_thread(root_cspace, thread_r.r) != OK) {
			panic("Error attaching cspace to secondary VCPU");
		}

		optr.thread = thread_r.r;
		cid_r	    = cspace_create_master_cap(root_cspace, optr,
						       OBJECT_TYPE_THREAD);
		if (cid_r.e != OK) {
			panic("Unable to create cap to root VM secondary VCPU");
		}
		env_data->psci_secondary_vcpus[i] = cid_r.r;

		if (vpm_attach(pg_r.r, thread_r.r, i) != OK) {
			panic("Unable to attach root VCPU to the PSCI group");
		}
	}
}

void
psci_pc_handle_rootvm_init_late(cspace_t	 *root_cspace,
				boot_env_data_t *env_data)
{
	// Activate the secondary VCPU objects
	for (cpu_index_t i = 0; cpulocal_index_valid(i); i++) {
		cap_id_t thread_cap = env_data->psci_secondary_vcpus[i];
		if (thread_cap == CSPACE_CAP_INVALID) {
			continue;
		}

		object_type_t	    type;
		object_ptr_result_t o = cspace_lookup_object_any(
			root_cspace, thread_cap,
			CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE, &type);

		if ((o.e != OK) || (type != OBJECT_TYPE_THREAD) ||
		    (object_activate_thread(o.r.thread) != OK)) {
			panic("Error activating secondary VCPU");
		}

		object_put(type, o.r);
	}
}
#endif
