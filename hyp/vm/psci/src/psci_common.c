// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <cpulocal.h>
#include <ipi.h>
#include <log.h>
#include <object.h>
#include <panic.h>
#include <platform_cpu.h>
#include <platform_psci.h>
#include <platform_timer.h>
#include <preempt.h>
#include <psci.h>
#include <rcu.h>
#include <scheduler.h>
#include <smccc.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <trace_helpers.h>
#include <util.h>
#include <vcpu.h>
#include <vgic.h>
#include <vic.h>
#include <virq.h>

#include <events/power.h>
#include <events/psci.h>
#include <events/vcpu.h>

#include "event_handlers.h"
#include "psci_arch.h"
#include "psci_common.h"
#include "psci_helper.h"
#include "psci_pm_list.h"
#include "vpm_base.h"

CPULOCAL_DECLARE_STATIC(_Atomic count_t, vpm_active_vcpus);

#define REGISTER_BITS util_width(register_t)
static_assert(PLATFORM_MAX_CORES <= REGISTER_BITS,
	      "PLATFORM_MAX_CORES > REGISTER_BITS");
static _Atomic register_t vpm_active_pcpus_bitmap;

void
psci_handle_boot_cold_init(void)
{
	if (psci_smc_psci_version() < PSCI_VERSION) {
		panic("psci version");
	}

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
index_t
psci_clear_vpm_active_pcpus_bit(cpu_index_t bit)
{
	index_t result;

	register_t cleared_bit = ~util_bit(bit);

	register_t old = atomic_fetch_and_explicit(
		&vpm_active_pcpus_bitmap, cleared_bit, memory_order_relaxed);

	register_t running = (old & cleared_bit);

	cpuid_set_t wakeup_check_mask = platform_get_functional_cpus();
	wakeup_check_mask.bitmap[0] &= cleared_bit;

#if (PLATFORM_MAX_HIERARCHY == 2)
	if ((running == 0U) && !ipi_check_suspended_cpus(wakeup_check_mask)) {
		// Last core at the system level
		result = 2U;
		goto out;
	}

	// Limit the considered cores to the local cluster
	const register_t cluster_mask = platform_cluster_mask(bit);
	running &= cluster_mask;
	wakeup_check_mask.bitmap[0] &= cluster_mask;
#endif
	if ((running == 0U) && !ipi_check_suspended_cpus(wakeup_check_mask)) {
		// Last core at the cluster level
		result = 1U;
		goto out;
	}

	// Not the last core at any level
	result = 0U;
out:
	return result;
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
	       0U;
}

bool
psci_handle_vcpu_activate_thread(thread_t *thread)
{
	assert(thread != NULL);
	assert(vcpu_is_vcpu(thread));

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
		if (thread->vpm_group != NULL) {
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

	return true;
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
		assert(vcpu_is_vcpu(thread));
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
	thread_t    *current   = thread_get_self();
	thread_t    *result    = NULL;
	vpm_group_t *vpm_group = current->vpm_group;

	assert(vpm_group != NULL);

	// This function is not performance-critical; it is only called during
	// PSCI_CPU_ON and PSCI_AFFINITY_INFO. A simple linear search of the VPM
	// group is good enough.
	rcu_read_start();
	for (index_t i = 0U; i < util_array_size(vpm_group->psci_cpus); i++) {
		thread_t *thread =
			atomic_load_consume(&vpm_group->psci_cpus[i]);
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

	if (compiler_unexpected(current->vpm_group == NULL)) {
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
		assert(current->vpm_group != NULL);

		// FIXME: QC Gunyah issue #129
		cpulocal_begin();
		ret = platform_psci_suspend_state_validation(
			suspend_state, cpulocal_get_index(),
			current->vpm_group->psci_mode);
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

	if (compiler_unexpected(current->vpm_group == NULL)) {
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

	if (compiler_unexpected(current->vpm_group == NULL)) {
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

	// FIXME: QC Gunyah issue #129
	cpulocal_begin();
	psci_suspend_powerstate_stateid_t stateid =
		platform_psci_deepest_level_stateid(cpulocal_get_index(), 0U);
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

	if (compiler_unexpected(current->vpm_group == NULL)) {
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

	if (compiler_unexpected(current->vpm_group == NULL)) {
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

	if (compiler_unexpected(current->vpm_group == NULL)) {
		handled = false;
	} else {
		vcpu_power_req_flags_t power_flags =
			vcpu_power_req_flags_default();
		vcpu_power_req_flags_set_psci_op(&power_flags, true);

		error_t ret = vcpu_poweroff(false, false, power_flags);
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

			if (!valid || (index >= PLATFORM_MAX_CORES)) {
				ret = PSCI_RET_INVALID_PARAMETERS;
			} else {
				ret = PSCI_RET_INTERNAL_FAILURE;
			}
		}
		goto out;
	}

	bool	     reschedule = false;
	vpm_group_t *vpm_group	= thread->vpm_group;

	if (vcpu_option_flags_get_pinned(&thread->vcpu_options) &&
	    !platform_cpu_functional(thread->scheduler_affinity)) {
		ret = PSCI_RET_INTERNAL_FAILURE;
		goto out_put_thread;
	}

	bool is_online = bitmap_atomic_isset(vpm_group->psci_online_vcpus,
					     thread->psci_index,
					     memory_order_acquire);
	if (bitmap_atomic_isset(vpm_group->psci_started_vcpus,
				thread->psci_index, memory_order_relaxed)) {
		if (is_online) {
			ret = PSCI_RET_ALREADY_ON;
		} else {
			ret = PSCI_RET_ON_PENDING;
		}
		goto out_put_thread;
	}

	vcpu_power_req_flags_t power_flags = vcpu_power_req_flags_default();
	vcpu_power_req_flags_set_psci_op(&power_flags, true);

	scheduler_lock(thread);
	bool_result_t result =
		vcpu_poweron(thread, vmaddr_result_ok(entry_point_address),
			     register_result_ok(context_id), power_flags);
	reschedule = result.r;
	ret	   = (result.e == OK) ? PSCI_RET_SUCCESS
		     : (result.e == ERROR_ARGUMENT_INVALID)
			     ? PSCI_RET_INVALID_PARAMETERS
		     : (result.e == ERROR_RETRY) ? PSCI_RET_ALREADY_ON
		     : (result.e == ERROR_BUSY)	 ? PSCI_RET_ON_PENDING
						 : PSCI_RET_INTERNAL_FAILURE;
	scheduler_unlock(thread);

out_put_thread:
	object_put_thread(thread);

	if (reschedule) {
		(void)scheduler_schedule();
	}

out:
	return ret;
}

bool
psci_cpu_on_32(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t *ret0)
{
	bool	  handled;
	thread_t *current = thread_get_self();

	if (compiler_unexpected(current->vpm_group == NULL)) {
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

	if (compiler_unexpected(current->vpm_group == NULL)) {
		handled = false;
	} else {
		psci_ret_t ret = psci_cpu_on(psci_mpidr_cast(arg1), arg2, arg3);
		*ret0	       = (uint64_t)ret;
		handled	       = true;
	}
	return handled;
}

static psci_ret_affinity_info_t
psci_affinity_info(psci_mpidr_t affinity, uint32_t lowest_affinity_level)
{
	psci_ret_affinity_info_t ret;

	thread_t *thread = psci_get_thread_by_mpidr(affinity);
	if (thread == NULL) {
		ret = PSCI_RET_AFFINITY_INFO_INVALID_PARAMETERS;
	} else if (lowest_affinity_level != 0U) {
		// lowest_affinity_level is legacy from PSCI 0.2; we are
		// allowed to fail if it is nonzero (which indicates a
		// query of the cluster-level state).
		ret = PSCI_RET_AFFINITY_INFO_INVALID_PARAMETERS;
	} else {
		bool is_online = bitmap_atomic_isset(
			thread->vpm_group->psci_online_vcpus,
			thread->psci_index, memory_order_acquire);
		if (bitmap_atomic_isset(thread->vpm_group->psci_started_vcpus,
					thread->psci_index,
					memory_order_relaxed)) {
			if (is_online) {
				ret = PSCI_RET_AFFINITY_INFO_ON;
			} else {
				ret = PSCI_RET_AFFINITY_INFO_ON_PENDING;
			}
		} else {
			ret = PSCI_RET_AFFINITY_INFO_OFF;
		}
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

	if (compiler_unexpected(current->vpm_group == NULL)) {
		handled = false;
	} else {
		psci_ret_affinity_info_t ret =
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

	if (compiler_unexpected(current->vpm_group == NULL)) {
		handled = false;
	} else {
		psci_ret_affinity_info_t ret = psci_affinity_info(
			psci_mpidr_cast(arg1), (uint32_t)arg2);
		*ret0	= (uint64_t)ret;
		handled = true;
	}
	return handled;
}

static noreturn void
psci_stop_all_vcpus(void)
{
	thread_t *current = thread_get_self();
	assert(current != NULL);
	assert(vcpu_is_vcpu(current));

	vpm_group_t *vpm_group = current->vpm_group;
	if (vpm_group != NULL) {
		for (index_t i = 0U; i < util_array_size(vpm_group->psci_cpus);
		     i++) {
			thread_t *thread =
				atomic_load_consume(&vpm_group->psci_cpus[i]);
			if ((thread != NULL) && (thread != current)) {
				error_t err = thread_kill(thread);
				if (err != OK) {
					panic("Unable to kill VCPU");
				}
			}
		}
	}

	vcpu_power_req_flags_t power_flags = vcpu_power_req_flags_default();
	vcpu_power_req_flags_set_psci_op(&power_flags, true);

	// Force power off
	(void)vcpu_poweroff(false, true, power_flags);
	// We should not be denied when force is true
	panic("vcpu_poweroff(force=true) returned");
}

static void
psci_set_block_all_other_vcpus(const thread_t *current, bool block)
	REQUIRE_PREEMPT_DISABLED
{
	bool trigger = false;

	vpm_group_t *vpm_group = current->vpm_group;

	if (vpm_group == NULL) {
		goto out;
	}

	for (index_t i = 0U; i < util_array_size(vpm_group->psci_cpus); i++) {
		thread_t *thread =
			atomic_load_consume(&vpm_group->psci_cpus[i]);
		if ((thread != NULL) && (thread != current)) {
			scheduler_lock_nopreempt(thread);
			if (block) {
				scheduler_block(thread,
						SCHEDULER_BLOCK_PSCI_STOP);
			} else {
				if (scheduler_unblock(
					    thread,
					    SCHEDULER_BLOCK_PSCI_STOP)) {
					trigger = true;
				}
			}
			scheduler_unlock_nopreempt(thread);
		}
	}

	if (trigger) {
		scheduler_trigger();
	}
out:
	return;
}

static void
psci_vm_off(thread_t *current, vm_off_flags_t flags, uint64_t type,
	    uint64_t cookie)
{
	bool defer = false;

	preempt_disable();

	trigger_vcpu_vm_off_request_event(current, flags, type, cookie, &defer);

	if (defer) {
		psci_set_block_all_other_vcpus(current, true);
		scheduler_yield();
		psci_set_block_all_other_vcpus(current, false);
	}

	preempt_enable();
}

bool
psci_system_off(void)
{
	bool	  handled;
	thread_t *current = thread_get_self();

	if (compiler_unexpected(current->vpm_group == NULL)) {
		handled = false;
	} else {
		psci_vm_off(current, VM_OFF_FLAGS_OFF, 0U, 0U);

		if (vcpu_option_flags_get_critical(&current->vcpu_options)) {
			// HLOS VM calls to this function are passed directly to
			// the firmware, to power off the physical device.
			trigger_power_system_off_event();
			panic("system_off event returned");
		}

		atomic_store_relaxed(&current->vpm_group->vm_on, false);
		psci_stop_all_vcpus();
	}

	return handled;
}

bool
psci_system_reset(void)
{
	bool	  handled;
	thread_t *current = thread_get_self();

	if (compiler_unexpected(current->vpm_group == NULL)) {
		handled = false;
	} else {
		psci_vm_off(current, VM_OFF_FLAGS_RESET,
			    PSCI_REQUEST_SYSTEM_RESET, 0U);

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

		atomic_store_relaxed(&current->vpm_group->vm_on, false);
		psci_stop_all_vcpus();
	}

	return handled;
}

static uint32_t
psci_system_reset2(uint64_t reset_type, uint64_t cookie)
{
	uint32_t  ret;
	thread_t *current = thread_get_self();

	bool critical = vcpu_option_flags_get_critical(&current->vcpu_options);

	psci_vm_off(current,
		    critical ? VM_OFF_FLAGS_RESET_MAY_FAIL : VM_OFF_FLAGS_RESET,
		    reset_type, cookie);

	if (critical) {
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

		trigger_vcpu_vm_off_failed_event(current);
	} else {
#if defined(INTERFACE_VCPU_RUN)
		// Tell the proxy thread that a reset was requested
		thread_get_self()->psci_system_reset	    = true;
		thread_get_self()->psci_system_reset_type   = reset_type;
		thread_get_self()->psci_system_reset_cookie = cookie;
#endif

		atomic_store_relaxed(&current->vpm_group->vm_on, false);
		psci_stop_all_vcpus();
	}

	return ret;
}

bool
psci_system_reset2_32(uint32_t arg1, uint32_t arg2, uint32_t *ret0)
{
	bool	  handled;
	thread_t *current = thread_get_self();

	if (compiler_unexpected(current->vpm_group == NULL)) {
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

	if (compiler_unexpected(current->vpm_group == NULL)) {
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
	bool	  has_psci = current->vpm_group != NULL;

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

	// FIXME: QC Gunyah issue #129
	psci_suspend_powerstate_t pstate = psci_suspend_powerstate_default();
#if !defined(PSCI_AFFINITY_LEVELS_NOT_SUPPORTED) ||                            \
	!PSCI_AFFINITY_LEVELS_NOT_SUPPORTED
	psci_suspend_powerstate_stateid_t stateid =
		platform_psci_deepest_level_stateid(thread->scheduler_affinity,
						    PLATFORM_MAX_HIERARCHY);
#else
	psci_suspend_powerstate_stateid_t stateid =
		platform_psci_deepest_level_stateid(thread->scheduler_affinity,
						    0U);
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
	if (!vcpu_is_vcpu(thread)) {
		thread->vpm_mode = VPM_MODE_NONE;
		err		 = OK;
	} else if (thread->vpm_group == NULL) {
		thread->vpm_mode = VPM_MODE_IDLE;
		err		 = OK;
	} else {
		assert(scheduler_is_blocked(thread, SCHEDULER_BLOCK_VCPU_OFF));

		thread->vpm_mode  = vpm_group_option_flags_get_no_aggregation(
					    &thread->vpm_group->options)
					    ? VPM_MODE_NONE
					    : VPM_MODE_PSCI;
		cpu_index_t index = thread->psci_index;
		thread_t   *tmp_null = NULL;

		if (!cpulocal_index_valid(index)) {
			err = ERROR_OBJECT_CONFIG;
		} else if (!atomic_compare_exchange_strong_explicit(
				   &thread->vpm_group->psci_cpus[index],
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

	if (thread->vpm_group != NULL) {
		thread_t   *tmp	  = thread;
		cpu_index_t index = thread->psci_index;

		(void)atomic_compare_exchange_strong_explicit(
			&thread->vpm_group->psci_cpus[index], &tmp, NULL,
			memory_order_relaxed, memory_order_relaxed);
		object_put_vpm_group(thread->vpm_group);
	}

	if (thread->vpm_mode == VPM_MODE_PSCI) {
		scheduler_lock(thread);
		psci_pm_list_delete(scheduler_get_affinity(thread), thread);
		scheduler_unlock(thread);
	}
}

void
psci_handle_object_deactivate_vpm_group(vpm_group_t *vpm_group)
{
	for (cpu_index_t i = 0; cpulocal_index_valid(i); i++) {
		assert(atomic_load_relaxed(&vpm_group->psci_cpus[i]) == NULL);
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
	} else if (!vcpu_is_vcpu(thread)) {
		err = ERROR_ARGUMENT_INVALID;
	} else {
		if (thread->vpm_group != NULL) {
			object_put_vpm_group(thread->vpm_group);
		}

		thread->vpm_group  = object_get_vpm_group_additional(pg);
		thread->psci_index = (cpu_index_t)index;
		trace_ids_set_vcpu_index(&thread->trace_ids,
					 (cpu_index_t)index);

		err = OK;
	}

	return err;
}

error_t
psci_handle_task_queue_execute(task_queue_entry_t *entry)
{
	assert(entry != NULL);
	vpm_group_t *vpm_group = vpm_group_container_of_psci_virq_task(entry);

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
	uint32_t prev_level = level - 1U;
	bool	 vcpu_awake = false;
	error_t	 ret;
	uint32_t start_idx = 0U, children_counts = 0U;
	ret = platform_psci_get_index_by_level(cpu, &start_idx,
					       &children_counts, level);
	if (ret != OK) {
		goto out;
	}

	uint64_t lpm_state = psci_get_level_state(&vm_state, prev_level);

	index_t state_bits = 0U, state_mask = 0U;
	psci_get_level_bits_mask(prev_level, &state_bits, &state_mask);

	index_t counts_till_level = psci_get_level_node_count(0U);

	for (uint32_t idx = 1U; idx < prev_level; idx++) {
		counts_till_level += psci_get_level_node_count(idx);
	}

	for (index_t i = 0U; i < children_counts; i++) {
		// Check if another vcpu is awake
		index_t psci_index = (start_idx + i) % (counts_till_level);

		uint64_t idle_state = (lpm_state >> (psci_index * state_bits)) &
				      (uint64_t)state_mask;
		if (platform_psci_is_node_active((index_t)idle_state,
						 prev_level)) {
			vcpu_awake = true;
			goto out;
		}
	}
out:
	return vcpu_awake;
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
psci_handle_vcpu_resume(thread_t *current)
{
	TRACE(PSCI, PSCI_VPM_VCPU_RESUME,
	      "psci vcpu resume: {:#x} - VM {:d} - VCPU {:d}",
	      (uintptr_t)current, current->addrspace->vmid,
	      current->psci_index);

	if (current->vpm_mode != VPM_MODE_NONE) {
		psci_vcpu_resume(current);
	}
}

void
psci_handle_vcpu_started(bool warm_reset)
{
	thread_t *current = thread_get_self();

	if ((current->vpm_group != NULL) && !warm_reset) {
		bitmap_atomic_set(current->vpm_group->psci_online_vcpus,
				  current->psci_index, memory_order_relaxed);
		psci_vcpu_clear_vcpu_state(current);

		if (!atomic_exchange_relaxed(&current->vpm_group->vm_on,
					     true)) {
			trigger_vcpu_vm_on_event(current);
		}
	}

	// If the VCPU has been warm-reset, there was no vcpu_stopped event and
	// no automatic psci_vcpu_suspend() call, so there's no need for a
	// wakeup here.
	if (!warm_reset) {
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
psci_handle_vcpu_expects_wakeup(const thread_t *vcpu)
{
	return !scheduler_is_blocked(vcpu,
				     SCHEDULER_BLOCK_PSCI_SYSTEM_SUSPEND) &&
	       scheduler_is_blocked(vcpu, SCHEDULER_BLOCK_VCPU_SUSPEND);
}

#if defined(INTERFACE_VCPU_RUN)
vcpu_run_state_t
psci_handle_vcpu_run_check(const thread_t *vcpu, register_t *state_data_0,
			   register_t *state_data_1)
{
	vcpu_run_state_t ret;

	if (vcpu->psci_system_reset) {
		ret	      = VCPU_RUN_STATE_PSCI_SYSTEM_RESET;
		*state_data_0 = vcpu->psci_system_reset_type;
		*state_data_1 = vcpu->psci_system_reset_cookie;
	} else if (psci_handle_vcpu_expects_wakeup(vcpu)) {
		ret = VCPU_RUN_STATE_EXPECTS_WAKEUP;
		*state_data_0 =
			psci_suspend_powerstate_raw(vcpu->psci_suspend_state);
		vpm_group_t *vpm_group = vcpu->vpm_group;
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
psci_handle_vcpu_poweron(thread_t *vcpu, vcpu_power_req_flags_t power_flags)
{
	error_t	     ret;
	vpm_group_t *vpm_group = vcpu->vpm_group;

	if (compiler_unexpected(vpm_group == NULL)) {
		ret = OK;
		goto out;
	}
	bool psci_mode = vcpu->vpm_mode == VPM_MODE_PSCI;

	static_assert(sizeof(vpm_group->psci_started_vcpus) ==
			      sizeof(register_t),
		      "psci cpu bitmap");
	register_t started_vcpus =
		atomic_load_relaxed(&vpm_group->psci_started_vcpus[0]);
	register_t cpu_bit = util_bit(vcpu->psci_index);

	// Atomically check for first vcpu poweron and update started_vcpus
	do {
		// If this is the not the first vcpu poweron
		if (psci_mode && (started_vcpus != 0U)) {
			// Permit only the first non-psci power-on as this can
			// never be done via PSCI calls. Note, the vcpu_poweron
			// may set the vcpu to started, so if the power on
			// fails, we can't retry, and the vcpu needs to be
			// deleted.
			if (!vcpu_power_req_flags_get_psci_op(&power_flags)) {
				ret = ERROR_DENIED;
				goto out;
			}
		}
	} while (!atomic_compare_exchange_weak_explicit(
		&vpm_group->psci_started_vcpus[0], &started_vcpus,
		started_vcpus | cpu_bit, memory_order_relaxed,
		memory_order_relaxed));

	ret = OK;
out:
	return ret;
}

error_t
psci_handle_vcpu_poweroff(thread_t *current, bool last_vcpu, bool force,
			  vcpu_power_req_flags_t power_flags)
{
	error_t	     ret;
	vpm_group_t *vpm_group = current->vpm_group;

	if (vpm_group == NULL) {
		// This is always the last CPU in the VM, so permit the poweroff
		// request if and only if it is intended for the last CPU or is
		// forced.
		ret = (last_vcpu || force) ? OK : ERROR_DENIED;
		goto out;
	}

	if (!vcpu_power_req_flags_get_psci_op(&power_flags)) {
		// Don't permit non-PSCI poweroff in PSCI mode
		ret = ERROR_DENIED;
		goto out;
	}

	static_assert(sizeof(vpm_group->psci_started_vcpus) ==
			      sizeof(register_t),
		      "psci cpu bitmap");
	register_t cpu_bit = util_bit(current->psci_index);
	register_t started_vcpus =
		atomic_load_relaxed(&vpm_group->psci_started_vcpus[0]);
	// Clear the current vcpu in psci_started_vcpus. Use atomic exchange to
	// handle concurrent power-offs.
	do {
		// We check here whether we are the last vcpu, and attempt to
		// prevent turning it off.
		if (!force && (last_vcpu != (started_vcpus == cpu_bit))) {
			ret = ERROR_DENIED;
			goto out;
		}
	} while (!atomic_compare_exchange_weak_explicit(
		&vpm_group->psci_started_vcpus[0], &started_vcpus,
		started_vcpus & ~cpu_bit, memory_order_relaxed,
		memory_order_relaxed));

	ret = OK;

out:
	return ret;
}

void
psci_handle_vcpu_stopped(void)
{
	thread_t    *current   = thread_get_self();
	vpm_group_t *vpm_group = current->vpm_group;
	error_t	     ret;

	if (vpm_group != NULL) {
		// Stopping a VCPU forces it into a power-off suspend state.
		psci_suspend_powerstate_t pstate =
			psci_suspend_powerstate_default();
		psci_suspend_powerstate_set_StateType(
			&pstate, PSCI_SUSPEND_POWERSTATE_TYPE_POWERDOWN);

		preempt_disable();

		// Note, if this is a forced vcpu halt, then the vcpu poweroff
		// event isn't called, and psci_started_vcpus will remain set.
		// We don't support resuming a halted vcpu, and if we need to,
		// a new event would be needed to reset this vcpu state.
		register_t online_vcpus = atomic_fetch_and_explicit(
			&vpm_group->psci_online_vcpus[0],
			~util_bit(current->psci_index), memory_order_release);
		online_vcpus &= ~util_bit(current->psci_index);

		cpu_index_t cpu	  = cpulocal_get_index();
		count_t	    tries = 0;

		// Turn off the VCPU
		do {
			// On retries, delay by (tries) ms, in order to avoid
			// racing
			platform_timer_ndelay(tries * 1000000);

			TRACE(PSCI, PSCI_PSTATE_VALIDATION,
			      "psci vcpu poweroff try {:d}: core {:d} and current cores on {:d}",
			      tries, current->psci_index, online_vcpus);
			psci_suspend_powerstate_stateid_t stateid =
				psci_vcpu_get_poweroff_state(current, cpu,
							     online_vcpus);

			psci_suspend_powerstate_set_StateID(&pstate, stateid);
			current->psci_suspend_state = pstate;

			if (current->vpm_mode != VPM_MODE_NONE) {
				ret = psci_vcpu_suspend(current);
			} else {
				ret = OK;
			}

			if (ret != OK) {
				TRACE(PSCI, PSCI_PSTATE_VALIDATION,
				      "vcpu_suspend try {:d} failed due with error {:d}",
				      tries, (register_t)ret);
			}

			// Check for VCPUs that have come online, which might
			// require us to make a shallower suspend request if
			// we are in OSI mode. Note that it is never necessary
			// to make a deeper suspend request; if another CPU went
			// offline and made a deeper state possible, that CPU
			// will make the corresponding request.
			online_vcpus |= atomic_load_relaxed(
				&vpm_group->psci_online_vcpus[0]);
			tries++;
		} while ((ret != OK) && (tries < 3U));
		preempt_enable();
	} else if (current->vpm_mode != VPM_MODE_NONE) {
		preempt_disable();
		ret = psci_vcpu_suspend(current);
		preempt_enable();
	} else {
		// VPM mode is none; nothing to do
		ret = OK;
	}
	assert(ret == OK);
}

void
psci_handle_thread_killed(thread_t *thread)
{
	vpm_group_t *vpm_group = thread->vpm_group;
	if (vpm_group != NULL) {
		bitmap_atomic_clear(vpm_group->psci_started_vcpus,
				    thread->psci_index, memory_order_relaxed);
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

error_t
vpm_system_wakeup(vpm_group_t *vpm_group)
{
	error_t	  err;
	thread_t *wakeup_vcpu = NULL;

	static_assert(sizeof(vpm_group->psci_started_vcpus) ==
			      sizeof(register_t),
		      "psci cpu bitmap");
	register_t started_vcpus =
		atomic_load_relaxed(&vpm_group->psci_started_vcpus[0]);

	if (started_vcpus == 0U) {
		err = ERROR_FAILURE;
		goto out;
	}
	index_t cpu_last = compiler_msb(started_vcpus);

	// If started_vcpus has multiple bits set, then we should not attempt a
	// VM wakeup.
	if (started_vcpus != util_bit(cpu_last)) {
		err = ERROR_DENIED;
		goto out;
	}

	rcu_read_start();
	wakeup_vcpu = atomic_load_consume(&vpm_group->psci_cpus[cpu_last]);
	if (wakeup_vcpu == NULL) {
		// We raced with thread deletion
		err = ERROR_DENIED;
		rcu_read_finish();
		goto out;
	}
	scheduler_lock(wakeup_vcpu);
	if (!scheduler_is_blocked(wakeup_vcpu,
				  SCHEDULER_BLOCK_PSCI_SYSTEM_SUSPEND)) {
		scheduler_unlock(wakeup_vcpu);
		rcu_read_finish();
		err = ERROR_DENIED;
		goto out;
	}
	(void)object_get_thread_additional(wakeup_vcpu);
	rcu_read_finish();

	(void)scheduler_unblock(wakeup_vcpu,
				SCHEDULER_BLOCK_PSCI_SYSTEM_SUSPEND);

	vcpu_wakeup(wakeup_vcpu);
	err = OK;

	scheduler_unlock(wakeup_vcpu);
	object_put_thread(wakeup_vcpu);
out:
	return err;
}

void
vpm_set_threshold(vpm_group_t *vpm_group, uint64_t power_state)
{
	psci_suspend_powerstate_t pstate =
		psci_suspend_powerstate_cast((uint32_t)power_state);

	TRACE_AND_LOG(DEBUG, INFO, "vpm_set_threshold: power_state {:#x}",
		      psci_suspend_powerstate_raw(pstate));

	vpm_group->power_state_threshold = pstate;
}
