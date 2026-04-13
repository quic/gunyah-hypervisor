// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypconstants.h>
#include <hyprights.h>

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
#include <smccc.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <util.h>

#include "event_handlers.h"
#include "ffa_private.h"

static ffa_rxtx_features_buffer_info_t ffa_rxtx_features_buffer_info;
static size_t			       ffa_rxtx_max_buffer_size;
static size_t			       ffa_rxtx_min_alignment;

// Add the hypervisor to the list of available FF-A components
static void
ffa_add_hypervisor(void)
{
	partition_t *hyp_partition = partition_get_private();

	void_ptr_result_t alloc_r = partition_alloc(hyp_partition,
						    sizeof(ffa_component_t),
						    alignof(ffa_component_t));
	if (alloc_r.e != OK) {
		panic("FF-A: Memory allocation failed");
	}
	ffa_component_t *component = (ffa_component_t *)alloc_r.r;

	component->part_id = ffa_partition_id_cast(FFA_HYP_PARTITION_ID);
	component->exec_context_count = platform_get_existing_cpus_count();
	component->uuid_lo	      = HYP_FFA_UUID_LO;
	component->uuid_hi	      = HYP_FFA_UUID_HI;

	ffa_part_prop_desc_t props = ffa_part_prop_desc_default();
	ffa_part_prop_desc_set_is_aarch64(&props, true);
	ffa_part_prop_desc_set_can_receive_direct_req(&props, true);
	ffa_part_prop_desc_set_can_send_direct_req(&props, true);
	ffa_part_prop_desc_set_can_receive_direct_req2(&props, true);
	ffa_part_prop_desc_set_can_send_direct_req2(&props, true);
	ffa_part_prop_desc_set_smc64_cpu_cycle_support(&props, true);
	component->properties = props;

	// No need to lock the list, since this is only done at cold boot
	list_insert_at_tail(&ffa_ns_components_list, &component->list_node);
}

