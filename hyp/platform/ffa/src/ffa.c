// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypconstants.h>
#include <hypcontainers.h>
#include <hyprights.h>

#include <atomic.h>
#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <ffa.h>
#include <list.h>
#include <log.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <platform_cpu.h>
#include <rcu.h>
#include <smccc.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <util.h>

#include "event_handlers.h"
#include "ffa_private.h"

#if ARCH_AARCH64_32BIT_EL1
#error FF-A not implemented for AArch32 VMs
#endif

static bool	     tz_has_ffa = false;
static ffa_version_t hyp_ffa_version;

list_t	   ffa_secure_components_list;
list_t	   ffa_ns_components_list;
spinlock_t ffa_ns_components_list_lock;

static_assert((HYP_FFA_RXTX_MIN_ALIGNMENT == 0x1000U),
	      "FFA RXTX buffer alignment is not 4k");

static_assert((HYP_FFA_RXTX_MIN_BUFFER_PAGE_COUNT == 1U) &&
		      (HYP_FFA_RXTX_MAX_BUFFER_PAGE_COUNT == 1U),
	      "FFA RXTX size must be 1 page");

ffa_component_t *
ffa_get_secure_component(ffa_partition_id_t part_id)
{
	ffa_component_t *ffa_component = NULL;
	LIST_FOREACH_CONTAINER_BEGIN(ffa_component_t,
				     &ffa_secure_components_list, ffa_component,
				     list_node, component)
		if (ffa_partition_id_is_equal(component->part_id, part_id)) {
			ffa_component = component;
			break;
		}
	LIST_FOREACH_CONTAINER_END

	return ffa_component;
}

void
ffa_set_active(bool active)
{
	tz_has_ffa = active;
}

bool
ffa_is_active(void)
{
	return tz_has_ffa;
}

ffa_version_t
ffa_get_hyp_version(void)
{
	return hyp_ffa_version;
}

void
ffa_set_hyp_version(ffa_version_t version)
{
	hyp_ffa_version = version;
}

bool
ffa_call_supported_32(smccc_function_t function)
{
	bool ret;

	switch (function) {
	case FFA_FUNCTION_FFA_VERSION:
	case FFA_FUNCTION_FFA_FEATURES:
	case FFA_FUNCTION_FFA_RXTX_MAP:
	case FFA_FUNCTION_FFA_RXTX_UNMAP:
	case FFA_FUNCTION_FFA_RX_RELEASE:
	case FFA_FUNCTION_FFA_ID_GET:
	case FFA_FUNCTION_FFA_MSG_SEND_DIRECT_REQ:
	case FFA_FUNCTION_FFA_RUN:
	case FFA_FUNCTION_FFA_SPM_ID_GET:
	case FFA_FUNCTION_FFA_PARTITION_INFO_GET:
	case FFA_FUNCTION_FFA_MEM_SHARE:
	case FFA_FUNCTION_FFA_MEM_LEND:
	case FFA_FUNCTION_FFA_MEM_RECLAIM:
	case FFA_FUNCTION_FFA_MEM_FRAG_TX:
		ret = true;
		break;
	default:
		ret = false;
		break;
	}
	return ret;
}

void
ffa_call_handle_functions_32(smccc_function_id_t fn_id,
			     const register_t (*args)[SMCCC_1_2_ARGS],
			     register_t (*ret)[SMCCC_1_2_RETS])
	EXCLUDE_PREEMPT_DISABLED
{
	smccc_function_t ffa_function = smccc_function_id_get_function(&fn_id);

	switch (ffa_function) {
	case FFA_FUNCTION_FFA_VERSION:
		ffa_call_handle_ffa_version(args, ret);
		break;
	case FFA_FUNCTION_FFA_FEATURES:
		ffa_call_handle_ffa_features(fn_id, args, ret);
		break;
	case FFA_FUNCTION_FFA_RXTX_MAP:
		ffa_call_handle_ffa_rxtx_map(fn_id, args, ret);
		break;
	case FFA_FUNCTION_FFA_RXTX_UNMAP:
		ffa_call_handle_ffa_rxtx_unmap(fn_id, args, ret);
		break;
	case FFA_FUNCTION_FFA_RX_RELEASE:
		ffa_call_handle_ffa_rx_release(fn_id, args, ret);
		break;
	case FFA_FUNCTION_FFA_MEM_FRAG_TX:
		ffa_call_handle_ffa_mem_frag_tx(fn_id, args, ret);
		break;
	case FFA_FUNCTION_FFA_ID_GET:
		ffa_call_handle_ffa_id_get(ret);
		break;
	case FFA_FUNCTION_FFA_MSG_SEND_DIRECT_REQ:
		ffa_call_handle_ffa_msg_send_direct(fn_id, args, ret);
		break;
	case FFA_FUNCTION_FFA_RUN:
		ffa_call_handle_ffa_run(fn_id, args, ret);
		break;
	case FFA_FUNCTION_FFA_SPM_ID_GET:
		// This call gets forwarded to TZ without any modification
		ffa_smccc_call(fn_id, args, ret);
		break;
	case FFA_FUNCTION_FFA_PARTITION_INFO_GET:
		ffa_call_handle_ffa_partition_info_get(args, ret);
		break;
	case FFA_FUNCTION_FFA_MEM_SHARE:
	case FFA_FUNCTION_FFA_MEM_LEND:
		ffa_call_handle_ffa_mem_lend_share(fn_id, args, ret);
		break;
	case FFA_FUNCTION_FFA_MEM_RECLAIM:
		ffa_call_handle_ffa_mem_reclaim(fn_id, args, ret);
		break;
	default:
		// NOT SUPPORTED
		ffa_set_error(FFA_RET_NOT_SUPPORTED, ret);
		break;
	}
}

