// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <limits.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <compiler.h>
#include <cpulocal.h>
#include <ipi.h>
#include <object.h>
#include <panic.h>
#include <platform_cpu.h>
#include <platform_psci.h>
#include <preempt.h>
#include <psci.h>
#include <rcu.h>
#include <scheduler.h>
#include <thread.h>
#include <trace.h>
#include <trace_helpers.h>
#include <util.h>
#include <vcpu.h>
#include <vgic.h>
#include <vic.h>
#include <virq.h>
#include <vpm.h>

#include <events/power.h>
#include <events/psci.h>

#include "event_handlers.h"
#include "psci_arch.h"
#include "psci_common.h"
#include "psci_pm_list.h"

CPULOCAL_DECLARE_STATIC(_Atomic count_t, vpm_active_vcpus);

// vpm_active_pcpus_bitmap could be made a count to avoid a bitmap.
#define REGISTER_BITS (sizeof(register_t) * (size_t)CHAR_BIT)
static_assert(PLATFORM_MAX_CORES <= REGISTER_BITS,
	      "PLATFORM_MAX_CORES > REGISTER_BITS");
static _Atomic register_t vpm_active_pcpus_bitmap;

void
psci_handle_boot_cold_init(void)
{
#if !defined(NDEBUG)
	register_t flags = 0U;
	TRACE_SET_CLASS(flags, PSCI);
	trace_set_class_flags(flags);
#endif

	psci_pm_list_init();
}

bool
psci_set_vpm_active_pcpus_bit(cpu_index_t bit)
{
	register_t old = atomic_fetch_or_explicit(
		&vpm_active_pcpus_bitmap, util_bit(bit), memory_order_relaxed);

	return old == 0U;
}

// Returns true if bitmap becomes zero after clearing bit
bool
psci_clear_vpm_active_pcpus_bit(cpu_index_t bit)
{
	register_t cleared_bit = ~util_bit(bit);

	register_t old = atomic_fetch_and_explicit(
		&vpm_active_pcpus_bitmap, cleared_bit, memory_order_relaxed);

	return (old & cleared_bit) == 0U;
}

void
psci_handle_boot_cpu_cold_init(cpu_index_t cpu)
{
	atomic_store_relaxed(&CPULOCAL_BY_INDEX(vpm_active_vcpus, cpu), 0U);
	(void)psci_set_vpm_active_pcpus_bit(cpu);
}

void
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

void
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

bool
psci_vpm_active_vcpus_is_zero(cpu_index_t cpu)
{
	assert(cpulocal_index_valid(cpu));

	return atomic_load_relaxed(&CPULOCAL_BY_INDEX(vpm_active_vcpus, cpu)) ==
	       0;
}

bool
psci_handle_vcpu_activate_thread(thread_t *thread)
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
psci_handle_scheduler_affinity_changed(thread_t *thread, cpu_index_t prev_cpu,
				       cpu_index_t next_cpu, bool *need_sync)
{
	object_state_t state = atomic_load_acquire(&thread->header.state);

	if ((state == OBJECT_STATE_ACTIVE) &&
	    (thread->vpm_mode != VPM_MODE_NONE)) {
		if (cpulocal_index_valid(prev_cpu)) {
			if (thread->vpm_mode == VPM_MODE_PSCI) {
				psci_pm_list_delete(prev_cpu, thread);
			}
			psci_vpm_active_vcpus_put(prev_cpu, thread);
		}

		if (cpulocal_index_valid(next_cpu)) {
			psci_vpm_active_vcpus_get(next_cpu, thread);
			if (thread->vpm_mode == VPM_MODE_PSCI) {
				thread->psci_migrate = true;
				*need_sync	     = true;
			}
		}
	}
}

void
psci_handle_scheduler_affinity_changed_sync(thread_t   *thread,
					    cpu_index_t next_cpu)
{
	if (thread->psci_migrate) {
		assert(thread->kind == THREAD_KIND_VCPU);
		assert(thread->vpm_mode == VPM_MODE_PSCI);
		assert(cpulocal_index_valid(next_cpu));

		psci_pm_list_insert(next_cpu, thread);

		thread->psci_migrate = false;
	}
}

static bool
psci_mpidr_matches_thread(MPIDR_EL1_t a, psci_mpidr_t b)
{
	return (MPIDR_EL1_get_Aff0(&a) == psci_mpidr_get_Aff0(&b)) &&
	       (MPIDR_EL1_get_Aff1(&a) == psci_mpidr_get_Aff1(&b)) &&
	       (MPIDR_EL1_get_Aff2(&a) == psci_mpidr_get_Aff2(&b)) &&
	       (MPIDR_EL1_get_Aff3(&a) == psci_mpidr_get_Aff3(&b));
}