// Gets the details of all the available SPs
static void
ffa_get_sp_info(void)
{
	partition_t *hyp_partition = partition_get_private();

	smccc_function_id_t fn_id = smccc_create_fn_id(
		(smccc_function_t)FFA_FUNCTION_FFA_PARTITION_INFO_GET_REGS,
		SMCCC_OWNER_ID_STANDARD, true, true);

	index_t	   num_entries = 0U;
	index_t	   start_index = 0U;
	register_t info_tag    = 0U;

	register_t ret[SMCCC_1_2_RETS];
	register_t(*args)[SMCCC_1_2_ARGS] =
		(register_t(*)[SMCCC_1_2_ARGS])(void *)&ret[1];

	do {
		// Zero initialize all arguments for each call
		(void)memset_s(args, sizeof(*args), 0, sizeof(*args));
		// UUID = 0 (request all components)
		// (*args)[0, 1] = { UUID_Lo, UUID_Hi }

		if (start_index >= UINT16_MAX) {
			panic("FF-A: Too many partitions");
		}

		ffa_index_tag_t ffa_index_tag = ffa_index_tag_default();
		ffa_index_tag_set_index(&ffa_index_tag, start_index);
		ffa_index_tag_set_tag(&ffa_index_tag, info_tag);
		(*args)[2] = ffa_index_tag_raw(ffa_index_tag);

		ffa_smccc_call(fn_id, args, &ret);

		smccc_function_id_t fn_id_ret =
			smccc_function_id_cast((uint32_t)ret[0]);
		smccc_function_t function_ret =
			smccc_function_id_get_function(&fn_id_ret);

		if (function_ret != FFA_FUNCTION_FFA_SUCCESS) {
			assert_debug(function_ret == FFA_FUNCTION_FFA_ERROR);
			int32_t error_code = (int32_t)ret[2];
			if (start_index == 0U) {
				if (error_code ==
				    (int32_t)FFA_RET_INVALID_PARAMETERS) {
					// No partitions found
					goto out;
				} else if (error_code ==
					   (int32_t)FFA_RET_NOT_SUPPORTED) {
					LOG(ERROR, WARN,
					    "FF-A: FFA_PARTITION_INFO_GET_REGS not supported");
					goto out;
				} else {
					// Unknown error
				}
			}
			panic("FFA_PARTITION_INFO_GET_REGS failed");
		}

		// Parse the metadata returned
		ffa_info_get_regs_metadata_t metadata =
			ffa_info_get_regs_metadata_cast(ret[2]);

		// Capture the last index
		if (start_index == 0U) {
			num_entries = ffa_info_get_regs_metadata_get_last_index(
					      &metadata) +
				      1U;
		}

		// FF-A v1.2 expected size is 24 bytes
		const count_t info_size	    = 24U / sizeof(register_t);
		const count_t max_info_regs = 15U; // x3..x17
		const count_t max_descs	    = max_info_regs / info_size;

		count_t curr_index =
			ffa_info_get_regs_metadata_get_curr_index(&metadata);
		if ((curr_index < start_index) ||
		    ((curr_index - start_index) >= max_descs)) {
			panic("FF-A: invalid curr_index from SPMC");
		}

		count_t count = (curr_index - start_index) + 1U;

		info_tag = ffa_info_get_regs_metadata_get_info_tag(&metadata);

		// According to the v1.2 standard, the size of the partition
		// information entry descriptor should be 24 bytes, returned in
		// 3 registers.
		if (ffa_info_get_regs_metadata_get_info_size(&metadata) !=
		    (sizeof(register_t) * info_size)) {
			panic("FF-A: invalid info_size from SPMC");
		}

		// Add the components to the list
		for (count_t i = 0; i < count; i++) {
			index_t offset = (i * info_size) + 3U;
			assert_debug((offset + 2U) < util_array_size(ret));

			ffa_part_info_t part_info =
				ffa_part_info_cast(ret[offset + 0U]);

			// Ensure it doesn't exceed the existing core count
			count_t exec_context_count =
				ffa_part_info_get_exec_context_count(
					&part_info);
			if (exec_context_count > PLATFORM_MAX_CORES) {
				panic("FF-A: Too many cores");
			}

			// Each component is encoded in three registers
			void_ptr_result_t alloc_r = partition_alloc(
				hyp_partition, sizeof(ffa_component_t),
				alignof(ffa_component_t));
			if (alloc_r.e != OK) {
				panic("FF-A: Memory allocation failed");
			}

			ffa_component_t *component =
				(ffa_component_t *)alloc_r.r;
			component->part_id =
				ffa_part_info_get_part_id(&part_info);
			component->exec_context_count = exec_context_count;
			component->properties =
				ffa_part_info_get_properties(&part_info);
			component->uuid_lo = ret[offset + 1U];
			component->uuid_hi = ret[offset + 2U];

			// Not applicable to SPs
			component->addrspace = NULL;

			// No need to lock the list, since this is only done at
			// cold boot
			list_insert_at_tail(&ffa_secure_components_list,
					    &component->list_node);
		}

		start_index += count;
	} while (start_index < num_entries);

out:
	LOG(DEBUG, INFO, "FF-A: Found {:d} secure components", num_entries);
	return;
}

