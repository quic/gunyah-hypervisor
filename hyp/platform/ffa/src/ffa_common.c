// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <compiler.h>
#include <ffa.h>
#include <list.h>
#include <log.h>
#include <object.h>
#include <smccc.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <util.h>

#include "ffa.h"

// Prepares w0-w1 for the 32-bit ERET conduit
static void
ffa_set_eret32(smccc_function_t function, register_t *ret0, register_t *ret1)
{
	smccc_function_id_t fn_id = smccc_create_fn_id(
		function, SMCCC_OWNER_ID_STANDARD, false, true);

	*ret0 = (register_t)smccc_function_id_raw(fn_id);
	*ret1 = 0U; // MBZ for ERET conduit
}

// Prepares x0-x1 for the 64-bit ERET conduit
static void
ffa_set_eret64(smccc_function_t function, register_t *ret0, register_t *ret1)
{
	smccc_function_id_t fn_id = smccc_create_fn_id(
		function, SMCCC_OWNER_ID_STANDARD, true, true);

	*ret0 = (register_t)smccc_function_id_raw(fn_id);
	*ret1 = 0U; // MBZ for ERET conduit
}

void
ffa_set_error(ffa_ret_t err, register_t (*ret)[SMCCC_1_2_RETS])
{
	ffa_set_eret32((smccc_function_t)FFA_FUNCTION_FFA_ERROR, &(*ret)[0],
		       &(*ret)[1]);
	(*ret)[2] = (register_t)err;
}

void
ffa_success(register_t (*ret)[SMCCC_1_2_RETS])
{
	ffa_set_eret32((smccc_function_t)FFA_FUNCTION_FFA_SUCCESS, &(*ret)[0],
		       &(*ret)[1]);
}

void
ffa_success64(register_t (*ret)[SMCCC_1_2_RETS])
{
	ffa_set_eret64((smccc_function_t)FFA_FUNCTION_FFA_SUCCESS, &(*ret)[0],
		       &(*ret)[1]);
}

// Simple version of FFA version negotiation, that assume we only support a
// single contiguous range of versions with the same major number.
void
ffa_call_handle_ffa_version(const register_t (*args)[SMCCC_1_2_ARGS],
			    register_t (*ret)[SMCCC_1_2_RETS])
	EXCLUDE_PREEMPT_DISABLED
{
	register_t    result;
	ffa_version_t vm_version  = ffa_version_cast((uint32_t)(*args)[0]);
	ffa_version_t hyp_version = ffa_get_hyp_version();

	// We don't implement v1.3 which defines w2 usage. In v1.2 and below
	// it is SBZ and we don't check it. We expect a caller to carefully
	// negotiate or attempt an 'negotiated version` check - which is
	// backwards compatible as it requires w1 to be zero (so that a older
	// FF-A implementation that assumes we are trying to negotiate version
	// Null will return NOT_SUPPORTED).
	static_assert((HYP_FFA_VERSION_MAJOR == 1U) &&
			      (HYP_FFA_VERSION_MINOR_MAX < 3U),
		      "need to implement Version query type support");

	uint16_t vm_version_minor  = ffa_version_get_minor(&vm_version);
	uint16_t hyp_version_minor = ffa_version_get_minor(&hyp_version);

	if (compiler_unexpected(!ffa_version_is_clean(vm_version))) {
		result = (register_t)FFA_RET_NOT_SUPPORTED;
		goto out;
	} else if ((ffa_version_get_major(&vm_version) !=
		    ffa_version_get_major(&hyp_version)) ||
		   (vm_version_minor > HYP_FFA_VERSION_MINOR_MAX) ||
		   (vm_version_minor < HYP_FFA_VERSION_MINOR_MIN)) {
		// The versions are not compatible, we can't return our version
		// since that would imply we are and the caller would assume a
		// version.
		result = (register_t)FFA_RET_NOT_SUPPORTED;
		goto out;
	} else if (vm_version_minor > hyp_version_minor) {
		// Downgrade the VM's negotiated version
		vm_version = hyp_version;
	} else {
		// The versions are compatible
	}

	// We implement partial FF-A v1.3 Alpha version re-negotiation,
	// without the 'in-use' checks here. This is just enough to allow a
	// bootloader and OS implementing the same version to boot.  We just
	// assume that FF-A is not in use by this partition for now.
	addrspace_t *addrspace = thread_get_self()->addrspace;
	assert_debug(addrspace != NULL);

	spinlock_acquire(&addrspace->header.lock);

	ffa_version_t existing_version =
		atomic_load_relaxed(&addrspace->ffa_negotiated_version);
	atomic_store_relaxed(&addrspace->ffa_negotiated_version, vm_version);

	// If VM version is higher than hypervisor then request the VM uses the
	// same version with hypervisor. If VM version is lower than hypervisor
	// the VM can continue with that version.
	result = (register_t)ffa_version_raw(hyp_version);

	assert(!ffa_version_is_equal(vm_version, ffa_version_default()));

	spinlock_release(&addrspace->header.lock);

	// Inform SPs about the new VM.
	if (ffa_version_is_equal(existing_version, ffa_version_default())) {
		ffa_vm_availability(addrspace->vmid, true);
	}
out:
	(*ret)[0] = result;
}