static MPIDR_EL1_t
psci_mpidr_to_cpu(psci_mpidr_t psci_mpidr)
{
	MPIDR_EL1_t mpidr = MPIDR_EL1_default();

	MPIDR_EL1_set_Aff0(&mpidr, psci_mpidr_get_Aff0(&psci_mpidr));
	MPIDR_EL1_set_Aff1(&mpidr, psci_mpidr_get_Aff1(&psci_mpidr));
	MPIDR_EL1_set_Aff2(&mpidr, psci_mpidr_get_Aff2(&psci_mpidr));
	MPIDR_EL1_set_Aff3(&mpidr, psci_mpidr_get_Aff3(&psci_mpidr));

	return mpidr;
}

static thread_t *
psci_get_thread_by_mpidr(psci_mpidr_t mpidr)
{
	thread_t    *current	= thread_get_self();
	thread_t    *result	= NULL;
	vpm_group_t *psci_group = current->psci_group;

	assert(psci_group != NULL);

	// This function is not performance-critical; it is only called during
	// PSCI_CPU_ON and PSCI_AFFINITY_INFO. A simple linear search of the VPM
	// group is good enough.
	rcu_read_start();
	for (index_t i = 0U; i < util_array_size(psci_group->psci_cpus); i++) {
		thread_t *thread =
			atomic_load_consume(&psci_group->psci_cpus[i]);
		if ((thread != NULL) &&
		    psci_mpidr_matches_thread(thread->vcpu_regs_mpidr_el1,
					      mpidr) &&
		    object_get_thread_safe(thread)) {
			result = thread;
			break;
		}
	}
	rcu_read_finish();

	return result;
}

bool
psci_version(uint32_t *ret0)
{
	bool	  handled;
	thread_t *current = thread_get_self();

	if (compiler_unexpected(current->psci_group == NULL)) {
		handled = false;
	} else {
		*ret0	= PSCI_VERSION;
		handled = true;
	}
	return handled;
}

psci_ret_t
psci_suspend(psci_suspend_powerstate_t suspend_state,
	     paddr_t entry_point_address, register_t context_id)
{
	psci_ret_t ret;
	thread_t  *current = thread_get_self();

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
	thread_t  *current = thread_get_self();

	// If the VCPU is participating in aggregation, we need to check with
	// platform code that the requested state is valid. Otherwise, all
	// requested states are accepted and treated equally.
	if (current->vpm_mode == VPM_MODE_PSCI) {
		assert(current->psci_group != NULL);

		// FIXME:
		cpulocal_begin();
		ret = platform_psci_suspend_state_validation(
			suspend_state, cpulocal_get_index(),
			current->psci_group->psci_mode);
		cpulocal_end();
		if (ret != PSCI_RET_SUCCESS) {
			TRACE(PSCI, PSCI_PSTATE_VALIDATION,
			      "psci_cpu_suspend: INVALID_PARAMETERS - pstate {:#x} - VM {:d}",
			      psci_suspend_powerstate_raw(suspend_state),
			      current->addrspace->vmid);
			goto out;
		}
	}

	ret = psci_suspend(suspend_state, entry_point_address, context_id);

out:
	return ret;
}

uint32_t
psci_cpu_suspend_32_features(void)
{
	return psci_cpu_suspend_features();
}

uint32_t
psci_cpu_suspend_64_features(void)
{
	return psci_cpu_suspend_features();
}

bool
psci_cpu_suspend_32(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t *ret0)
{
	bool	  handled;
	thread_t *current = thread_get_self();

	if (compiler_unexpected(current->psci_group == NULL)) {
		handled = false;
	} else {
		psci_ret_t ret = psci_cpu_suspend(
			psci_suspend_powerstate_cast(arg1), arg2, arg3);
		*ret0	= (uint32_t)ret;
		handled = true;
	}
	return handled;
}

bool
psci_cpu_suspend_64(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t *ret0)
{
	bool	  handled;
	thread_t *current = thread_get_self();

	if (compiler_unexpected(current->psci_group == NULL)) {
		handled = false;
	} else {
		psci_ret_t ret = psci_cpu_suspend(
			psci_suspend_powerstate_cast((uint32_t)arg1), arg2,
			arg3);
		*ret0	= (uint64_t)ret;
		handled = true;
	}
	return handled;
}

