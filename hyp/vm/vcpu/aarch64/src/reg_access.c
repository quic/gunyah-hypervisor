// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <compiler.h>
#include <object.h>
#include <thread.h>
#include <vcpu.h>

register_t
vcpu_gpr_read(thread_t *thread, uint8_t reg_num)
{
	register_t value;

	assert(reg_num <= 31);

	if (compiler_expected(reg_num != 31)) {
		value = thread->vcpu_regs_gpr.x[reg_num];
	} else {
		value = 0;
	}

	return value;
}

void
vcpu_gpr_write(thread_t *thread, uint8_t reg_num, register_t value)
{
	assert(reg_num <= 31);

	if (compiler_expected(reg_num != 31)) {
		thread->vcpu_regs_gpr.x[reg_num] = value;
	}
}