static bool
ffa_rxtx_set_property(ffa_rxtx_features_buffer_info_t *rxtx_buffer_sizes,
		      size_t *rxtx_min_alignment, size_t *rxtx_max_buffer_size)
{
	bool succ;

	assert(rxtx_buffer_sizes != NULL);
	assert(rxtx_min_alignment != NULL);
	assert(rxtx_max_buffer_size != NULL);

	ffa_rxtx_features_alignment_t min_alignment =
		ffa_rxtx_features_buffer_info_get_min_alignment(
			rxtx_buffer_sizes);

	size_t min_buffer_size_align;
	switch (min_alignment) {
	case FFA_RXTX_FEATURES_ALIGNMENT_SIZE_4K:
		min_buffer_size_align = 0x1000U;
		break;
	case FFA_RXTX_FEATURES_ALIGNMENT_SIZE_16K:
		min_buffer_size_align = 0x4000U;
		break;
	case FFA_RXTX_FEATURES_ALIGNMENT_SIZE_64K:
		min_buffer_size_align = 0x10000U;
		break;
	default:
		min_buffer_size_align = 0U;
		break;
	}

	if (min_buffer_size_align == 0U) {
		succ = false;
		LOG(ERROR, WARN, "FF-A: RXTX invalid min_alignment code");
		goto out;
	}

	size_t max_buffer_size =
		(size_t)ffa_rxtx_features_buffer_info_get_max_num_pages(
			rxtx_buffer_sizes) *
		4096U;

	if ((max_buffer_size > 0U) && (max_buffer_size < HYP_FFA_PAGE_SIZE)) {
		LOG(ERROR, WARN, "FF-A: RXTX max buffer size is small");
		succ = false;
		goto out;
	}

	if (min_buffer_size_align < HYP_FFA_RXTX_MIN_ALIGNMENT) {
		min_buffer_size_align = HYP_FFA_RXTX_MIN_ALIGNMENT;
		// Update rxtx_buffer_sizes
		switch (min_buffer_size_align) {
		case 0x1000U:
			min_alignment = FFA_RXTX_FEATURES_ALIGNMENT_SIZE_4K;
			break;
		case 0x4000U:
			min_alignment = FFA_RXTX_FEATURES_ALIGNMENT_SIZE_16K;
			break;
		case 0x10000U:
			min_alignment = FFA_RXTX_FEATURES_ALIGNMENT_SIZE_64K;
			break;
		default:
			panic("FF-A: invalid HYP_FFA_RXTX_MIN_ALIGNMENT");
		}

		ffa_rxtx_features_buffer_info_set_min_alignment(
			rxtx_buffer_sizes, min_alignment);
	} else if (min_buffer_size_align > HYP_FFA_RXTX_MIN_ALIGNMENT) {
		// Currently we assume and require VM, hypervisor, TZ use the
		// same page sizes for the RXTX buffer.
		LOG(ERROR, WARN, "FF-A: RXTX min buf not supported");
		succ = false;
		goto out;
	} else {
		// Size is as expected.
	}

	if ((max_buffer_size > HYP_FFA_RXTX_MAX_BUFFER_SIZE) ||
	    (max_buffer_size == 0U)) {
		// Reduce the maximum size
		max_buffer_size = HYP_FFA_RXTX_MAX_BUFFER_SIZE;

		ffa_rxtx_features_buffer_info_set_max_num_pages(
			rxtx_buffer_sizes, HYP_FFA_RXTX_MAX_BUFFER_PAGE_COUNT);
	}

	*rxtx_min_alignment   = min_buffer_size_align;
	*rxtx_max_buffer_size = max_buffer_size;

	succ = true;
out:
	return succ;
}

// Check if the query function is supported
static void
ffa_hyp_feature_query(smccc_function_t query_function,
		      register_t (*args)[SMCCC_1_2_ARGS],
		      register_t (*ret)[SMCCC_1_2_RETS])
{
	bool is_query_smc64;

	// If an interface defines both SMC32 and SMC64 function IDs,
	// then either function ID could be used.
	if (ffa_call_supported_32((smccc_function_t)query_function)) {
		is_query_smc64 = false;
	} else if (ffa_call_supported_64((smccc_function_t)query_function)) {
		is_query_smc64 = true;
	} else {
		ffa_set_error(FFA_RET_NOT_SUPPORTED, ret);
		goto out;
	}

	smccc_function_id_t query_fn_id = smccc_create_fn_id(
		(smccc_function_t)query_function, SMCCC_OWNER_ID_STANDARD,
		is_query_smc64, true);

	smccc_function_id_t fn_id =
		smccc_create_fn_id(FFA_FUNCTION_FFA_FEATURES,
				   SMCCC_OWNER_ID_STANDARD, false, true);

	(*args)[0] = smccc_function_id_raw(query_fn_id);
	ffa_smccc_call(fn_id, args, ret);
out:
	return;
}