// Same as psci_cpu_suspend, but it sets the suspend state to the deepest
// cpu-level.
static psci_ret_t
psci_cpu_default_suspend(paddr_t entry_point_address, register_t context_id)
	EXCLUDE_PREEMPT_DISABLED
{
	psci_ret_t ret;

	psci_suspend_powerstate_t pstate = psci_suspend_powerstate_default();

	// FIXME:
	cpulocal_begin();
	psci_suspend_powerstate_stateid_t stateid =
		platform_psci_deepest_cpu_level_stateid(cpulocal_get_index());
	cpulocal_end();

	psci_suspend_powerstate_set_StateID(&pstate, stateid);
	psci_suspend_powerstate_set_StateType(
		&pstate, PSCI_SUSPEND_POWERSTATE_TYPE_POWERDOWN);

	ret = psci_suspend(pstate, entry_point_address, context_id);

	return ret;
}

bool
psci_cpu_default_suspend_32(uint32_t arg1, uint32_t arg2, uint32_t *ret0)
{
	bool	  handled;
	thread_t *current = thread_get_self();

	if (compiler_unexpected(current->psci_group == NULL)) {
		handled = false;
	} else {
		psci_ret_t ret = psci_cpu_default_suspend(arg1, arg2);
		*ret0	       = (uint32_t)ret;
		handled	       = true;
	}
	return handled;
}

bool
psci_cpu_default_suspend_64(uint64_t arg1, uint64_t arg2, uint64_t *ret0)
{
	bool	  handled;
	thread_t *current = thread_get_self();

	if (compiler_unexpected(current->psci_group == NULL)) {
		handled = false;
	} else {
		psci_ret_t ret = psci_cpu_default_suspend(arg1, arg2);
		*ret0	       = (uint64_t)ret;
		handled	       = true;
	}
	return handled;
}

bool
psci_cpu_off(uint32_t *ret0)
{
	bool	  handled;
	thread_t *current = thread_get_self();

	if (compiler_unexpected(current->psci_group == NULL)) {
		handled = false;
	} else {
		error_t ret = vcpu_poweroff(false, false);
		// If we return, the only reason should be DENIED
		assert(ret == ERROR_DENIED);
		*ret0	= (uint32_t)PSCI_RET_DENIED;
		handled = true;
	}
	return handled;
}

static psci_ret_t
psci_cpu_on(psci_mpidr_t cpu, paddr_t entry_point_address,
	    register_t context_id)
{
	thread_t  *thread = psci_get_thread_by_mpidr(cpu);
	psci_ret_t ret;

	if (compiler_unexpected(thread == NULL)) {
		thread_t *current = thread_get_self();
		vic_t	 *vic	  = vic_get_vic(current);
		if (vic == NULL) {
			ret = PSCI_RET_INVALID_PARAMETERS;
		} else {
			// Check whether MPIDR was valid or not. Note, we
			// currently use PLATFORM_MAX_CORES instead of a per
			// psci group
			MPIDR_EL1_t mpidr = psci_mpidr_to_cpu(cpu);

			const platform_mpidr_mapping_t *mpidr_mapping =
				vgic_get_mpidr_mapping(vic);

			bool valid = platform_cpu_map_mpidr_valid(mpidr_mapping,
								  mpidr);

			index_t index = platform_cpu_map_mpidr_to_index(
				mpidr_mapping, mpidr);

			if (!valid || (index > PLATFORM_MAX_CORES)) {
				ret = PSCI_RET_INVALID_PARAMETERS;
			} else {
				ret = PSCI_RET_INTERNAL_FAILURE;
			}
		}
	} else {
		bool reschedule = false;

		scheduler_lock(thread);
		if (vcpu_option_flags_get_pinned(&thread->vcpu_options) &&
		    !platform_cpu_exists(thread->scheduler_affinity)) {
			ret = PSCI_RET_INTERNAL_FAILURE;
			goto unlock;
		}

		if (scheduler_is_blocked(thread, SCHEDULER_BLOCK_VCPU_OFF)) {
			bool_result_t result = vcpu_poweron(
				thread, vmaddr_result_ok(entry_point_address),
				register_result_ok(context_id));
			reschedule = result.r;
			ret	   = (result.e == OK) ? PSCI_RET_SUCCESS
				     : (result.e == ERROR_FAILURE)
					     ? PSCI_RET_INTERNAL_FAILURE
				     : (result.e == ERROR_RETRY)
					     ? PSCI_RET_ALREADY_ON
					     : PSCI_RET_INVALID_PARAMETERS;
		} else {
			ret = PSCI_RET_ALREADY_ON;
		}
	unlock:
		scheduler_unlock(thread);
		object_put_thread(thread);

		if (reschedule) {
			(void)scheduler_schedule();
		}
	}

	return ret;
}

