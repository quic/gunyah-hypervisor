// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <cpulocal.h>
#include <ipi.h>
#include <irq.h>
#include <rcu.h>
#include <scheduler.h>
#include <vcpu.h>

bool
irq_interrupt_dispatch(void)
{
	rcu_read_start();
	thread_t *primary_vcpu =
		scheduler_get_primary_vcpu(cpulocal_get_index());
	scheduler_lock(primary_vcpu);
	vcpu_wakeup(primary_vcpu);
	scheduler_unlock(primary_vcpu);
	rcu_read_finish();
	return ipi_handle_relaxed();
}
