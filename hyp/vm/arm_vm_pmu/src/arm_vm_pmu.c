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
