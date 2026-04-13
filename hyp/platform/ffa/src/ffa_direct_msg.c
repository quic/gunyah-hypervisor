// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <thread.h>
#include <util.h>

#include "ffa_private.h"

// Direct messaging API

void
ffa_call_handle_ffa_msg_send_direct(smccc_function_id_t fn_id,
				    const register_t (*args)[SMCCC_1_2_ARGS],
				    register_t (*ret)[SMCCC_1_2_RETS])
{
	thread_t *current = thread_get_self();

	ffa_partition_id_t vm_partid =
		ffa_vmid_to_partid(current->addrspace->vmid);
	assert_debug(!ffa_partition_id_get_is_secure(&vm_partid));

	ffa_ep_ids_t ffa_ep_ids = ffa_ep_ids_cast((uint32_t)(*args)[0]);

	ffa_partition_id_t receiver_id = ffa_ep_ids_get_dest_id(&ffa_ep_ids);
	ffa_partition_id_t sender_id   = ffa_ep_ids_get_src_id(&ffa_ep_ids);

	if (ffa_partition_id_is_equal(vm_partid, sender_id) &&
	    ffa_partition_id_get_is_secure(&receiver_id)) {
		// Simply forward to TZ for now.
		ffa_smccc_direct_req(fn_id, args, ret);
	} else {
		// Invalid sender or receiver ID.
		ffa_set_error(FFA_RET_INVALID_PARAMETERS, ret);
	}
}

void
ffa_call_handle_ffa_run(smccc_function_id_t fn_id,
			const register_t (*args)[SMCCC_1_2_ARGS],
			register_t (*ret)[SMCCC_1_2_RETS])
{
	thread_t *current = thread_get_self();

	ffa_partition_id_t vm_partid =
		ffa_vmid_to_partid(current->addrspace->vmid);
	assert_debug(!ffa_partition_id_get_is_secure(&vm_partid));

	// The FF-A spec does not define how VCPU IDs should be allocated for an
	// endpoint, aside from them being unique for each execution context.
	// Ignore the VCPU ID for now and let the SPMC validate it.
	ffa_target_info_t  target = ffa_target_info_cast((uint32_t)(*args)[0]);
	ffa_partition_id_t ep_id  = ffa_target_info_get_ep_id(&target);

	if (ffa_partition_id_get_is_secure(&ep_id)) {
		// Simply forward to TZ for now.
		ffa_smccc_run(fn_id, args, ret, vm_partid);
	} else {
		// Invalid target endpoint ID.
		ffa_set_error(FFA_RET_INVALID_PARAMETERS, ret);
	}
}