void
ffa_handle_boot_cold_init(void)
{
	ffa_version_t ffa_hyp_version = ffa_version_default();

	ffa_version_set_major(&ffa_hyp_version, HYP_FFA_VERSION_MAJOR);
	ffa_version_set_minor(&ffa_hyp_version, HYP_FFA_VERSION_MINOR_MAX);
	ffa_set_hyp_version(ffa_hyp_version);

	smccc_function_id_t fn_id =
		smccc_create_fn_id((smccc_function_t)FFA_FUNCTION_FFA_VERSION,
				   SMCCC_OWNER_ID_STANDARD, false, true);

	uint64_t hyp_args[6] = { 0U };
	uint64_t hyp_ret[4]  = { 0U };
	hyp_args[0]	     = ffa_version_raw(ffa_hyp_version);
	smccc_1_1_call(fn_id, &hyp_args, &hyp_ret, NULL, CLIENT_ID_HYP, false);

	bool tz_ffa_compatible = false;

	// The compatibility of the version number (x.y) of the caller with the
	// version number  (a.b) of the callee can also be as follows:
	// 1. If x != a, then the versions are incompatible.
	//    • The caller cannot inter-operate with the callee.
	// 2. If x == a and y > b, then the versions are incompatible.
	//    • The caller can inter-operate with the callee only if it
	//    downgrades its minor revision such that y <= b.
	// 3. If x == a and y <= b, then the versions are compatible, but
	// hypervisor only support limited version.
	ffa_version_t ffa_tz_version = ffa_version_cast((uint32_t)hyp_ret[0]);
	if ((int32_t)hyp_ret[0] == (int32_t)FFA_RET_NOT_SUPPORTED) {
		LOG(ERROR, WARN, "FF-A: SPMC not present");
	} else if (compiler_expected(ffa_version_is_clean(ffa_tz_version))) {
		// We don't compare the versions based on the FF-A rules, only
		// the version between MIN and MAX is available.
		uint16_t tz_ver_major = ffa_version_get_major(&ffa_tz_version);
		uint16_t tz_ver_minor = ffa_version_get_minor(&ffa_tz_version);

		if ((tz_ver_major != HYP_FFA_VERSION_MAJOR) ||
		    (tz_ver_minor > HYP_FFA_VERSION_MINOR_MAX) ||
		    (tz_ver_minor < HYP_FFA_VERSION_MINOR_MIN)) {
			panic("FF-A incompatibility between Hypervisor and SPMC");
		} else {
			// FF-A is available, we expect a minimum SMCCC version
			if (smccc_get_tz_version() < 0x10002U) {
				panic("smccc version < v1.2");
			}

			static_assert(
				HYP_FFA_VERSION_MINOR_MIN ==
					HYP_FFA_VERSION_MINOR_MAX,
				"we don't support multiple versions or downgrading");
			// Set the FFA version to be same as TZ.
			tz_ffa_compatible = true;
		}
	} else {
		panic("Unexpected FF-A version response from SPMC");
	}

	list_init(&ffa_secure_components_list);
	list_init(&ffa_ns_components_list);
	spinlock_init(&ffa_ns_components_list_lock);

	if (tz_ffa_compatible) {
		register_t args[SMCCC_1_2_ARGS] = { 0 };
		register_t ret[SMCCC_1_2_RETS]	= { 0 };

		// First discover and add all the TZ components
		ffa_get_sp_info();
		// Then add hypervisor to the list
		ffa_add_hypervisor();
		// TODO: Add all other EL2 components

		// Create HYP to TZ RXTX buffer if supported.
		ffa_hyp_feature_query(FFA_FUNCTION_FFA_RXTX_MAP, &args, &ret);

		smccc_function_id_t ret_fn_id =
			smccc_function_id_cast((uint32_t)ret[0]);
		smccc_function_t ret_function =
			smccc_function_id_get_function(&ret_fn_id);

		if (ret_function == FFA_FUNCTION_FFA_SUCCESS) {
			ffa_rxtx_features_buffer_info =
				ffa_rxtx_features_buffer_info_cast(
					(uint32_t)ret[2]);

			if (!ffa_rxtx_set_property(
				    &ffa_rxtx_features_buffer_info,
				    &ffa_rxtx_min_alignment,
				    &ffa_rxtx_max_buffer_size)) {
				tz_ffa_compatible = false;
				LOG(ERROR, WARN,
				    "FF-A: ffa_rxtx_set_property failed.");
			}

			if (!ffa_create_hyp_rxtx_buffer((
				    count_t)(ffa_rxtx_min_alignment / 4096U))) {
				panic("FF-A: create HYP to TZ RXTX buffer failed.");
			}
		} else {
			LOG(ERROR, WARN, "FF-A: FFA_FEATURE_RXTX unsupported");
			tz_ffa_compatible = false;
		}

		if (!tz_ffa_compatible) {
			panic("FF-A: FFA_FEATURE_RXTX error");
		}
	}

	if (tz_ffa_compatible) {
		ffa_set_hyp_version(ffa_tz_version);
		ffa_set_active(true);
	}
}