void
ffa_call_handle_ffa_features(smccc_function_id_t fn_id,
			     const register_t (*args)[SMCCC_1_2_ARGS],
			     register_t (*ret)[SMCCC_1_2_RETS])
{
	bool hyp_supported;

	// w1 contains an FF-A Function ID or Feature ID
	smccc_function_id_t function_feature_id =
		smccc_function_id_cast((uint32_t)(*args)[0]);

	// The FAST bit is used to distinguish between Function and Feature ID
	if (smccc_function_id_get_is_fast(&function_feature_id)) {
		// Function ID
		smccc_owner_id_t owner_id =
			smccc_function_id_get_owner_id(&function_feature_id);
		smccc_function_t ffa_func =
			smccc_function_id_get_function(&function_feature_id);
		uint32_t res0 =
			smccc_function_id_get_res0(&function_feature_id);

		if ((owner_id != SMCCC_OWNER_ID_STANDARD) || (res0 != 0U)) {
			hyp_supported = false;
			goto out_support;
		}

		if (ffa_func == FFA_FUNCTION_FFA_RXTX_MAP) {
			ffa_success(ret);
			(*ret)[2] = ffa_rxtx_features_buffer_info_raw(
				ffa_get_rxtx_buffer_sizes());
			// Do not forward the RXTX feature acquisition to the
			// TZ, because VM-to-SPMC RXTX mapping is not supported.
			goto out;
		}

		// If it is supported in the hypervisor, forward the call
		if (smccc_function_id_get_is_smc64(&function_feature_id)) {
			hyp_supported = ffa_call_supported_64(ffa_func);
		} else {
			hyp_supported = ffa_call_supported_32(ffa_func);
		}
	} else {
		// Feature ID
		ffa_func_feature_id_t feature_id =
			ffa_func_feature_id_cast((uint32_t)(*args)[0]);

		if (ffa_func_feature_id_get_mbz(&feature_id) != 0U) {
			hyp_supported = false;
			goto out_support;
		}
		// w2 must be zero
		if ((*args)[1] != 0U) {
			hyp_supported = false;
			goto out_support;
		}

		ffa_feature_id_result_t feature_result =
			ffa_feature_id_cast_safe(
				ffa_func_feature_id_get_id(&feature_id));

		if (feature_result.e != OK) {
			hyp_supported = false;
			goto out_support;
		}

		ffa_feature_id_t feature = feature_result.r;
		switch (feature) {
		case FFA_FEATURE_ID_NOTIFICATION_PENDING_INT:
		case FFA_FEATURE_ID_SCHEDULE_RECEIVER_INT:
			hyp_supported = false;
			break;
		case FFA_FEATURE_ID_MANAGED_EXIT_INT:
		default:
			hyp_supported = false;
			break;
		}
	}

out_support:
	// Assume all Hypervisor supported features are also supported by TZ.
	if (hyp_supported) {
		ffa_smccc_call(fn_id, args, ret);
	} else {
		ffa_set_error(FFA_RET_NOT_SUPPORTED, ret);
	}
out:
	return;
}

void
ffa_call_handle_ffa_id_get(register_t (*ret)[SMCCC_1_2_RETS])
{
	thread_t *current = thread_get_self();

	ffa_success(ret);
	(*ret)[2] = (register_t)ffa_partition_id_raw(
		ffa_vmid_to_partid(current->addrspace->vmid));

	return;
}