bool
psci_cpu_on_32(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t *ret0)
{
	bool	  handled;
	thread_t *current = thread_get_self();

	if (compiler_unexpected(current->psci_group == NULL)) {
		handled = false;
	} else {
		psci_ret_t ret = psci_cpu_on(psci_mpidr_cast(arg1), arg2, arg3);
		*ret0	       = (uint32_t)ret;
		handled	       = true;
	}
	return handled;
}

bool
psci_cpu_on_64(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t *ret0)
{
	bool	  handled;
	thread_t *current = thread_get_self();

	if (compiler_unexpected(current->psci_group == NULL)) {
		handled = false;
	} else {
		psci_ret_t ret = psci_cpu_on(psci_mpidr_cast(arg1), arg2, arg3);
		*ret0	       = (uint64_t)ret;
		handled	       = true;
	}
	return handled;
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
	bool	  handled;
	thread_t *current = thread_get_self();

	if (compiler_unexpected(current->psci_group == NULL)) {
		handled = false;
	} else {
		psci_ret_t ret =
			psci_affinity_info(psci_mpidr_cast(arg1), arg2);
		*ret0	= (uint32_t)ret;
		handled = true;
	}
	return handled;
}

bool
psci_affinity_info_64(uint64_t arg1, uint64_t arg2, uint64_t *ret0)
{
	bool	  handled;
	thread_t *current = thread_get_self();

	if (compiler_unexpected(current->psci_group == NULL)) {
		handled = false;
	} else {
		psci_ret_t ret = psci_affinity_info(psci_mpidr_cast(arg1),
						    (uint32_t)arg2);
		*ret0	       = (uint64_t)ret;
		handled	       = true;
	}
	return handled;
}

static noreturn void
psci_stop_all_vcpus(void)
{
	thread_t *current = thread_get_self();
	assert(current != NULL);
	assert(current->kind == THREAD_KIND_VCPU);

	vpm_group_t *psci_group = current->psci_group;
	if (psci_group != NULL) {
		for (index_t i = 0U; i < util_array_size(psci_group->psci_cpus);
		     i++) {
			thread_t *thread =
				atomic_load_consume(&psci_group->psci_cpus[i]);
			if ((thread != NULL) && (thread != current)) {
				error_t err = thread_kill(thread);
				if (err != OK) {
					panic("Unable to kill VCPU");
				}
			}
		}
	}

	// Force power off
	(void)vcpu_poweroff(false, true);
	// We should not be denied when force is true
	panic("vcpu_poweroff(force=true) returned");
}

bool
psci_system_off(void)
{
	bool	  handled;
	thread_t *current = thread_get_self();

	if (compiler_unexpected(current->psci_group == NULL)) {
		handled = false;
	} else {
		if (vcpu_option_flags_get_critical(&current->vcpu_options)) {
			// HLOS VM calls to this function are passed directly to
			// the firmware, to power off the physical device.
			trigger_power_system_off_event();
			panic("system_off event returned");
		}

		psci_stop_all_vcpus();
	}

	return handled;
}

bool
psci_system_reset(void)
{
	bool	  handled;
	thread_t *current = thread_get_self();

	if (compiler_unexpected(current->psci_group == NULL)) {
		handled = false;
	} else {
		if (vcpu_option_flags_get_critical(&current->vcpu_options)) {
			// HLOS VM calls to this function are passed directly to
			// the firmware, to reset the physical device.
			error_t ret = OK;
			(void)trigger_power_system_reset_event(
				PSCI_REQUEST_SYSTEM_RESET, 0U, &ret);
			panic("system_reset event returned");
		}

#if defined(INTERFACE_VCPU_RUN)
		// Tell the proxy thread that a reset was requested
		thread_get_self()->psci_system_reset = true;
		thread_get_self()->psci_system_reset_type =
			PSCI_REQUEST_SYSTEM_RESET;
		thread_get_self()->psci_system_reset_cookie = 0U;
#endif

		psci_stop_all_vcpus();
	}

	return handled;
}

static uint32_t
psci_system_reset2(uint64_t reset_type, uint64_t cookie)
{
	uint32_t  ret;
	thread_t *current = thread_get_self();

	if (vcpu_option_flags_get_critical(&current->vcpu_options)) {
		// HLOS VM calls to this function are passed directly to the
		// firmware, to reset the physical device.
		error_t error = OK;
		(void)trigger_power_system_reset_event(reset_type, cookie,
						       &error);

		if (error == ERROR_ARGUMENT_INVALID) {
			ret = (uint32_t)PSCI_RET_INVALID_PARAMETERS;
		} else {
			ret = (uint32_t)PSCI_RET_NOT_SUPPORTED;
		}
	} else {
#if defined(INTERFACE_VCPU_RUN)
		// Tell the proxy thread that a reset was requested
		thread_get_self()->psci_system_reset	    = true;
		thread_get_self()->psci_system_reset_type   = reset_type;
		thread_get_self()->psci_system_reset_cookie = cookie;
#endif

		psci_stop_all_vcpus();
	}

	return ret;
}

