// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <thread.h>

#include <events/smccc.h>

#include "event_handlers.h"
#include "smccc_hypercall.h"

static bool
smccc_handle_call(bool is_hvc) EXCLUDE_PREEMPT_DISABLED
{
	bool		    handled;
	thread_t	   *current = thread_get_self();
	smccc_function_id_t function_id =
		smccc_function_id_cast((uint32_t)current->vcpu_regs_gpr.x[0]);

	uint32_t res0 = smccc_function_id_get_res0(&function_id);
	if (res0 != 0U) {
		current->vcpu_regs_gpr.x[0] =
			(register_t)SMCCC_UNKNOWN_FUNCTION64;
		handled = true;
		goto out;
	}

	// TODO: the smccc handling below needs to be refactored, to permit
	// registering ranges of service IDs, rather than registering
	// individual calls directly. The current approach allows for unknown
	// call IDs to be unhandled and fallthrough to a later module, which is
	// undesirable.
	//
	// For SMCCC based hypercalls, we need function ID range-base handling,
	// so its currently called directly here.
	if (smccc_handle_hypercall_wrapper(function_id, is_hvc)) {
		handled = true;
		goto out;
	}

	if (smccc_function_id_get_is_smc64(&function_id)) {
		uint64_t ret0 = (uint64_t)current->vcpu_regs_gpr.x[0];
		uint64_t ret1 = (uint64_t)current->vcpu_regs_gpr.x[1];
		uint64_t ret2 = (uint64_t)current->vcpu_regs_gpr.x[2];
		uint64_t ret3 = (uint64_t)current->vcpu_regs_gpr.x[3];

		if (smccc_function_id_get_is_fast(&function_id)) {
			handled = trigger_smccc_dispatch_fast_64_event(
				smccc_function_id_get_owner_id(&function_id),
				smccc_function_id_get_function(&function_id),
				is_hvc, (uint64_t)current->vcpu_regs_gpr.x[1],
				(uint64_t)current->vcpu_regs_gpr.x[2],
				(uint64_t)current->vcpu_regs_gpr.x[3],
				(uint64_t)current->vcpu_regs_gpr.x[4],
				(uint64_t)current->vcpu_regs_gpr.x[5],
				(uint64_t)current->vcpu_regs_gpr.x[6],
				smccc_client_id_cast(
					(uint32_t)current->vcpu_regs_gpr.x[7]),
				&ret0, &ret1, &ret2, &ret3);
		} else {
			handled = trigger_smccc_dispatch_yielding_64_event(
				smccc_function_id_get_owner_id(&function_id),
				smccc_function_id_get_function(&function_id),
				is_hvc, (uint64_t)current->vcpu_regs_gpr.x[1],
				(uint64_t)current->vcpu_regs_gpr.x[2],
				(uint64_t)current->vcpu_regs_gpr.x[3],
				(uint64_t)current->vcpu_regs_gpr.x[4],
				(uint64_t)current->vcpu_regs_gpr.x[5],
				(uint64_t)current->vcpu_regs_gpr.x[6],
				smccc_client_id_cast(
					(uint32_t)current->vcpu_regs_gpr.x[7]),
				&ret0, &ret1, &ret2, &ret3);
		}

		if (handled) {
			current->vcpu_regs_gpr.x[0] = (register_t)ret0;
			current->vcpu_regs_gpr.x[1] = (register_t)ret1;
			current->vcpu_regs_gpr.x[2] = (register_t)ret2;
			current->vcpu_regs_gpr.x[3] = (register_t)ret3;
		}
	} else {
		uint32_t ret0 = (uint32_t)current->vcpu_regs_gpr.x[0];
		uint32_t ret1 = (uint32_t)current->vcpu_regs_gpr.x[1];
		uint32_t ret2 = (uint32_t)current->vcpu_regs_gpr.x[2];
		uint32_t ret3 = (uint32_t)current->vcpu_regs_gpr.x[3];

		if (smccc_function_id_get_is_fast(&function_id)) {
			handled = trigger_smccc_dispatch_fast_32_event(
				smccc_function_id_get_owner_id(&function_id),
				smccc_function_id_get_function(&function_id),
				is_hvc, (uint32_t)current->vcpu_regs_gpr.x[1],
				(uint32_t)current->vcpu_regs_gpr.x[2],
				(uint32_t)current->vcpu_regs_gpr.x[3],
				(uint32_t)current->vcpu_regs_gpr.x[4],
				(uint32_t)current->vcpu_regs_gpr.x[5],
				(uint32_t)current->vcpu_regs_gpr.x[6],
				smccc_client_id_cast(
					(uint32_t)current->vcpu_regs_gpr.x[7]),
				&ret0, &ret1, &ret2, &ret3);
		} else {
			handled = trigger_smccc_dispatch_yielding_32_event(
				smccc_function_id_get_owner_id(&function_id),
				smccc_function_id_get_function(&function_id),
				is_hvc, (uint32_t)current->vcpu_regs_gpr.x[1],
				(uint32_t)current->vcpu_regs_gpr.x[2],
				(uint32_t)current->vcpu_regs_gpr.x[3],
				(uint32_t)current->vcpu_regs_gpr.x[4],
				(uint32_t)current->vcpu_regs_gpr.x[5],
				(uint32_t)current->vcpu_regs_gpr.x[6],
				smccc_client_id_cast(
					(uint32_t)current->vcpu_regs_gpr.x[7]),
				&ret0, &ret1, &ret2, &ret3);
		}

		if (handled) {
			current->vcpu_regs_gpr.x[0] = (register_t)ret0;
			current->vcpu_regs_gpr.x[1] = (register_t)ret1;
			current->vcpu_regs_gpr.x[2] = (register_t)ret2;
			current->vcpu_regs_gpr.x[3] = (register_t)ret3;
		}
	}

out:
	return handled;
}

bool
smccc_handle_vcpu_trap_smc64(ESR_EL2_ISS_SMC64_t iss)
{
	bool handled = false;

	if (ESR_EL2_ISS_SMC64_get_imm16(&iss) == (uint16_t)0U) {
		handled = smccc_handle_call(false);
	}

	return handled;
}

bool
smccc_handle_vcpu_trap_hvc64(ESR_EL2_ISS_HVC_t iss)
{
	bool handled = false;

	if (ESR_EL2_ISS_HVC_get_imm16(&iss) == (uint16_t)0U) {
		handled = smccc_handle_call(true);
	}

	return handled;
}

bool
smccc_handle_vcpu_trap_default(void)
{
	// We always fallback to returning -1, otherwise we'll deliver an
	// exception to the VCPU.
	thread_t *current	    = thread_get_self();
	current->vcpu_regs_gpr.x[0] = (register_t)SMCCC_UNKNOWN_FUNCTION64;

	return true;
}
