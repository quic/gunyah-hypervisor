// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <irq.h>
#include <timer_queue.h>
#include <vic.h>
#include <virq.h>

#include <asm/barrier.h>

#include "arm_vm_pmu.h"
#include "event_handlers.h"

error_t
arm_vm_pmu_handle_object_create_thread(thread_create_t thread_create)
{
	thread_t *thread = thread_create.thread;
	assert(thread != NULL);

	// A PMU object is considered "active" if the VM is allowed access to
	// PMU and has already accessed it.
	// For now we activate it for all vCPUs and don't trap its registers.
	if (thread->kind == THREAD_KIND_VCPU) {
		thread->pmu.is_active = true;
	} else {
		thread->pmu.is_active = false;
	}

	return OK;
}

error_t
arm_vm_pmu_handle_object_activate_thread(thread_t *thread)
{
	error_t ret = OK;

	if (thread->kind == THREAD_KIND_VCPU) {
		ret = vic_bind_private_vcpu(&thread->pmu.pmu_virq_src, thread,
					    PLATFORM_VM_PMU_IRQ,
					    VIRQ_TRIGGER_PMU);
	}

	return ret;
}

void
arm_vm_pmu_handle_object_deactivate_thread(thread_t *thread)
{
	if (thread->kind == THREAD_KIND_VCPU) {
		vic_unbind(&thread->pmu.pmu_virq_src);
	}
}