static void
ffa_set_part_info(const ffa_component_t *component, register_t (*descriptor)[3],
		  bool			 all_components)
{
	ffa_part_info_t part_info = ffa_part_info_default();
	ffa_part_info_set_part_id(&part_info, component->part_id);
	ffa_part_info_set_exec_context_count(&part_info,
					     component->exec_context_count);
	ffa_part_info_set_properties(&part_info, component->properties);

	(*descriptor)[0] = ffa_part_info_raw(part_info);
	(*descriptor)[1] = all_components ? component->uuid_lo : 0U;
	(*descriptor)[2] = all_components ? component->uuid_hi : 0U;
}

static void
ffa_part_info_process_list_regs(list_t *ffa_components_list,
				register_t (*ret)[SMCCC_1_2_RETS],
				uint64_t uuid_lo, uint64_t uuid_hi,
				index_t start_index, count_t *info_count,
				count_t *full_count)
{
	bool all_components = (uuid_lo == 0U) && (uuid_hi == 0U);

	LIST_FOREACH_CONTAINER_BEGIN(ffa_component_t, ffa_components_list,
				     ffa_component, list_node, component)
		if (!all_components && ((component->uuid_lo != uuid_lo) ||
					(component->uuid_hi != uuid_hi))) {
			continue;
		}

		(*full_count)++;
		if ((start_index >= *full_count) ||
		    (*info_count == HYP_FFA_PARTITION_INFO_MAX_COUNT)) {
			continue;
		}

		// Shift 3 for register 0-2
		index_t i = 3U + (*info_count * 3U);
		// And 3 more for the current partition info
		assert((i + 3U) <= SMCCC_1_2_RETS);

		register_t *address	   = &((*ret)[i]);
		register_t(*descriptor)[3] = (register_t(*)[3])address;
		ffa_set_part_info(component, descriptor, all_components);

		(*info_count)++;
	LIST_FOREACH_CONTAINER_END
}

void
ffa_call_handle_ffa_partition_info_get_regs(
	const register_t (*args)[SMCCC_1_2_ARGS],
	register_t (*ret)[SMCCC_1_2_RETS])
{
	uint64_t uuid_lo     = (uint64_t)(*args)[0];
	uint64_t uuid_hi     = (uint64_t)(*args)[1];
	uint32_t start_index = (uint32_t)((*args)[2] & util_mask(16));
	uint32_t info_tag    = (uint32_t)(((*args)[2] >> 16) & util_mask(16));

	if (info_tag != 0U) {
		ffa_set_error(FFA_RET_RETRY, ret);
		goto out;
	}

	static_assert((3U + (HYP_FFA_PARTITION_INFO_MAX_COUNT * 3U)) <=
			      SMCCC_1_2_RETS,
		      "HYP_FFA_PARTITION_INFO_MAX_COUNT overflows registers");

	count_t info_count = 0U;
	count_t full_count = 0U;

	ffa_part_info_process_list_regs(&ffa_secure_components_list, ret,
					uuid_lo, uuid_hi, start_index,
					&info_count, &full_count);

	if (info_count > 0U) {
		ffa_success64(ret);

		ffa_info_get_regs_metadata_t metadata =
			ffa_info_get_regs_metadata_default();
		ffa_info_get_regs_metadata_set_last_index(&metadata,
							  full_count - 1U);
		ffa_info_get_regs_metadata_set_curr_index(
			&metadata, start_index + info_count - 1U);
		ffa_info_get_regs_metadata_set_info_tag(&metadata, 0U);
		ffa_info_get_regs_metadata_set_info_size(
			&metadata, FFA_COMPONENT_DESCRIPTOR_SIZE);
		(*ret)[2] = ffa_info_get_regs_metadata_raw(metadata);
	} else {
		// UUID not found or invalid start index
		ffa_set_error(FFA_RET_INVALID_PARAMETERS, ret);
	}

out:
	return;
}

