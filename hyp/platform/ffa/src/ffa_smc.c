// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypconstants.h>

#include <atomic.h>
#include <compiler.h>
#include <idle.h>
#include <panic.h>
#include <preempt.h>
#include <scheduler.h>
#include <smccc.h>
#include <thread.h>
#include <util.h>

#if defined(INTERFACE_VCPU)
#include <cpulocal.h>
#include <platform_ipi.h>
#include <vcpu.h>
#endif

#include "event_handlers.h"
#include "ffa_private.h"

static bool
ffa_smccc_is_run(smccc_function_id_t fn_id)
{
	smccc_function_t func = smccc_function_id_get_function(&fn_id);

	return func == FFA_FUNCTION_FFA_RUN;
}

static bool
ffa_smccc_is_direct_req(smccc_function_id_t fn_id)
{
	smccc_function_t func = smccc_function_id_get_function(&fn_id);

	return (func == FFA_FUNCTION_FFA_MSG_SEND_DIRECT_REQ) ||
	       (func == FFA_FUNCTION_FFA_MSG_SEND_DIRECT_REQ2);
}

void
ffa_smccc_call(smccc_function_id_t fn_id,
	       const register_t (*args)[SMCCC_1_2_ARGS],
	       register_t (*ret)[SMCCC_1_2_RETS])
{
	assert(args != NULL);
	assert(ret != NULL);

	// All FF-A ABIs are allocated from the fast call range.
	assert_debug(smccc_function_id_get_is_fast(&fn_id));

	// FF-A function must not be donating CPU cycles.
	assert_debug(!ffa_smccc_is_run(fn_id));
	assert_debug(!ffa_smccc_is_direct_req(fn_id));

	// FF-A calls handled by the SPMC aren't interruptible.
	smccc_1_2_call(fn_id, args, ret, false);
}

static void
ffa_smccc_resuming(smccc_function_id_t fn_id,
		   const register_t (*args)[SMCCC_1_2_ARGS],
		   register_t (*ret)[SMCCC_1_2_RETS])
{
	// Don't set the force interruptible flag for resuming calls. Otherwise
	// if the caller is a VCPU it may be interrupted and the pending wakeup
	// will prevent completion of the call.
	smccc_1_2_call(fn_id, args, ret, false);

	smccc_function_id_t ret_fn_id =
		smccc_function_id_cast((uint32_t)(*ret)[0]);
	smccc_function_t ret_func = smccc_function_id_get_function(&ret_fn_id);

	count_t	      retry_count = 0U;
	const count_t max_retries = 100U;

	while ((ret_func == FFA_FUNCTION_FFA_INTERRUPT) ||
	       (ret_func == FFA_FUNCTION_FFA_YIELD)) {
		retry_count++;
		if (retry_count == max_retries) {
			panic("Reached max retries resuming FF-A call");
		}

		// If callee is blocked allow other threads to run.
		if (ret_func == FFA_FUNCTION_FFA_YIELD) {
			scheduler_yield();
		}

		// Resume the call with target information returned in w1.
		smccc_function_id_t resume_fn_id = smccc_create_fn_id(
			FFA_FUNCTION_FFA_RUN, SMCCC_OWNER_ID_STANDARD, false,
			true);
		register_t resume_args[SMCCC_1_2_ARGS] = {
			[0] = (*ret)[1],
		};

		smccc_1_2_call(resume_fn_id, &resume_args, ret, false);

		ret_fn_id = smccc_function_id_cast((uint32_t)(*ret)[0]);
		ret_func  = smccc_function_id_get_function(&ret_fn_id);
	}
}