bool
psci_system_reset2_32(uint32_t arg1, uint32_t arg2, uint32_t *ret0)
{
	bool	  handled;
	thread_t *current = thread_get_self();

	if (compiler_unexpected(current->psci_group == NULL)) {
		handled = false;
	} else {
		*ret0	= psci_system_reset2(arg1, arg2);
		handled = true;
	}

	return handled;
}

bool
psci_system_reset2_64(uint64_t arg1, uint64_t arg2, uint64_t *ret0)
{
	bool	  handled;
	thread_t *current = thread_get_self();

	if (compiler_unexpected(current->psci_group == NULL)) {
		handled = false;
	} else {
		*ret0 = psci_system_reset2(
			(uint32_t)arg1 | PSCI_REQUEST_SYSTEM_RESET2_64, arg2);
		handled = true;
	}

	return handled;
}

bool
psci_features(uint32_t arg1, uint32_t *ret0)
{
	thread_t *current  = thread_get_self();
	bool	  has_psci = current->psci_group != NULL;

	smccc_function_id_t fn_id = smccc_function_id_cast(arg1);
	uint32_t	    ret	  = SMCCC_UNKNOWN_FUNCTION32;
	smccc_function_t    fn	  = smccc_function_id_get_function(&fn_id);

	if (has_psci &&
	    (smccc_function_id_get_owner_id(&fn_id) ==
	     SMCCC_OWNER_ID_STANDARD) &&
	    smccc_function_id_get_is_fast(&fn_id) &&
	    (smccc_function_id_get_res0(&fn_id) == 0U)) {
		ret = smccc_function_id_get_is_smc64(&fn_id)
			      ? trigger_psci_features64_event(
					(psci_function_t)fn)
			      : trigger_psci_features32_event(
					(psci_function_t)fn);
	} else if ((smccc_function_id_get_owner_id(&fn_id) ==
		    SMCCC_OWNER_ID_ARCH) &&
		   smccc_function_id_get_is_fast(&fn_id) &&
		   !smccc_function_id_get_is_smc64(&fn_id) &&
		   (smccc_function_id_get_res0(&fn_id) == 0U) &&
		   (fn == (smccc_function_t)SMCCC_ARCH_FUNCTION_VERSION)) {
		// SMCCC>=1.1 is implemented and SMCCC_VERSION is safe to call.
		ret = (uint32_t)PSCI_RET_SUCCESS;
	} else {
		/* Do Nothing */
	}

	*ret0 = ret;
	return true;
}

error_t
psci_handle_object_create_thread(thread_create_t thread_create)
{
	thread_t *thread = thread_create.thread;
	assert(thread != NULL);

	// FIXME:
	psci_suspend_powerstate_t pstate = psci_suspend_powerstate_default();
#if !defined(PSCI_AFFINITY_LEVELS_NOT_SUPPORTED) ||                            \
	!PSCI_AFFINITY_LEVELS_NOT_SUPPORTED
	psci_suspend_powerstate_stateid_t stateid =
		platform_psci_deepest_cluster_level_stateid(
			thread->scheduler_affinity);
#else
	psci_suspend_powerstate_stateid_t stateid =
		platform_psci_deepest_cpu_level_stateid(
			thread->scheduler_affinity);
#endif
	psci_suspend_powerstate_set_StateID(&pstate, stateid);
	psci_suspend_powerstate_set_StateType(
		&pstate, PSCI_SUSPEND_POWERSTATE_TYPE_POWERDOWN);

	// Initialize to deepest possible state
	thread->psci_suspend_state = pstate;

	return OK;
}