bool
ffa_call_supported_64(smccc_function_t function)
{
	bool ret;

	switch (function) {
	case FFA_FUNCTION_FFA_RXTX_MAP:
	case FFA_FUNCTION_FFA_MSG_SEND_DIRECT_REQ:
	case FFA_FUNCTION_FFA_MSG_SEND_DIRECT_REQ2:
	case FFA_FUNCTION_FFA_RUN:
	case FFA_FUNCTION_FFA_PARTITION_INFO_GET_REGS:
	case FFA_FUNCTION_FFA_MEM_SHARE:
	case FFA_FUNCTION_FFA_MEM_LEND:
		ret = true;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

void
ffa_call_handle_functions_64(smccc_function_id_t fn_id,
			     const register_t (*args)[SMCCC_1_2_ARGS],
			     register_t (*ret)[SMCCC_1_2_RETS])
	EXCLUDE_PREEMPT_DISABLED
{
	smccc_function_t ffa_function = smccc_function_id_get_function(&fn_id);

	switch (ffa_function) {
	case FFA_FUNCTION_FFA_RXTX_MAP:
		ffa_call_handle_ffa_rxtx_map(fn_id, args, ret);
		break;
	case FFA_FUNCTION_FFA_MSG_SEND_DIRECT_REQ:
		ffa_call_handle_ffa_msg_send_direct(fn_id, args, ret);
		break;
	case FFA_FUNCTION_FFA_MSG_SEND_DIRECT_REQ2:
		ffa_call_handle_ffa_msg_send_direct(fn_id, args, ret);
		break;
	case FFA_FUNCTION_FFA_RUN:
		ffa_call_handle_ffa_run(fn_id, args, ret);
		break;
	case FFA_FUNCTION_FFA_PARTITION_INFO_GET_REGS:
		ffa_call_handle_ffa_partition_info_get_regs(args, ret);
		break;
	case FFA_FUNCTION_FFA_MEM_SHARE:
	case FFA_FUNCTION_FFA_MEM_LEND:
		ffa_call_handle_ffa_mem_lend_share(fn_id, args, ret);
		break;
	default:
		// NOT SUPPORTED
		ffa_set_error(FFA_RET_NOT_SUPPORTED, ret);
		break;
	}
}

ffa_partition_id_t
ffa_vmid_to_partid(vmid_t vmid)
{
	// VMID is used as partition ID
	// We can only fit the first 15 bits of VMID
	assert(vmid < util_bit(FFA_PARTITION_ID_PART_ID_BITS));

	ffa_partition_id_t part_id = ffa_partition_id_default();
	ffa_partition_id_set_part_id(&part_id, vmid);
	return part_id;
}

vmid_t
ffa_partid_to_vmid(ffa_partition_id_t part_id)
{
	return ffa_partition_id_get_part_id(&part_id);
}

error_t
ffa_vm_destruction(addrspace_t *addrspace)
{
	error_t ret;
	assert(addrspace != NULL);

	spinlock_acquire(&addrspace->header.lock);

	ffa_version_t default_version = ffa_version_default();
	ffa_version_t old_version =
		atomic_load_relaxed(&addrspace->ffa_negotiated_version);
	atomic_store_relaxed(&addrspace->ffa_negotiated_version,
			     default_version);

	spinlock_release(&addrspace->header.lock);

	// If the current FFA version is default, it is either because
	// the VM does not use FF-A APIs, or because the VM FF-A cleanup
	// has been completed.
	if (ffa_version_is_equal(old_version, default_version)) {
		ret = OK;
		goto out;
	}

	// The buffer can be safely freed here,
	// i.e., the buffer must not be in use.
	ret = ffa_free_vm_rxtx_buffer(addrspace, true);

	if (ret == OK) {
		// Do cleanup for subsystem, e.g. memory sharing, rxtx
		// buffer, notification, etc.
		ffa_vm_availability(addrspace->vmid, false);

		// Cleanup memory sharing
		ret = ffa_memory_cleanup(addrspace);
	}

out:
	return ret;
}

error_t
ffa_shutdown_and_cleanup(cap_id_t addrspace_cap) EXCLUDE_PREEMPT_DISABLED
{
	error_t ret;

	if (!ffa_is_active()) {
		ret = OK;
		goto out;
	}

	addrspace_ptr_result_t lookup = cspace_lookup_addrspace(
		cspace_get_self(), addrspace_cap, CAP_RIGHTS_ADDRSPACE_ATTACH);
	if (compiler_unexpected(lookup.e != OK)) {
		ret = lookup.e;
		goto out;
	}

	addrspace_t *addrspace = lookup.r;

	ret = ffa_vm_destruction(addrspace);

	if (ret == OK) {
		// We need RCU sync to complete memextent deletion
		rcu_sync();
	}

	object_put_addrspace(addrspace);
out:
	return ret;
}