static void
ffa_smccc_cpu_cycle(smccc_function_id_t fn_id,
		    const register_t (*args)[SMCCC_1_2_ARGS],
		    register_t (*ret)[SMCCC_1_2_RETS],
		    ffa_partition_id_t src_id, ffa_partition_id_t dest_id,
		    index_result_t vcpu_id)
{
	const ffa_partition_id_t hyp_id =
		ffa_partition_id_cast(FFA_HYP_PARTITION_ID);

	assert(args != NULL);
	assert(ret != NULL);

	// All FF-A ABIs are allocated from the fast call range.
	assert_debug(smccc_function_id_get_is_fast(&fn_id));

	// The CPU cycle donation must be from NS endpoint to secure.
	assert_debug(!ffa_partition_id_get_is_secure(&src_id));
	assert_debug(ffa_partition_id_get_is_secure(&dest_id));

	if (ffa_partition_id_is_equal(src_id, hyp_id)) {
		// Auto-resume the call until it has completed.
		ffa_smccc_resuming(fn_id, args, ret);
	} else {
		smccc_1_2_call(fn_id, args, ret, true);
	}

	// If the SP returned a direct response, check that the returned source
	// and destination IDs align with the original IDs of the request. If
	// there is a mismatch then we must avoid returning the result to the
	// caller to avoid leaking information to the wrong VM.
	smccc_function_id_t ret_fn_id =
		smccc_function_id_cast((uint32_t)(*ret)[0]);
	smccc_function_t ret_func = smccc_function_id_get_function(&ret_fn_id);
	if ((ret_func == FFA_FUNCTION_FFA_MSG_SEND_DIRECT_RESP) ||
	    (ret_func == FFA_FUNCTION_FFA_MSG_SEND_DIRECT_RESP2)) {
		ffa_ep_ids_t ret_ep_ids = ffa_ep_ids_cast((uint32_t)(*ret)[1]);
		ffa_partition_id_t ret_src_id =
			ffa_ep_ids_get_src_id(&ret_ep_ids);
		if (!ffa_partition_id_is_equal(ret_src_id, dest_id)) {
			// We should never get a response from the wrong SP.
			panic("Unexpected src ID in FF-A direct resp");
		}

		ffa_partition_id_t ret_dest_id =
			ffa_ep_ids_get_dest_id(&ret_ep_ids);
		if (!ffa_partition_id_is_equal(ret_dest_id, src_id)) {
			// If no target VCPU was specified then we must have
			// initiated a new direct request; the returned
			// destination ID must match the original source.
			if (vcpu_id.e != OK) {
				panic("Unexpected dest ID in FF-A direct resp");
			}

			// Otherwise we resumed a previous direct request via
			// FFA_RUN which was initiated by another VM and got
			// interrupted or blocked. We could discard the response
			// and return FFA_MSG_WAIT, but this would mean the
			// initiator of the direct request would never get a
			// response. Ideally we should save the response and
			// return it the next time the requesting VM tries to
			// resume the target via FFA_RUN, but we don't currently
			// have the infrastructure to support this.
			// FIXME: QC Gunyah issue #285
			panic("Unhandled FF-A direct resp");
		}
	}
}

void
ffa_smccc_run(smccc_function_id_t fn_id,
	      const register_t (*args)[SMCCC_1_2_ARGS],
	      register_t (*ret)[SMCCC_1_2_RETS], ffa_partition_id_t src_id)
{
	assert_debug(ffa_smccc_is_run(fn_id));

	ffa_target_info_t  target  = ffa_target_info_cast((uint32_t)(*args)[0]);
	ffa_partition_id_t dest_id = ffa_target_info_get_ep_id(&target);
	index_t		   vcpu_id = ffa_target_info_get_vcpu_id(&target);

	ffa_smccc_cpu_cycle(fn_id, args, ret, src_id, dest_id,
			    index_result_ok(vcpu_id));
}

void
ffa_smccc_direct_req(smccc_function_id_t fn_id,
		     const register_t (*args)[SMCCC_1_2_ARGS],
		     register_t (*ret)[SMCCC_1_2_RETS])
{
	assert_debug(ffa_smccc_is_direct_req(fn_id));

	ffa_ep_ids_t	   ep_ids  = ffa_ep_ids_cast((uint32_t)(*args)[0]);
	ffa_partition_id_t src_id  = ffa_ep_ids_get_src_id(&ep_ids);
	ffa_partition_id_t dest_id = ffa_ep_ids_get_dest_id(&ep_ids);

	ffa_smccc_cpu_cycle(fn_id, args, ret, src_id, dest_id,
			    index_result_error(ERROR_ARGUMENT_INVALID));
}