error_t
ffa_vm_configure(cap_id_t addrspace_cap, uint64_t uuid_lo, uint64_t uuid_hi,
		 uint64_t exec_context_count, uint64_t properties)
{
	error_t	  ret;
	cspace_t *cspace = cspace_get_self();

	if (!ffa_is_active()) {
		ret = ERROR_UNIMPLEMENTED;
		goto out;
	}

	if ((exec_context_count > PLATFORM_MAX_CORES) ||
	    (properties >= util_bit(32))) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	ffa_part_prop_desc_t component_properties =
		ffa_part_prop_desc_cast((uint32_t)properties);
	if (compiler_unexpected(
		    !ffa_part_prop_desc_is_clean(component_properties))) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	addrspace_ptr_result_t c = cspace_lookup_addrspace(
		cspace, addrspace_cap, CAP_RIGHTS_ADDRSPACE_ATTACH);
	if (compiler_unexpected(c.e != OK)) {
		ret = c.e;
		goto out_bad_cap;
	}
	addrspace_t *addrspace = c.r;
	assert(addrspace != NULL);

	void_ptr_result_t alloc_r = partition_alloc(addrspace->header.partition,
						    sizeof(ffa_component_t),
						    alignof(ffa_component_t));
	if (alloc_r.e != OK) {
		ret = ERROR_NOMEM;
		goto out;
	}
	ffa_component_t *component    = (ffa_component_t *)alloc_r.r;
	component->addrspace	      = addrspace;
	component->part_id	      = ffa_vmid_to_partid(addrspace->vmid);
	component->exec_context_count = (uint16_t)exec_context_count;
	component->uuid_lo	      = uuid_lo;
	component->uuid_hi	      = uuid_hi;
	component->properties	      = component_properties;

	spinlock_acquire(&ffa_ns_components_list_lock);
	list_insert_at_tail(&ffa_ns_components_list, &component->list_node);
	spinlock_release(&ffa_ns_components_list_lock);

	object_put_addrspace(addrspace);
	ret = OK;

out_bad_cap:
out:
	return ret;
}

ffa_rxtx_features_buffer_info_t
ffa_get_rxtx_buffer_sizes(void)
{
	return ffa_rxtx_features_buffer_info;
}

size_t
ffa_get_ffa_rxtx_max_buffer_size(void)
{
	return ffa_rxtx_max_buffer_size;
}

size_t
ffa_get_ffa_rxtx_min_alignment(void)
{
	return ffa_rxtx_min_alignment;
}
