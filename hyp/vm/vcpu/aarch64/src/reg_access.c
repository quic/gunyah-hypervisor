// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <atomic.h>
#include <compiler.h>
#include <object.h>
#include <scheduler.h>
#include <thread.h>
#include <util.h>
#include <vcpu.h>

#include "reg_access.h"

register_t
vcpu_gpr_read(thread_t *thread, uint8_t reg_num)
{
	register_t value;

	assert(reg_num <= 31U);

	if (compiler_expected(reg_num != 31U)) {
		value = thread->vcpu_regs_gpr.x[reg_num];
	} else {
		value = 0;
	}

	return value;
}

void
vcpu_gpr_write(thread_t *thread, uint8_t reg_num, register_t value)
{
	assert(reg_num <= 31U);

	if (compiler_expected(reg_num != 31U)) {
		thread->vcpu_regs_gpr.x[reg_num] = value;
	}
}

error_t
vcpu_register_write(thread_t *vcpu, vcpu_register_set_t register_set,
		    index_t register_index, register_t value)
{
	error_t err;

	if (compiler_expected(vcpu->kind != THREAD_KIND_VCPU)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	scheduler_lock(vcpu);

	thread_state_t state = atomic_load_relaxed(&vcpu->state);
	if ((state != THREAD_STATE_INIT) && (state != THREAD_STATE_READY)) {
		// Thread has been killed or exited
		err = ERROR_OBJECT_STATE;
		goto out_locked;
	}

	if (!scheduler_is_blocked(vcpu, SCHEDULER_BLOCK_VCPU_OFF)) {
		// Not safe to access registers of a runnable VCPU
		err = ERROR_BUSY;
		goto out_locked;
	}

	switch (register_set) {
	case VCPU_REGISTER_SET_X:
		if (register_index < 31U) {
			vcpu_gpr_write(vcpu, (uint8_t)register_index, value);
			err = OK;
		} else {
			err = ERROR_ARGUMENT_INVALID;
		}
		break;
	case VCPU_REGISTER_SET_PC:
#if ARCH_AARCH64_32BIT_EL1
#error alignment check is not correct for AArch32
#endif
		if ((register_index == 0U) && util_is_baligned(value, 4U)) {
			vcpu->vcpu_regs_gpr.pc = ELR_EL2_cast(value);
			err		       = OK;
		} else {
			err = ERROR_ARGUMENT_INVALID;
		}
		break;
	case VCPU_REGISTER_SET_SP_EL:
		if (!util_is_baligned(value, 16U)) {
			err = ERROR_ARGUMENT_INVALID;
		} else if (register_index == 0U) {
			vcpu->vcpu_regs_el1.sp_el0 = SP_EL0_cast(value);
			err			   = OK;
		} else if (register_index == 1U) {
			vcpu->vcpu_regs_el1.sp_el1 = SP_EL1_cast(value);
			err			   = OK;
		} else {
			err = ERROR_ARGUMENT_INVALID;
		}
		break;
	default:
		err = ERROR_ARGUMENT_INVALID;
		break;
	}

out_locked:
	scheduler_unlock(vcpu);
out:
	return err;
}
