// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <hypregisters.h>

#include <thread.h>

#include <asm/sysregs.h>

#include "event_handlers.h"

void
vcpu_context_switch_cpu_load(void)
{
	thread_t *thread = thread_get_self();

	if (thread->kind == THREAD_KIND_VCPU) {
		register_ACTLR_EL1_write(thread->vcpu_regs_el1.actlr_el1);
		register_AMAIR_EL1_write(thread->vcpu_regs_el1.amair_el1);
		register_AFSR0_EL1_write(thread->vcpu_regs_el1.afsr0_el1);
		register_AFSR1_EL1_write(thread->vcpu_regs_el1.afsr1_el1);
	}
}

void
vcpu_context_switch_cpu_save(void)
{
	thread_t *thread = thread_get_self();

	if (thread->kind == THREAD_KIND_VCPU) {
		thread->vcpu_regs_el1.actlr_el1 = register_ACTLR_EL1_read();
		thread->vcpu_regs_el1.amair_el1 = register_AMAIR_EL1_read();
		thread->vcpu_regs_el1.afsr0_el1 = register_AFSR0_EL1_read();
		thread->vcpu_regs_el1.afsr1_el1 = register_AFSR1_EL1_read();
	}
}
