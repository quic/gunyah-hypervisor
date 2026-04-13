// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <compiler.h>
#include <list.h>
#include <log.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <platform_timer.h>
#include <preempt.h>
#include <smccc.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <util.h>

#include "event_handlers.h"
#include "ffa_private.h"

static uint64_t
ffa_availability_msg(ffa_partition_id_t vm_part_id,
		     ffa_partition_id_t sp_part_id, ffa_msg_flags_t request)
	EXCLUDE_PREEMPT_DISABLED
{
	smccc_function_id_t fn_id = smccc_create_fn_id(
		(smccc_function_t)FFA_FUNCTION_FFA_MSG_SEND_DIRECT_REQ,
		SMCCC_OWNER_ID_STANDARD, false, true);
	smccc_function_id_t fn_resp_id = smccc_create_fn_id(
		(smccc_function_t)FFA_FUNCTION_FFA_MSG_SEND_DIRECT_RESP,
		SMCCC_OWNER_ID_STANDARD, false, true);

	register_t args[SMCCC_1_2_ARGS] = { 0 };
	register_t ret[SMCCC_1_2_RETS];

	ffa_ep_ids_t msg_ids_w1 = ffa_ep_ids_default();
	ffa_ep_ids_t msg_ids_w5 = ffa_ep_ids_default();

	// It has to be a framework message.
	assert(ffa_msg_flags_get_framework_msg(&request));
	// Ensure we only pass requests, not ACKs
	ffa_msg_framework_types_t framework_type =
		ffa_msg_flags_get_framework_type(&request);
	assert(((uint32_t)framework_type & 1U) == 0U);

	ticks_t start_ticks   = platform_timer_get_current_ticks_sync();
	ticks_t timeout_ticks = platform_timer_convert_ms_to_ticks(
		HYP_FFA_VM_AVAILABILITY_RETRY_TIMEOUT_MS);
	ticks_t end_ticks = start_ticks + timeout_ticks;

	assert_debug(start_ticks < end_ticks);

	ffa_partition_id_t hyp_id = ffa_partition_id_cast(FFA_HYP_PARTITION_ID);
	ffa_ep_ids_set_src_id(&msg_ids_w1, hyp_id);
	ffa_ep_ids_set_dest_id(&msg_ids_w1, sp_part_id);

	ffa_ep_ids_set_dest_id(&msg_ids_w5, vm_part_id);

	args[0] = ffa_ep_ids_raw(msg_ids_w1);
	args[1] = ffa_msg_flags_raw(request);
	args[2] = FFA_INVALID_HANDLE;
	args[3] = FFA_INVALID_HANDLE;
	args[4] = ffa_ep_ids_raw(msg_ids_w5);

	assert_preempt_enabled();
	do {
		ffa_smccc_direct_req(fn_id, &args, &ret);

		if (((uint32_t)ret[0] != smccc_function_id_raw(fn_resp_id)) ||
		    ((ret[2] ^ 1U) != ffa_msg_flags_raw(request)) ||
		    ((int32_t)ret[3] == (int32_t)FFA_RET_INVALID_PARAMETERS) ||
		    ((int32_t)ret[3] == (int32_t)FFA_RET_DENIED)) {
			panic("FFA: Unexpected response from TZ");
		}

		// Retry if TZ is busy
	} while ((((int32_t)ret[3] == (int32_t)FFA_RET_RETRY) ||
		  ((int32_t)ret[3] == (int32_t)FFA_RET_INTERRUPTED)) &&
		 (platform_timer_get_current_ticks_sync() <= end_ticks));

	return ret[3];
}

void
ffa_vm_availability(vmid_t vmid, bool is_create) EXCLUDE_PREEMPT_DISABLED
{
	assert((vmid != 0U) && (vmid < ADDRSPACE_NUM_VMIDS));

	ffa_partition_id_t part_id = ffa_vmid_to_partid(vmid);

	ffa_msg_flags_t request = ffa_msg_flags_default();
	ffa_msg_flags_set_framework_msg(&request, true);

	// Iterate through all the existing Secure components and inform the
	// ones that want to know about VM creations/destructions.
	//
	// Since the SPs list remains unchanged, there is no need to lock the
	// list.
	LIST_FOREACH_CONTAINER_BEGIN(ffa_component_t,
				     &ffa_secure_components_list, ffa_component,
				     list_node, component)
		if (is_create && ffa_part_prop_desc_get_inform_vm_created(
					 &component->properties)) {
			ffa_msg_flags_set_framework_type(
				&request, FFA_MSG_FRAMEWORK_TYPES_VM_CREATION);
		} else if (!is_create &&
			   ffa_part_prop_desc_get_inform_vm_destroyed(
				   &component->properties)) {
			ffa_msg_flags_set_framework_type(
				&request, FFA_MSG_FRAMEWORK_TYPES_VM_DESTROY);
		} else {
			// Nothing to do
			continue;
		}

		uint64_t result = ffa_availability_msg(
			part_id, component->part_id, request);
		if ((int32_t)result != (int32_t)FFA_RET_SUCCESS) {
			LOG(ERROR, WARN,
			    "FF-A: VM {:d} availability failed {:#x}", vmid,
			    result);
		}
	LIST_FOREACH_CONTAINER_END
}

error_t
ffa_handle_object_activate_addrspace(addrspace_t *addrspace)
	EXCLUDE_PREEMPT_DISABLED
{
	error_t ret;
	assert_debug(addrspace != NULL);

	atomic_store_relaxed(&addrspace->ffa_negotiated_version,
			     ffa_version_default());

	spinlock_init(&addrspace->ffa_rxtx_buffer.lock);

	ret = OK;
	return ret;
}

void
ffa_handle_object_deactivate_addrspace(addrspace_t *addrspace)
	EXCLUDE_PREEMPT_DISABLED
{
	assert_debug(addrspace != NULL);

	if (!ffa_is_active()) {
		goto out;
	}

	if (ffa_vm_destruction(addrspace) != OK) {
		panic("FFA: addrspace not clean");
	}

	// Remove all the VM's components from the list
	spinlock_acquire(&ffa_ns_components_list_lock);
	LIST_FOREACH_CONTAINER_BEGIN(ffa_component_t, &ffa_ns_components_list,
				     ffa_component, list_node, component)
		if (component->addrspace == addrspace) {
			(void)list_delete_node(&ffa_ns_components_list,
					       &component->list_node);
			partition_free(addrspace->header.partition, component,
				       sizeof(ffa_component_t));
		}
	LIST_FOREACH_CONTAINER_END
	spinlock_release(&ffa_ns_components_list_lock);

out:
	return;
}
