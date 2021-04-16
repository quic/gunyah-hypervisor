// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <cpulocal.h>
#include <ipi.h>
#include <list.h>
#include <spinlock.h>

#include "event_handlers.h"
#include "psci_pm_list.h"

CPULOCAL_DECLARE_STATIC(list_t, vcpu_pm_list);
CPULOCAL_DECLARE_STATIC(spinlock_t, vcpu_pm_list_lock);

void
psci_pm_list_init(void)
{
	for (cpu_index_t cpu = 0U; cpu < PLATFORM_MAX_CORES; cpu++) {
		list_init(&CPULOCAL_BY_INDEX(vcpu_pm_list, cpu));
		spinlock_init(&CPULOCAL_BY_INDEX(vcpu_pm_list_lock, cpu));
	}
}

list_t *
psci_pm_list_get_self(void)
{
	return &CPULOCAL(vcpu_pm_list);
}

void
psci_pm_list_insert(cpu_index_t cpu_index, thread_t *vcpu)
{
	list_t *list = &CPULOCAL_BY_INDEX(vcpu_pm_list, cpu_index);

	spinlock_acquire(&CPULOCAL_BY_INDEX(vcpu_pm_list_lock, cpu_index));
	list_insert_at_tail_release(list, &vcpu->psci_pm_list_node);
	spinlock_release(&CPULOCAL_BY_INDEX(vcpu_pm_list_lock, cpu_index));
}

void
psci_pm_list_delete(cpu_index_t cpu_index, thread_t *vcpu)
{
	list_t *list = &CPULOCAL_BY_INDEX(vcpu_pm_list, cpu_index);

	spinlock_acquire(&CPULOCAL_BY_INDEX(vcpu_pm_list_lock, cpu_index));
	list_delete_node(list, &vcpu->psci_pm_list_node);
	spinlock_release(&CPULOCAL_BY_INDEX(vcpu_pm_list_lock, cpu_index));

	ipi_one_idle(IPI_REASON_IDLE, cpulocal_get_index());
}