error_t
psci_handle_object_activate_thread(thread_t *thread)
{
	error_t err;
	if (thread->kind != THREAD_KIND_VCPU) {
		thread->vpm_mode = VPM_MODE_NONE;
		err		 = OK;
	} else if (thread->psci_group == NULL) {
		thread->vpm_mode = VPM_MODE_IDLE;
		err		 = OK;
	} else {
		assert(scheduler_is_blocked(thread, SCHEDULER_BLOCK_VCPU_OFF));

		thread->vpm_mode  = vpm_group_option_flags_get_no_aggregation(
					    &thread->psci_group->options)
					    ? VPM_MODE_NONE
					    : VPM_MODE_PSCI;
		cpu_index_t index = thread->psci_index;
		thread_t   *tmp_null = NULL;

		if (!cpulocal_index_valid(index)) {
			err = ERROR_OBJECT_CONFIG;
		} else if (!atomic_compare_exchange_strong_explicit(
				   &thread->psci_group->psci_cpus[index],
				   &tmp_null, thread, memory_order_release,
				   memory_order_relaxed)) {
			err = ERROR_DENIED;
		} else {
			err = OK;
		}
	}
	return err;
}

void
psci_handle_object_deactivate_thread(thread_t *thread)
{
	assert(thread != NULL);

	if (thread->psci_group != NULL) {
		thread_t   *tmp	  = thread;
		cpu_index_t index = thread->psci_index;

		(void)atomic_compare_exchange_strong_explicit(
			&thread->psci_group->psci_cpus[index], &tmp, NULL,
			memory_order_relaxed, memory_order_relaxed);
		object_put_vpm_group(thread->psci_group);
	}

	if (thread->vpm_mode == VPM_MODE_PSCI) {
		scheduler_lock(thread);
		psci_pm_list_delete(scheduler_get_affinity(thread), thread);
		scheduler_unlock(thread);
	}
}

void
psci_handle_object_deactivate_vpm_group(vpm_group_t *pg)
{
	for (cpu_index_t i = 0; cpulocal_index_valid(i); i++) {
		assert(atomic_load_relaxed(&pg->psci_cpus[i]) == NULL);
	}

	cpulocal_begin();
	ipi_one_relaxed(IPI_REASON_IDLE, cpulocal_get_index());
	ipi_others_idle(IPI_REASON_IDLE);
	cpulocal_end();
}

error_t
vpm_group_configure(vpm_group_t *vpm_group, vpm_group_option_flags_t flags)
{
	vpm_group->options = flags;

	return OK;
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

		err = OK;
	}

	return err;
}

error_t
psci_handle_task_queue_execute(task_queue_entry_t *task_entry)
{
	assert(task_entry != NULL);
	vpm_group_t *vpm_group =
		vpm_group_container_of_psci_virq_task(task_entry);

	(void)virq_assert(&vpm_group->psci_system_suspend_virq, true);
	object_put_vpm_group(vpm_group);

	return OK;
}

