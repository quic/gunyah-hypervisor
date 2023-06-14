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
#include "psci_common.h"
#include "psci_pm_list.h"

void
psci_pc_handle_boot_cold_init(void)
{
#if !defined(PSCI_SET_SUSPEND_MODE_NOT_SUPPORTED) ||                           \
	!PSCI_SET_SUSPEND_MODE_NOT_SUPPORTED
	error_t ret = platform_psci_set_suspend_mode(PSCI_MODE_PC);
	assert(ret == OK);
#endif
}

uint32_t
psci_cpu_suspend_features(void)
{
	uint32_t ret;

	// Only Platform Co-ordinated mode, extended StateID
	ret = 2U;

	return ret;
}

bool
psci_pc_set_suspend_mode(uint32_t arg1, uint32_t *ret0)
{
	bool	   handled;
	psci_ret_t ret;

	thread_t *current = thread_get_self();

	if (current->psci_group == NULL) {
		handled = false;
	} else {
		if (arg1 == PSCI_MODE_PC) {
			ret = PSCI_RET_SUCCESS;
		} else {
			ret = PSCI_RET_INVALID_PARAMETERS;
			TRACE(PSCI, INFO,
			      "psci_set_suspend_mode - INVALID_PARAMETERS - VM {:d}",
			      current->addrspace->vmid);
		}
		*ret0	= (uint32_t)ret;
		handled = true;
	}

	return handled;
}

error_t
psci_pc_handle_object_activate_vpm_group(vpm_group_t *pg)
{
	spinlock_init(&pg->psci_lock);
	task_queue_init(&pg->psci_virq_task, TASK_QUEUE_CLASS_VPM_GROUP_VIRQ);

	// Default psci mode to be platfom-coordinated
	pg->psci_mode = PSCI_MODE_PC;

	// Initialize vcpus states of the vpm to the deepest suspend state
	// FIXME:
	cpulocal_begin();
	psci_cpu_state_t cpu_state =
		platform_psci_deepest_cpu_state(cpulocal_get_index());
	cpulocal_end();

	vpm_group_suspend_state_t vm_state = vpm_group_suspend_state_default();

	for (cpu_index_t i = 0;
	     i < (PSCI_VCPUS_STATE_BITS / PSCI_VCPUS_STATE_PER_VCPU_BITS);
	     i++) {
		vcpus_state_set(&vm_state, i, cpu_state);
	}

	atomic_store_release(&pg->psci_vm_suspend_state, vm_state);

	return OK;
}

vpm_state_t
vpm_get_state(vpm_group_t *vpm_group)
{
	vpm_state_t vpm_state = VPM_STATE_NO_STATE;

	vpm_group_suspend_state_t vm_state =
		atomic_load_acquire(&vpm_group->psci_vm_suspend_state);

	if (vcpus_state_is_any_awake(vm_state, PLATFORM_MAX_HIERARCHY, 0)) {
		vpm_state = VPM_STATE_RUNNING;
	} else {
		vpm_state = VPM_STATE_CPUS_SUSPENDED;
	}

	return vpm_state;
}

/*
 * this clears the vcpu state for core which has started to boot from
 * hw followed by firmware cluster and suspend states are still
 * cleared by the same in wake-up path by calling into psci_vcpu_wakeup
 */
void
psci_vcpu_clear_vcpu_state(thread_t *thread, cpu_index_t target_cpu)
{
	(void)target_cpu;
	if (thread->vpm_mode != VPM_MODE_PSCI) {
		goto out;
	}

	assert(thread->psci_group != NULL);

	vpm_group_t *vpm_group = thread->psci_group;
	cpu_index_t  vcpu_id   = thread->psci_index;

	thread->psci_suspend_state = psci_suspend_powerstate_default();

	vpm_group_suspend_state_t old_state =
		atomic_load_relaxed(&vpm_group->psci_vm_suspend_state);
	vpm_group_suspend_state_t new_state;

	new_state = old_state;
	vcpus_state_clear(&new_state, vcpu_id);

out:
	// Nothing to do for non PSCI threads
	return;
}

void
psci_vcpu_resume(thread_t *thread)
{
	assert(thread->vpm_mode != VPM_MODE_NONE);

	scheduler_lock_nopreempt(thread);
	psci_vpm_active_vcpus_get(scheduler_get_active_affinity(thread),
				  thread);
	scheduler_unlock_nopreempt(thread);

	if (thread->vpm_mode != VPM_MODE_PSCI) {
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

error_t
psci_vcpu_suspend(thread_t *current)
{
	assert(current->vpm_mode != VPM_MODE_NONE);

	// Decrement refcount of the PCPU
	scheduler_lock_nopreempt(current);
	psci_vpm_active_vcpus_put(scheduler_get_active_affinity(current),
				  current);
	scheduler_unlock_nopreempt(current);

	if (current->vpm_mode != VPM_MODE_PSCI) {
		goto out;
	}

	assert(current->psci_group != NULL);

	vpm_group_t	*vpm_group = current->psci_group;
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

idle_state_t
psci_pc_handle_idle_yield(bool in_idle_thread)
{
	assert_preempt_disabled();

	idle_state_t idle_state = IDLE_STATE_IDLE;

	if (!in_idle_thread) {
		goto out;
	}

	if (rcu_has_pending_updates()) {
		goto out;
	}

	cpu_index_t cpu = cpulocal_get_index();

	// Check if there is any vcpu running in this cpu
	if (!psci_vpm_active_vcpus_is_zero(cpu)) {
		goto out;
	}

	thread_t *vcpu	       = NULL;
	list_t	 *psci_pm_list = psci_pm_list_get_self();
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