static bool
ffa_handle_call(void) EXCLUDE_PREEMPT_DISABLED
{
	bool	handled;
	count_t num_ret;

	thread_t	   *current = thread_get_self();
	smccc_function_id_t fn_id =
		smccc_function_id_cast((uint32_t)current->vcpu_regs_gpr.x[0]);
	smccc_owner_id_t owner_id   = smccc_function_id_get_owner_id(&fn_id);
	smccc_function_t smccc_func = smccc_function_id_get_function(&fn_id);
	bool		 is_smc64   = smccc_function_id_get_is_smc64(&fn_id);
	uint32_t	 res0	    = smccc_function_id_get_res0(&fn_id);

	if (!smccc_function_id_get_is_fast(&fn_id) ||
	    (owner_id != SMCCC_OWNER_ID_STANDARD)) {
		// FF-A calls are all fast standard secure service calls
		handled = false;
		goto out;
	}

	if ((smccc_func < FFA_FUNCTION_ID_MIN) ||
	    (smccc_func > FFA_FUNCTION_ID_MAX) || (res0 != 0U)) {
		// Not an FF-A call
		handled = false;
		goto out;
	}

	register_t ret[SMCCC_1_2_RETS] = { 0 };
	bool	   is_ffa_version = !is_smc64 &&
				    (smccc_func == FFA_FUNCTION_FFA_VERSION);

	// No FF-A support in TZ?
	if (!ffa_is_active()) {
		if (is_ffa_version) {
			ret[0] = (register_t)SMCCC_UNKNOWN_FUNCTION32;
		} else {
			ffa_set_error(FFA_RET_NOT_SUPPORTED, &ret);
		}
		goto out_not_supported;
	}

	// Before using any FF-A ABI other than FFA_VERSION, the version
	// negotiation must have taken place.
	if (!is_ffa_version) {
		addrspace_t *addrspace = thread_get_self()->addrspace;
		assert(addrspace != NULL);

		ffa_version_t vm_version =
			atomic_load_relaxed(&addrspace->ffa_negotiated_version);

		if (ffa_version_is_equal(vm_version, ffa_version_default())) {
			// An invocation of any FF-A ABI apart from FFA_VERSION
			// completes with the NOT_SUPPORTED error code, if the
			// negotiated version of the caller is the Null
			// version.
			ffa_set_error(FFA_RET_NOT_SUPPORTED, &ret);
			goto out_not_supported;
		}
#if defined(VERBOSE) && VERBOSE
		ffa_version_t hyp_version = ffa_get_hyp_version();
		uint16_t vm_version_minor = ffa_version_get_minor(&vm_version);
		assert((ffa_version_get_major(&vm_version) ==
			ffa_version_get_major(&hyp_version)) &&
		       (vm_version_minor <= HYP_FFA_VERSION_MINOR_MAX) &&
		       (vm_version_minor >= HYP_FFA_VERSION_MINOR_MIN));
#endif
	}

	const register_t(*args)[SMCCC_1_2_ARGS];

	// Start the args array at x1
	args = (const register_t(*)[SMCCC_1_2_ARGS])(
		       void *)&current->vcpu_regs_gpr.x[1];

	if (is_smc64) {
		ffa_call_handle_functions_64(fn_id, args, &ret);
	} else {
		ffa_call_handle_functions_32(fn_id, args, &ret);
	}

out_not_supported:
	// Handled FF-A calls always return a FF-A SMCCC function ID in w0
	// (excluding FFA_VERSION). Based on this we can determine the number of
	// results returned by the call; we should preserve the other registers
	// to remain compliant with SMCCC v1.2. Note that FF-A does break
	// compliance by allowing some SMC32 CPU cycle management calls to
	// return SMC64 results; it is expected that the guest can handle this
	// or uses SMC64 FIDs to avoid this non-compliance.
	if (is_ffa_version) {
		num_ret = SMCCC_1_2_SMC32_RETS;
	} else {
		smccc_function_id_t ret_fn_id =
			smccc_function_id_cast((uint32_t)ret[0]);
		num_ret = smccc_function_id_get_is_smc64(&ret_fn_id)
				  ? SMCCC_1_2_RETS
				  : SMCCC_1_2_SMC32_RETS;
	}

	static_assert(util_array_size(current->vcpu_regs_gpr.x) >=
			      util_array_size(ret),
		      "array size mismatch");

	// SMC32 FF-A ABIs must preserve w8-w17 in addition to w18-w30.
	//
	// Copy only the ABI required (num_ret) result values back to
	// registers, preserving unchanged registers.
	// Note, if the result is a 64-bit function id, we copy all the SMC64
	// rets, ignoring the fact that the original SMC may have been an SMC32
	// as documented in the FF-A specific SMCCC deviation.
	for (index_t i = 0; i < num_ret; i++) {
		current->vcpu_regs_gpr.x[i] = ret[i];
	}

	handled = true;

out:
	return handled;
}

bool
ffa_handle_vcpu_trap_smc64(ESR_EL2_ISS_SMC64_t iss)
{
	bool handled = false;

	if (ESR_EL2_ISS_SMC64_get_imm16(&iss) == (uint16_t)0U) {
		handled = ffa_handle_call();
	}

	return handled;
}

bool
ffa_handle_vcpu_trap_hvc64(ESR_EL2_ISS_HVC_t iss)
{
	bool handled = false;

	if (ESR_EL2_ISS_HVC_get_imm16(&iss) == (uint16_t)0U) {
		handled = ffa_handle_call();
	}

	return handled;
}