static bool
ffa_part_info_process_list_buf(list_t *ffa_components_list, bool all_components,
			       uint64_t uuid_lo, uint64_t uuid_hi,
			       count_t *count, ffa_rxtx_buffer_t *rxtx_buffer,
			       bool count_only)
	REQUIRE_SPINLOCK(rxtx_buffer -> lock)
{
	bool	ret;
	count_t num_components = 0U;

	// 3 registers are used to encode each partition information
	// descriptor
	uint64_t descriptor[3]	 = { 0 };
	size_t	 descriptor_size = sizeof(descriptor);
	size_t	 offset		 = 0UL;

	assert(rxtx_buffer != NULL);

	// Only the RX buffer is used for this call.
	uint8_t *rx_buffer	= ffa_rxtx_buffer_get_rx(rxtx_buffer);
	size_t	 rx_buffer_size = ffa_rxtx_buffer_size(rxtx_buffer);

	LIST_FOREACH_CONTAINER_BEGIN(ffa_component_t, ffa_components_list,
				     ffa_component, list_node, component)
		if (!all_components && ((component->uuid_lo != uuid_lo) ||
					(component->uuid_hi != uuid_hi))) {
			continue;
		}

		if (!count_only) {
			offset = num_components * descriptor_size;

			if ((offset + descriptor_size) > rx_buffer_size) {
				ret = false;
				goto out;
			}

			ffa_set_part_info(component, &descriptor,
					  all_components);

			size_t remaining = rx_buffer_size - offset;
			(void)memscpy((void *)(rx_buffer + offset), remaining,
				      (void *)descriptor, descriptor_size);
		}

		num_components++;
	LIST_FOREACH_CONTAINER_END

	ret = true;
out:
	*count = num_components;
	return ret;
}

void
ffa_call_handle_ffa_partition_info_get(const register_t (*args)[SMCCC_1_2_ARGS],
				       register_t (*ret)[SMCCC_1_2_RETS])
{
	bool succ;
	bool rx_valid;

	count_t count = 0U;

	ffa_rxtx_buffer_t *rxtx_buffer;
	bool		   count_only;

	thread_t *current = thread_get_self();
	assert(current != NULL);
	addrspace_t *addrspace = current->addrspace;
	assert(addrspace != NULL);

	uint64_t uuid_lo = ((*args)[0] & util_mask(32)) | ((*args)[1] << 32);
	uint64_t uuid_hi = ((*args)[2] & util_mask(32)) | ((*args)[3] << 32);
	ffa_part_info_get_flags_t flags = ffa_part_info_get_flags_cast(
		(uint32_t)((*args)[4] & util_mask(32)));

	bool all_components = (uuid_lo == 0U) && (uuid_hi == 0U);

	count_only = ffa_part_info_get_flags_get_count_only(&flags);

	// Get the RX buffer between VM and HYP
	rxtx_buffer = &addrspace->ffa_rxtx_buffer;

	spinlock_acquire(&rxtx_buffer->lock);
	if (ffa_rxtx_buffer_is_valid(rxtx_buffer) &&
	    ffa_rxtx_buffer_hyp_is_rx_owner(rxtx_buffer)) {
		rx_valid = true;
		succ	 = ffa_part_info_process_list_buf(
			&ffa_secure_components_list, all_components, uuid_lo,
			uuid_hi, &count, rxtx_buffer, count_only);
		if (succ && !count_only && (count != 0U)) {
			// Transfer the RX buffer ownership to the caller.
			ffa_rxtx_buffer_set_hyp_is_rx_owner(rxtx_buffer, false);
		}
	} else {
		// No RX buffer available or Hypervisor is not the owner
		rx_valid = false;
		succ	 = false;
	}
	spinlock_release(&rxtx_buffer->lock);

	if (!rx_valid) {
		ffa_set_error(FFA_RET_BUSY, ret);
		goto out;
	}

	if (!succ) {
		ffa_set_error(FFA_RET_NO_MEMORY, ret);
		goto out;
	}

	if ((count == 0U) && !all_components) {
		// Unrecognized UUID
		ffa_set_error(FFA_RET_INVALID_PARAMETERS, ret);
	} else {
		ffa_success(ret);
		(*ret)[2] = count;
		if (count_only) {
			(*ret)[3] = 0U;
		} else {
			// Size of each descriptor is 24 bytes.
			(*ret)[3] = 24U;
		}
	}

out:
	return;
}
