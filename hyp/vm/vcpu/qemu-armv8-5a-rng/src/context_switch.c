// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <thread.h>

#include <asm/sysregs.h>

#include "event_handlers.h"

// There are no implementation specific EL1 registers to switch for QEMU.

void
vcpu_context_switch_cpu_load(void)
{
	thread_t *thread = thread_get_self();

	if (compiler_expected(thread->kind == THREAD_KIND_VCPU)) {
		// No-op
	}
}

void
vcpu_context_switch_cpu_save(void)
{
	thread_t *thread = thread_get_self();

	if (compiler_expected(thread->kind == THREAD_KIND_VCPU)) {
		// No-op
	}
}