error_t
vpm_bind_virq(vpm_group_t *vpm_group, vic_t *vic, virq_t virq)
{
	error_t ret;

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

bool
vcpus_state_is_any_awake(vpm_group_suspend_state_t vm_state, uint32_t level,
			 cpu_index_t cpu)
{
	bool	 vcpu_awake = false;
	error_t	 ret;
	uint32_t start_idx = 0, children_counts = 0;

	uint64_t vcpus_state =
		vpm_group_suspend_state_get_vcpus_state(&vm_state);

	uint16_t vcluster_state =
		vpm_group_suspend_state_get_cluster_state(&vm_state);

	ret = platform_psci_get_index_by_level(cpu, &start_idx,
					       &children_counts, level);

	index_t idle_state = 0U, psci_index;
	if (ret != OK) {
		goto out;
	}

	for (index_t i = 0U; i < children_counts; i++) {
		// Check if another vcpu is awake
		psci_index = start_idx + i;

		if (level == 1U) {
			idle_state =
				(index_t)(vcpus_state >>
					  (psci_index *
					   PSCI_VCPUS_STATE_PER_VCPU_BITS)) &
				PSCI_VCPUS_STATE_PER_VCPU_MASK;
			if (platform_psci_is_cpu_active(
				    (psci_cpu_state_t)idle_state)) {
				vcpu_awake = true;
				goto out;
			}
		} else if (level == 2U) {
			idle_state = ((index_t)vcluster_state >>
				      ((psci_index % (PLATFORM_MAX_CORES)) *
				       PSCI_PER_CLUSTER_STATE_BITS)) &
				     PSCI_PER_CLUSTER_STATE_BITS_MASK;
			if (platform_psci_is_cluster_active(
				    (psci_cluster_state_L3_t)idle_state)) {
				vcpu_awake = true;
				goto out;
			}
		} else {
			// Only two levels are implemented. Return false
		}
	}
out:
	return vcpu_awake;
}

void
vcpus_state_set(vpm_group_suspend_state_t *vm_state, cpu_index_t cpu,
		psci_cpu_state_t cpu_state)
{
	uint64_t vcpus_state =
		vpm_group_suspend_state_get_vcpus_state(vm_state);

	vcpus_state &= ~((uint64_t)PSCI_VCPUS_STATE_PER_VCPU_MASK
			 << (cpu * PSCI_VCPUS_STATE_PER_VCPU_BITS));
	vcpus_state |= (uint64_t)cpu_state
		       << (cpu * PSCI_VCPUS_STATE_PER_VCPU_BITS);

	vpm_group_suspend_state_set_vcpus_state(vm_state, vcpus_state);
}

void
vcpus_state_clear(vpm_group_suspend_state_t *vm_state, cpu_index_t cpu)
{
	uint64_t vcpus_state =
		vpm_group_suspend_state_get_vcpus_state(vm_state);

	vcpus_state &= ~((uint64_t)PSCI_VCPUS_STATE_PER_VCPU_MASK
			 << (cpu * PSCI_VCPUS_STATE_PER_VCPU_BITS));

	vpm_group_suspend_state_set_vcpus_state(vm_state, vcpus_state);
}

error_t
psci_handle_vcpu_suspend(thread_t *current)
{
	error_t ret;

	if (current->vpm_mode != VPM_MODE_NONE) {
		ret = psci_vcpu_suspend(current);
	} else {
		ret = OK;
	}

	if (ret == OK) {
		TRACE(PSCI, PSCI_VPM_VCPU_SUSPEND,
		      "psci vcpu suspend: {:#x} - VM {:d}", (uintptr_t)current,
		      current->addrspace->vmid);
	}

	return ret;
}

void
psci_unwind_vcpu_suspend(thread_t *current)
{
	if (current->vpm_mode != VPM_MODE_NONE) {
		psci_vcpu_resume(current);
	}
}

bool
psci_handle_trapped_idle(void)
{
	thread_t *current = thread_get_self();
	bool	  handled = false;

	if (current->vpm_mode == VPM_MODE_IDLE) {
		error_t err = vcpu_suspend();
		if ((err != OK) && (err != ERROR_BUSY)) {
			panic("unhandled vcpu_suspend error (WFI)");
		}
		handled = true;
	}

	return handled;
}

void
psci_handle_vcpu_resume(thread_t *vcpu)
{
	TRACE(PSCI, PSCI_VPM_VCPU_RESUME,
	      "psci vcpu resume: {:#x} - VM {:d} - VCPU {:d}", (uintptr_t)vcpu,
	      vcpu->addrspace->vmid, vcpu->psci_index);

	if (vcpu->vpm_mode != VPM_MODE_NONE) {
		psci_vcpu_resume(vcpu);
	}
}

void
psci_handle_vcpu_started(bool warm_reset)
{
	// If the VCPU has been warm-reset, there was no vcpu_stopped event and
	// no automatic psci_vcpu_suspend() call, so there's no need for a
	// wakeup here.
	if (!warm_reset) {
		thread_t *current = thread_get_self();

		TRACE(PSCI, PSCI_VPM_VCPU_RESUME,
		      "psci vcpu started: {:#x} - VM {:d}", (uintptr_t)current,
		      current->addrspace->vmid);

		if (current->vpm_mode != VPM_MODE_NONE) {
			preempt_disable();
			psci_vcpu_resume(current);
			preempt_enable();
		}
	}
}

void
psci_handle_vcpu_wakeup(thread_t *vcpu)
{
	if (scheduler_is_blocked(vcpu, SCHEDULER_BLOCK_VCPU_SUSPEND)) {
		vcpu_resume(vcpu);
	}
}

void
psci_handle_vcpu_wakeup_self(void)
{
	thread_t *current = thread_get_self();
	assert(!scheduler_is_blocked(current, SCHEDULER_BLOCK_VCPU_SUSPEND) ||
	       thread_is_dying(current));
}

bool
psci_handle_vcpu_expects_wakeup(const thread_t *thread)
{
	return scheduler_is_blocked(thread, SCHEDULER_BLOCK_VCPU_SUSPEND);
}

#if defined(INTERFACE_VCPU_RUN)
vcpu_run_state_t
psci_handle_vcpu_run_check(const thread_t *thread, register_t *state_data_0,
			   register_t *state_data_1)
{
	vcpu_run_state_t ret;

	if (thread->psci_system_reset) {
		ret	      = VCPU_RUN_STATE_PSCI_SYSTEM_RESET;
		*state_data_0 = thread->psci_system_reset_type;
		*state_data_1 = thread->psci_system_reset_cookie;
	} else if (psci_handle_vcpu_expects_wakeup(thread)) {
		ret = VCPU_RUN_STATE_EXPECTS_WAKEUP;
		*state_data_0 =
			psci_suspend_powerstate_raw(thread->psci_suspend_state);
		vpm_group_t *vpm_group = thread->psci_group;
		bool	     system_suspend;
		if (vpm_group != NULL) {
			vpm_group_suspend_state_t vm_state =
				atomic_load_acquire(
					&vpm_group->psci_vm_suspend_state);
			system_suspend =
				vpm_group_suspend_state_get_system_suspend(
					&vm_state);
		} else {
			system_suspend = false;
		}
		vcpu_run_wakeup_from_state_t from_state =
			system_suspend
				? VCPU_RUN_WAKEUP_FROM_STATE_PSCI_SYSTEM_SUSPEND
				: VCPU_RUN_WAKEUP_FROM_STATE_PSCI_CPU_SUSPEND;
		*state_data_1 = (register_t)from_state;
	} else {
		ret = VCPU_RUN_STATE_BLOCKED;
	}

	return ret;
}
#endif

error_t
psci_handle_vcpu_poweron(thread_t *vcpu)
{
	if (compiler_unexpected(vcpu->psci_group == NULL)) {
		goto out;
	}

	(void)atomic_fetch_add_explicit(&vcpu->psci_group->psci_online_count,
					1U, memory_order_relaxed);
	cpu_index_t cpu = vcpu->scheduler_affinity;
	if (cpulocal_index_valid(cpu)) {
		psci_vcpu_clear_vcpu_state(vcpu, cpu);
	}

out:
	return OK;
}

error_t
psci_handle_vcpu_poweroff(thread_t *vcpu, bool last_cpu, bool force)
{
	error_t	     ret;
	vpm_group_t *psci_group = vcpu->psci_group;

	if (psci_group == NULL) {
		// This is always the last CPU in the VM, so permit the poweroff
		// request if and only if it is intended for the last CPU or is
		// forced.
		ret = (last_cpu || force) ? OK : ERROR_DENIED;
	} else if (vcpu->vpm_mode == VPM_MODE_PSCI) {
		count_t online_cpus =
			atomic_load_relaxed(&psci_group->psci_online_count);
		do {
			assert(online_cpus > 0U);
			if (!force && (last_cpu != (online_cpus == 1U))) {
				ret = ERROR_DENIED;
				goto out;
			}
		} while (!atomic_compare_exchange_weak_explicit(
			&psci_group->psci_online_count, &online_cpus,
			online_cpus - 1U, memory_order_relaxed,
			memory_order_relaxed));

		ret = OK;
	} else {
		assert(vcpu->vpm_mode == VPM_MODE_NONE);
		ret = OK;
	}

out:
	return ret;
}

void
psci_handle_vcpu_stopped(void)
{
	thread_t *vcpu = thread_get_self();

	if (vcpu->psci_group != NULL) {
		// Stopping a VCPU forces it into a power-off suspend state.
		psci_suspend_powerstate_t pstate =
			psci_suspend_powerstate_default();
		psci_suspend_powerstate_set_StateType(
			&pstate, PSCI_SUSPEND_POWERSTATE_TYPE_POWERDOWN);
		psci_suspend_powerstate_stateid_t stateid;

		preempt_disable();
		cpu_index_t cpu = cpulocal_get_index();

#if !defined(PSCI_AFFINITY_LEVELS_NOT_SUPPORTED) ||                            \
	!PSCI_AFFINITY_LEVELS_NOT_SUPPORTED
		// FIXME:
		if (vcpu->psci_group->psci_mode == PSCI_MODE_PC) {
			stateid = platform_psci_deepest_cluster_level_stateid(
				cpu);
		} else
#endif
		{
			stateid = platform_psci_deepest_cpu_level_stateid(cpu);
		}
		preempt_enable();

		psci_suspend_powerstate_set_StateID(&pstate, stateid);
		vcpu->psci_suspend_state = pstate;
	}

	if (vcpu->vpm_mode != VPM_MODE_NONE) {
		preempt_disable();
		error_t ret = psci_vcpu_suspend(vcpu);
		preempt_enable();
		// Note that psci_vcpu_suspend can only fail if we are in OSI
		// mode and requesting a cluster suspend state, which can't
		// happen here because we set a non-cluster state above.
		assert(ret == OK);
	}
}

void
psci_handle_power_cpu_online(void)
{
	(void)psci_set_vpm_active_pcpus_bit(cpulocal_get_index());
}

void
psci_handle_power_cpu_offline(void)
{
	(void)psci_clear_vpm_active_pcpus_bit(cpulocal_get_index());
}
