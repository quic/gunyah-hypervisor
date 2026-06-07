// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypconstants.h>
#include <hypregisters.h>
#include <hyprights.h>

#include <addrspace.h>
#include <atomic.h>
#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <log.h>
#include <memdb.h>
#include <memextent.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <pgtable.h>
#include <preempt.h>
#include <rcu.h>
#include <rm_rpc.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <trace_helpers.h>
#include <util.h>
#include <vcpu.h>
#include <vdevice.h>

#include <events/rm_rpc.h>

#include <asm/nospec_checks.h>
#include <asm/sysregs.h>
#include <asm/system_registers.h>

#include "event_handlers.h"
#include "kernel_protect_internal.h"
#include "useraccess.h"

#define REGION_TYPE_TO_U64(x) ((uint64_t)(x))
#define KP_PROTECT_S2_PAGING

static inline bool
kp_segment_bounds_valid(uint64_t base, uint64_t size)
{
	bool valid = (size != 0U) &&
		     util_is_baligned(base, PGTABLE_VM_PAGE_SIZE) &&
		     util_is_baligned(size, PGTABLE_VM_PAGE_SIZE) &&
		     !util_add_overflows(base, size - 1U);
	return valid;
}

bool
kernel_protect_ipa_in_assets_region(vmid_t vmid, vmaddr_t ipa_page)
{
	bool	     ret;
	thread_t    *current	  = thread_get_self();
	addrspace_t *vm_addrspace = current->addrspace;

	assert(vm_addrspace->vmid == vmid);
	if (vm_addrspace->kp_extent_info == NULL ||
	    (vm_addrspace->kp_asset_info == NULL)) {
		ret = false;
		goto out;
	}

	vmaddr_t exclusion_range =
		vm_addrspace->kp_asset_info
			->section_info
				[KERNEL_PROTECT_REGION_TYPE_REGION_FIXMAP_TEXT_POKE0]
			.mem_area_base;
	size_t exclusion_size =
		vm_addrspace->kp_asset_info
			->section_info
				[KERNEL_PROTECT_REGION_TYPE_REGION_FIXMAP_TEXT_POKE0]
			.mem_area_size;

	ret = ((ipa_page >= exclusion_range) &&
	       (ipa_page < (exclusion_range + exclusion_size)));

out:
	return ret;
}

bool
kernel_protect_arm_el1_kernel_asset_info_compact(uint64_t arg1, uint64_t arg2,
						 uint64_t arg3, uint64_t *ret0)
{
	uint64_t ret;
	error_t	 err;
	(void)arg3;
#if defined(VERBOSE) && VERBOSE
	TRACE_AND_LOG(DEBUG, INFO, "GH_KERNEL_PROTECT: Compat EL1 HVC");
#endif
	thread_t *current	  = thread_get_self();
	bool	  invalid_segment = false;

	addrspace_t   *vm_addrspace = current->addrspace;
	kp_mem_info_t *mem_info	    = current->addrspace->kp_extent_info;
	asset_info_t  *vm_asset_info;

	if (!vcpu_is_vcpu(current)) {
		ret = (uint64_t)ERROR_ARGUMENT_INVALID;
		goto out;
	}
	if (mem_info == NULL) {
		ret = (uint64_t)ERROR_UNIMPLEMENTED;
		goto out;
	}

	/* Reject if KP was not set up (kp_extent_info is the sentinel). */
	if (vm_addrspace->kp_extent_info == NULL ||
	    (vm_addrspace->kp_asset_info == NULL)) {
		ret = (uint64_t)ERROR_OBJECT_STATE;
		goto out;
	}

	/* Prevent being called more than once: version is zeroed on allocation
	 * and set to KP_MAGIC_VERSION only after a successful first call. */
	if (vm_addrspace->kp_asset_info->version == KP_MAGIC_VERSION) {
		ret = (uint64_t)ERROR_OBJECT_STATE;
		goto out;
	}

	kernel_asset_info_t el1_asset_info = { 0 };
	if (sizeof(el1_asset_info) != arg2) {
		ret = (uint64_t)ERROR_ARGUMENT_SIZE;
		goto out;
	}

	size_result_t copy_ret;
	copy_ret = useraccess_copy_from_guest_ipa(vm_addrspace, &el1_asset_info,
						  sizeof(el1_asset_info), arg1,
						  (size_t)arg2, false, false);

	if (copy_ret.e != OK) {
		TRACE(ERROR, WARN,
		      "GH_KERNEL_PROTECT: Failed to copy IPA for assets");
		ret = (uint64_t)copy_ret.e;
		goto out;
	}

	if ((el1_asset_info.num_sections >
	     KERNEL_PROTECT_REGION_TYPE_REGION_MAX) ||
	    (el1_asset_info.version != KP_MAGIC_VERSION) ||
	    (vm_addrspace->kp_extent_info == NULL)) {
		ret = (uint64_t)ERROR_ARGUMENT_INVALID;
		goto out;
	}

#if defined(VERBOSE) && VERBOSE
	TRACE_AND_LOG(DEBUG, INFO, "GH_KERNEL_PROTECT: Updating EL1 asset info",
		      0, 0);
#endif
	for (count_t i = 0; i < el1_asset_info.num_sections; i++) {
		/* el1_asset_info.section_info[i].region_type is a uint64
		 * supplied by the guest. Range-check it with
		 * nospec_range_check() before using it as an array index to
		 * prevent a Spectre v1 gadget that could leak arbitrary
		 * hypervisor memory. */
		index_result_t region_idx_r = nospec_range_check(
			(index_t)el1_asset_info.section_info[i].region_type,
			(index_t)KERNEL_PROTECT_REGION_TYPE_REGION_MAX);
		if (region_idx_r.e != OK) {
			TRACE(ERROR, WARN,
			      "GH_KERNEL_PROTECT: region_type out of range");
			ret = (uint64_t)ERROR_ARGUMENT_INVALID;
			goto out;
		}
		index_t safe_region_type = region_idx_r.r;

		vm_asset_info = &el1_asset_info.section_info[safe_region_type];

		switch (safe_region_type) {
		case KERNEL_PROTECT_REGION_TYPE_REGION_TEXT:
			if (!kp_segment_bounds_valid(
				    vm_asset_info->mem_area_base,
				    vm_asset_info->mem_area_size)) {
				ret = (uint64_t)ERROR_ARGUMENT_SIZE;
				goto out;
			}

#if defined(VERBOSE) && VERBOSE
			TRACE_AND_LOG(
				DEBUG, INFO,
				"GH_KERNEL_PROTECT: Text(RX) Section info",
				vm_asset_info->mem_area_base,
				vm_asset_info->mem_area_size);
#endif
			err = memextent_donate_child(
				mem_info->memext_rx_region,
				vm_asset_info->mem_area_base,
				vm_asset_info->mem_area_size, false);
			if (err != OK) {
				TRACE(ERROR, WARN,
				      "GH_KERNEL_PROTECT: Failed updating Text section info");
				ret = (uint64_t)err;
				goto out;
			}
			err = rm_rpc_send_to_rm(
				RM_RPC_MSG_TYPE_REQUEST,
				(uint32_t)RM_RPC_CALL_MSG_ID_KP_CALCULATE_HASH,
				vm_asset_info, sizeof(*vm_asset_info));
			if (err != OK) {
				TRACE(ERROR, WARN,
				      "GH_KERNEL_PROTECT: Failed sending messgage rodata section info");
				ret = (uint64_t)err;
				goto out;
			}
			break;
		case KERNEL_PROTECT_REGION_TYPE_REGION_RODATA:
			if (!kp_segment_bounds_valid(
				    vm_asset_info->mem_area_base,
				    vm_asset_info->mem_area_size)) {
				ret = (uint64_t)ERROR_ARGUMENT_SIZE;
				goto out;
			}

#if defined(VERBOSE) && VERBOSE
			TRACE_AND_LOG(DEBUG, INFO,
				      "GH_KERNEL_PROTECT: (RO) Section info",
				      vm_asset_info->mem_area_base,
				      vm_asset_info->mem_area_size);
#endif
			err = memextent_donate_child(
				mem_info->memext_ro_region,
				vm_asset_info->mem_area_base,
				vm_asset_info->mem_area_size, false);
			if (err != OK) {
				TRACE(ERROR, WARN,
				      "GH_KERNEL_PROTECT: Failed updating RO section info");
				ret = (uint64_t)err;
				goto out;
			}
			err = rm_rpc_send_to_rm(
				RM_RPC_MSG_TYPE_REQUEST,
				(uint32_t)RM_RPC_CALL_MSG_ID_KP_CALCULATE_HASH,
				vm_asset_info, sizeof(*vm_asset_info));
			if (err != OK) {
				TRACE(ERROR, WARN,
				      "GH_KERNEL_PROTECT: Failed sending messgage rodata section info");
				ret = (uint64_t)err;
				goto out;
			}
			break;
		case KERNEL_PROTECT_REGION_TYPE_REGION_BSS:
			if (!kp_segment_bounds_valid(
				    vm_asset_info->mem_area_base,
				    vm_asset_info->mem_area_size)) {
				ret = (uint64_t)ERROR_ARGUMENT_SIZE;
				goto out;
			}

#if defined(VERBOSE) && VERBOSE
			TRACE_AND_LOG(DEBUG, INFO,
				      "GH_KERNEL_PROTECT: (BSS) Section info",
				      vm_asset_info->mem_area_base,
				      vm_asset_info->mem_area_size);
#endif
			err = memextent_donate_child(
				mem_info->memext_rw_region,
				vm_asset_info->mem_area_base,
				vm_asset_info->mem_area_size, false);
			if (err != OK) {
				TRACE(ERROR, WARN,
				      "GH_KERNEL_PROTECT: Failed updating BSS section info");
				ret = (uint64_t)err;
				goto out;
			}
			break;
		case KERNEL_PROTECT_REGION_TYPE_REGION_DATA:
			if (!kp_segment_bounds_valid(
				    vm_asset_info->mem_area_base,
				    vm_asset_info->mem_area_size)) {
				ret = (uint64_t)ERROR_ARGUMENT_SIZE;
				goto out;
			}

#if defined(VERBOSE) && VERBOSE
			TRACE_AND_LOG(DEBUG, INFO,
				      "GH_KERNEL_PROTECT: (DATA) Section info",
				      vm_asset_info->mem_area_base,
				      vm_asset_info->mem_area_size);
#endif
			err = memextent_donate_child(
				mem_info->memext_rw_region,
				vm_asset_info->mem_area_base,
				vm_asset_info->mem_area_size, false);
			if (err != OK) {
				TRACE(ERROR, WARN,
				      "GH_KERNEL_PROTECT: Failed updating DATA section info");
				ret = (uint64_t)err;
				goto out;
			}
			break;
		case KERNEL_PROTECT_REGION_TYPE_REGION_FIXMAP_TEXT_POKE0:
			if (!kp_segment_bounds_valid(
				    vm_asset_info->mem_area_base,
				    vm_asset_info->mem_area_size)) {
				ret = (uint64_t)ERROR_ARGUMENT_SIZE;
				goto out;
			}

#if defined(VERBOSE) && VERBOSE
			TRACE_AND_LOG(DEBUG, INFO,
				      "FixMap: (Text)  Poke Section info",
				      vm_asset_info->mem_area_base,
				      vm_asset_info->mem_area_size);
#endif
			break;
		default:
			TRACE(ERROR, WARN,
			      "GH_KERNEL_PROTECT: Invalid memory type");
			invalid_segment = true;
			break;
		}
	}

	if (!invalid_segment && (vm_addrspace->kp_extent_info != NULL) &&
	    (current->addrspace->kp_asset_info != NULL)) {
		memscpy(current->addrspace->kp_asset_info,
			sizeof(*current->addrspace->kp_asset_info),
			&el1_asset_info, sizeof(el1_asset_info));
		ret = 0;
	} else {
		TRACE_AND_LOG(
			ERROR, WARN,
			"GH_KERNEL_PROTECT: Failed to Protect EL1 Assets");
		ret = (uint64_t)ERROR_ARGUMENT_INVALID;
	}

out:
	*ret0 = ret;
	return true;
}

bool
kernel_protect_arm_el1_kernel_asset_info(uint64_t arg1, uint64_t arg2,
					 uint64_t arg3, uint64_t *ret0)
{
	uint64_t	     ret;
	thread_t	    *current  = thread_get_self();
	kp_mem_info_t	    *mem_info = current->addrspace->kp_extent_info;
	kernel_asset_info_t *asset_info;

	if (!vcpu_is_vcpu(current)) {
		ret = (uint64_t)ERROR_ARGUMENT_INVALID;
		goto out;
	}
	if ((mem_info == NULL) || (current->addrspace->kp_asset_info == NULL)) {
		ret = (uint64_t)ERROR_OBJECT_STATE;
		goto out;
	}
	asset_info = current->addrspace->kp_asset_info;

	/* arg3 is a guest-supplied region type. Range-check it with
	 * nospec_range_check() before using it in a switch/array index
	 * to prevent a Spectre v1 gadget. */
	index_result_t region_idx_r = nospec_range_check(
		(index_t)arg3, (index_t)KERNEL_PROTECT_REGION_TYPE_REGION_MAX);
	if (region_idx_r.e != OK) {
		TRACE(ERROR, WARN,
		      "GH_KERNEL_PROTECT: region_type out of range");
		ret = (uint64_t)ERROR_ARGUMENT_INVALID;
		goto out;
	}
	kernel_protect_region_type_t region =
		kernel_protect_region_type_raw_cast((uint32_t)region_idx_r.r);

	bool	     perform_integrity = false;
	asset_info_t vm_asset_info;
	error_t	     err;

	errno_t memset_err = memset_s(&vm_asset_info, sizeof(vm_asset_info), 0,
				      sizeof(vm_asset_info));
	if (memset_err != 0) {
		panic("memset_s failed in kernel_protect_arm_el1_kernel_asset_info");
	}

	size_t asset_size	    = arg2;
	vm_asset_info.mem_area_base = arg1;
	vm_asset_info.mem_area_size = asset_size;

	// Validate the guest-supplied base/size before touching any state.
	if (!kp_segment_bounds_valid(vm_asset_info.mem_area_base,
				     vm_asset_info.mem_area_size)) {
		ret = (uint64_t)ERROR_ARGUMENT_SIZE;
		goto out;
	}
#if defined(VERBOSE) && VERBOSE
	TRACE_AND_LOG(
		DEBUG, INFO,
		"GH_KERNEL_PROTECT: Type : {:x} Base : {:x} Alignment {:x}",
		region, vm_asset_info.mem_area_base,
		vm_asset_info.mem_area_size);
#endif
	switch (region) {
	case KERNEL_PROTECT_REGION_TYPE_REGION_TEXT:
#if defined(VERBOSE) && VERBOSE
		TRACE_AND_LOG(DEBUG, INFO, "GH_KERNEL_PROTECT: RX Asset info");
#endif
		vm_asset_info.region_type = REGION_TYPE_TO_U64(
			KERNEL_PROTECT_REGION_TYPE_REGION_TEXT);
		perform_integrity = true;

		asset_info->section_info[KERNEL_PROTECT_REGION_TYPE_REGION_TEXT]
			.mem_area_base = vm_asset_info.mem_area_base;
		asset_info->section_info[KERNEL_PROTECT_REGION_TYPE_REGION_TEXT]
			.mem_area_size = vm_asset_info.mem_area_size;
#ifdef KP_PROTECT_S2_PAGING
		err = memextent_donate_child(mem_info->memext_rx_region,
					     vm_asset_info.mem_area_base,
					     vm_asset_info.mem_area_size,
					     false);
		if (err != OK) {
			TRACE(ERROR, WARN,
			      "GH_KERNEL_PROTECT: RX extent Creation Failed {:d}",
			      (register_t)err);
			ret = (uint64_t)err;
			goto out;
		}
#endif
		break;
	case KERNEL_PROTECT_REGION_TYPE_REGION_RODATA:
#if defined(VERBOSE) && VERBOSE
		TRACE_AND_LOG(DEBUG, INFO, "GH_KERNEL_PROTECT: RO Asset info");
#endif
		vm_asset_info.region_type = REGION_TYPE_TO_U64(
			KERNEL_PROTECT_REGION_TYPE_REGION_RODATA);
		perform_integrity = true;

		asset_info
			->section_info[KERNEL_PROTECT_REGION_TYPE_REGION_RODATA]
			.mem_area_base = vm_asset_info.mem_area_base;
		asset_info
			->section_info[KERNEL_PROTECT_REGION_TYPE_REGION_RODATA]
			.mem_area_size = vm_asset_info.mem_area_size;

#ifdef KP_PROTECT_S2_PAGING
		err = memextent_donate_child(mem_info->memext_ro_region,
					     vm_asset_info.mem_area_base,
					     vm_asset_info.mem_area_size,
					     false);
		if (err != OK) {
			TRACE(ERROR, WARN,
			      "GH_KERNEL_PROTECT: RO extent Creation Failed {:d}",
			      (register_t)err);
			ret = (uint64_t)err;
			goto out;
		}
#endif
		break;
	case KERNEL_PROTECT_REGION_TYPE_REGION_BSS:
		vm_asset_info.region_type = REGION_TYPE_TO_U64(
			KERNEL_PROTECT_REGION_TYPE_REGION_BSS);
		asset_info->section_info[KERNEL_PROTECT_REGION_TYPE_REGION_BSS]
			.mem_area_base = vm_asset_info.mem_area_base;
		asset_info->section_info[KERNEL_PROTECT_REGION_TYPE_REGION_BSS]
			.mem_area_size = vm_asset_info.mem_area_size;
#ifdef KP_PROTECT_S2_PAGING
		err = memextent_donate_child(mem_info->memext_rw_region,
					     vm_asset_info.mem_area_base,
					     vm_asset_info.mem_area_size,
					     false);
		if (err != OK) {
			TRACE(ERROR, WARN,
			      "GH_KERNEL_PROTECT: BSS/DATA RWX extent Creation Failed {:d}",
			      (register_t)err);
			ret = (uint64_t)err;
			goto out;
		}
#endif
		break;
	case KERNEL_PROTECT_REGION_TYPE_REGION_DATA:
		vm_asset_info.region_type = REGION_TYPE_TO_U64(
			KERNEL_PROTECT_REGION_TYPE_REGION_DATA);

		asset_info->section_info[KERNEL_PROTECT_REGION_TYPE_REGION_DATA]
			.mem_area_base = vm_asset_info.mem_area_base;
		asset_info->section_info[KERNEL_PROTECT_REGION_TYPE_REGION_DATA]
			.mem_area_size = vm_asset_info.mem_area_size;
#if defined(VERBOSE) && VERBOSE
		TRACE_AND_LOG(DEBUG, WARN, "GH_KERNEL_PROTECT: RW Asset info");
#endif

#ifdef KP_PROTECT_S2_PAGING
		err = memextent_donate_child(mem_info->memext_rw_region,
					     vm_asset_info.mem_area_base,
					     vm_asset_info.mem_area_size,
					     false);
		if (err != OK) {
			TRACE(ERROR, WARN,
			      "GH_KERNEL_PROTECT: BSS/DATA RWX extent Creation Failed {:d}",
			      (register_t)err);
			ret = (uint64_t)err;
			goto out;
		}
#endif
		break;
	case KERNEL_PROTECT_REGION_TYPE_REGION_FIXMAP_TEXT_POKE0:
		asset_info
			->section_info
				[KERNEL_PROTECT_REGION_TYPE_REGION_FIXMAP_TEXT_POKE0]
			.mem_area_base = vm_asset_info.mem_area_base;
		asset_info
			->section_info
				[KERNEL_PROTECT_REGION_TYPE_REGION_FIXMAP_TEXT_POKE0]
			.mem_area_size = vm_asset_info.mem_area_size;
		break;
	case KERNEL_PROTECT_REGION_TYPE_REGION_MAX:
	default:
		TRACE(ERROR, WARN, "GH_KERNEL_PROTECT: Invalid memory attrs");
		break;
	}

	if (perform_integrity) {
		err = rm_rpc_send_to_rm(
			RM_RPC_MSG_TYPE_REQUEST,
			(uint32_t)RM_RPC_CALL_MSG_ID_KP_CALCULATE_HASH,
			&vm_asset_info, sizeof(vm_asset_info));
		if (err != OK) {
			TRACE(ERROR, WARN, "GH_KERNEL_PROTECT: RPC Msg failed");
			ret = (uint64_t)err;
			goto out;
		}
	}
	ret = (uint64_t)OK;
out:
	*ret0 = ret;
	return true;
}

bool
kernel_protect_enable_vm_protection(uint64_t arg1, uint64_t *ret0)
{
	thread_t    *current = thread_get_self();
	uint64_t     ret;
	asset_info_t vm_asset_info;
	error_t	     err;

	/* Two callers are permitted:
	 *   1. The HLOS VM (PVM) — may query any VM's integrity, including
	 *      its own addrspace or a protected secondary VM.
	 *   2. Any VM querying its own addrspace (self-check).
	 * All other combinations are rejected with ERROR_DENIED. */

	/* arg1 is the CapID of the target VM's addrspace — look it up. */
	cspace_t	      *cspace = cspace_get_self();
	addrspace_ptr_result_t vm_ret = cspace_lookup_addrspace_any(
		cspace, (cap_id_t)arg1, CAP_RIGHTS_ADDRSPACE_LOOKUP);
	if (compiler_unexpected(vm_ret.e != OK)) {
		TRACE(ERROR, WARN,
		      "GH_KERNEL_PROTECT: addrspace lookup failed ({:d})",
		      (register_t)vm_ret.e);
		ret = (uint64_t)vm_ret.e;
		goto out;
	}
	addrspace_t *target_addrspace = vm_ret.r;

	bool caller_is_hlos =
		vcpu_option_flags_get_hlos_vm(&current->vcpu_options);
	bool self_check = (target_addrspace == current->addrspace);

	if (!caller_is_hlos && !self_check) {
		TRACE(ERROR, WARN,
		      "GH_KERNEL_PROTECT: caller is not HLOS and not querying own addrspace");
		ret = (uint64_t)ERROR_DENIED;
		object_put_addrspace(target_addrspace);
		goto out;
	}

	if (target_addrspace->kp_extent_info == NULL ||
	    (target_addrspace->kp_asset_info == NULL)) {
		ret = (uint64_t)ERROR_OBJECT_STATE;
		object_put_addrspace(target_addrspace);
		goto out;
	}
	kernel_asset_info_t *asset_info = target_addrspace->kp_asset_info;
	vm_asset_info.region_type =
		REGION_TYPE_TO_U64(KERNEL_PROTECT_REGION_TYPE_REGION_TEXT);
	vm_asset_info.mem_area_base =
		asset_info->section_info[KERNEL_PROTECT_REGION_TYPE_REGION_TEXT]
			.mem_area_base;
	vm_asset_info.mem_area_size =
		asset_info->section_info[KERNEL_PROTECT_REGION_TYPE_REGION_TEXT]
			.mem_area_size;

	err = rm_rpc_send_to_rm(RM_RPC_MSG_TYPE_REQUEST,
				(uint32_t)RM_RPC_CALL_MSG_ID_KP_CHECK_INTEGRITY,
				&vm_asset_info, sizeof(vm_asset_info));
	if (err != OK) {
		TRACE(ERROR, WARN, "GH_KERNEL_PROTECT: RPC Msg failed {:d}",
		      (register_t)err);
		ret = (uint64_t)err;
		goto out;
	}

	vm_asset_info.region_type =
		REGION_TYPE_TO_U64(KERNEL_PROTECT_REGION_TYPE_REGION_RODATA);
	vm_asset_info.mem_area_base =
		asset_info
			->section_info[KERNEL_PROTECT_REGION_TYPE_REGION_RODATA]
			.mem_area_base;
	vm_asset_info.mem_area_size =
		asset_info
			->section_info[KERNEL_PROTECT_REGION_TYPE_REGION_RODATA]
			.mem_area_size;

	err = rm_rpc_send_to_rm(RM_RPC_MSG_TYPE_REQUEST,
				(uint32_t)RM_RPC_CALL_MSG_ID_KP_CHECK_INTEGRITY,
				&vm_asset_info, sizeof(vm_asset_info));
	if (err != OK) {
		TRACE(ERROR, WARN, "GH_KERNEL_PROTECT: RPC Msg failed {:d}",
		      (register_t)err);
		ret = (uint64_t)err;
		goto out;
	}
	ret = (uint64_t)err;

out:
	if (vm_ret.e == OK) {
		object_put_addrspace(vm_ret.r);
	}
	*ret0 = ret;
	return true;
}

void
kernel_protect_handle_object_cleanup_addrspace(rcu_update_status_t *status,
					       addrspace_t	   *addrspace)
{
	(void)status;
	assert(addrspace != NULL);
#if defined(VERBOSE) && VERBOSE
	TRACE_AND_LOG(DEBUG, INFO, "GH_KERNEL_PROTECT: Addrspace Cleanup ", 0,
		      0);
#endif
	if (addrspace->kp_extent_info != NULL) {
		if (addrspace->kp_extent_info == NULL) {
			TRACE(ERROR, WARN,
			      "Cleanup : KP memory layout not initialized");
			goto out;
		}

		kp_mem_info_t *mem_info = addrspace->kp_extent_info;

		if (mem_info->memext_rx_region != NULL) {
			vdevice_detach_phys(&mem_info->rx_mem_intent_vdev,
					    mem_info->memext_rx_region);
			object_put_memextent(mem_info->memext_rx_region);
			mem_info->memext_rx_region = NULL;
		}

		if (mem_info->memext_ro_region != NULL) {
			vdevice_detach_phys(&mem_info->ro_mem_intent_vdev,
					    mem_info->memext_ro_region);
			object_put_memextent(mem_info->memext_ro_region);
			mem_info->memext_ro_region = NULL;
		}

		if (mem_info->memext_rw_region != NULL) {
			vdevice_detach_phys(&mem_info->rw_mem_intent_vdev,
					    mem_info->memext_rw_region);
			object_put_memextent(mem_info->memext_rw_region);
			mem_info->memext_rw_region = NULL;
		}

		if (mem_info->memext_rwx_region != NULL) {
			object_put_memextent(mem_info->memext_rwx_region);
			mem_info->memext_rwx_region = NULL;
		}

		partition_t *partition = addrspace->header.partition;

		partition_free(partition, addrspace->kp_extent_info,
			       sizeof(*addrspace->kp_extent_info));
		addrspace->kp_extent_info = NULL;

		if (addrspace->kp_asset_info != NULL) {
			partition_free(partition, addrspace->kp_asset_info,
				       sizeof(*addrspace->kp_asset_info));
			addrspace->kp_asset_info = NULL;
		}
	}
out:
	return;
}

error_t
kernel_protect_handle_object_activate_addrspace(addrspace_t *addrspace)
{
	error_t err;
	assert(addrspace != NULL);
#if defined(VERBOSE) && VERBOSE
	TRACE_AND_LOG(DEBUG, INFO, "KP addrpace activate");
#endif
	// Commented as the RM configure calls fails if done before
	// if done before addrspace activate hypercall.
	if (addrspace->kp_extent_info != NULL) {
		kp_mem_info_t *mem_info = addrspace->kp_extent_info;
		mem_info->rx_mem_intent_vdev.type =
			VDEVICE_TYPE_VKERNEL_ASSET_RX;
		// Attach RX vdevices
		err = vdevice_attach_phys(&mem_info->rx_mem_intent_vdev,
					  mem_info->memext_rx_region);
		if (err != OK) {
			TRACE(ERROR, WARN, "KP memory layout not initialized");
			goto out;
		}
		// Attach RW vdevice memextent
		mem_info->rw_mem_intent_vdev.type =
			VDEVICE_TYPE_VKERNEL_ASSET_RW;
		err = vdevice_attach_phys(&mem_info->rw_mem_intent_vdev,
					  mem_info->memext_rw_region);
		if (err != OK) {
			TRACE(ERROR, WARN, "KP memory layout not initialized");
			goto out;
		}
		// Attach RO vdevice memextent
		mem_info->ro_mem_intent_vdev.type =
			VDEVICE_TYPE_VKERNEL_ASSET_RO;
		err = vdevice_attach_phys(&mem_info->ro_mem_intent_vdev,
					  mem_info->memext_ro_region);
		if (err != OK) {
			TRACE(ERROR, WARN, "KP memory layout not initialized");
			goto out;
		}
	} else {
		TRACE(ERROR, WARN, "KP not initialized");
	}
	err = OK;
out:
	return err;
}

#if defined(KERNEL_PROTECT_RAW_ADDR_FAULT)
vcpu_trap_result_t
vkernel_protect_handle_vcpu_trap_data_abort_guest(ESR_EL2_t	  esr,
						  vmaddr_result_t ipa,
						  FAR_EL2_t	  far)
{
	vcpu_trap_result_t result  = VCPU_TRAP_RESULT_UNHANDLED;
	thread_t	  *current = thread_get_self();

	if (!vcpu_is_vcpu(current)) {
		goto out;
	}

	vmid_t			 vmid = current->addrspace->vmid;
	ESR_EL2_ISS_DATA_ABORT_t iss =
		ESR_EL2_ISS_DATA_ABORT_cast(ESR_EL2_get_ISS(&esr));
	esr_ec_t	ec	   = ESR_EL2_get_EC(&esr);
	iss_da_ia_fsc_t fsc	   = ESR_EL2_ISS_DATA_ABORT_get_DFSC(&iss);
	bool		s1ptw	   = ESR_EL2_ISS_DATA_ABORT_get_S1PTW(&iss);
	bool		is_write   = false;
	bool		isv	   = ESR_EL2_ISS_DATA_ABORT_get_ISV(&iss);
	vmaddr_t	ipa_region = 0;
	vmaddr_t	ipa_page   = 0;

	// We dont care about other VM faults here only SVM
	if (vmid != QHEE_VMID_OEM1_VM) {
		goto out;
	}
#if defined(VERBOSE) && VERBOSE
	TRACE_AND_LOG(
		DEBUG, INFO,
		"GH_KERNEL_PROTECT: Trapped DATA abort starts here {:d} {:d} {:#x} ",
		ec, vmid, (register_t)ipa.r);
#endif
	bool permission_fault = ((fsc == ISS_DA_IA_FSC_PERMISSION_L0) ||
				 (fsc == ISS_DA_IA_FSC_PERMISSION_L1) ||
				 (fsc == ISS_DA_IA_FSC_PERMISSION_L2) ||
				 (fsc == ISS_DA_IA_FSC_PERMISSION_L3));

	if (s1ptw) {
		if (ipa.e != OK) {
#if defined(VERBOSE) && VERBOSE
			TRACE_AND_LOG(
				ERROR, WARN,
				"GH_KERNEL_PROTECT: S1PTW permission fault but IPA invalid %lu %lu",
				ec, vmid);
#endif
			goto out;
		}
		ipa_region = ipa.r;
		ipa_page   = util_balign_down(ipa_region, PGTABLE_VM_PAGE_SIZE);
#if defined(VERBOSE) && VERBOSE
		TRACE_AND_LOG(
			DEBUG, INFO,
			"GH_KERNEL_PROTECT: S1PTW hit protected Range but IPA invalid %llx %lu",
			ipa_page, vmid);
#endif
		result = VCPU_TRAP_RESULT_RETRY;
		goto out;
	}

	if (!permission_fault) {
#if defined(VERBOSE) && VERBOSE
		TRACE_AND_LOG(
			DEBUG, INFO,
			"GH_KERNEL_PROTECT: Not a perimission fault {:d} {:d} {:d}",
			ec, vmid, fsc);
#endif
		goto out;
	}

	if (!s1ptw && isv) {
		is_write = ESR_EL2_ISS_DATA_ABORT_get_WnR(&iss);
	}

	if (is_write) {
		gvaddr_t va = FAR_EL2_get_VirtualAddress(&far);

		// This is a permission fault, so the IPA in HPFAR_EL2 may
		// be invalid. If so, look up the IPA from the VA.
		if (ipa.e != OK) {
			ipa = addrspace_va_to_ipa_read(va);
			if (ipa.e != OK) {
				result = VCPU_TRAP_RESULT_RETRY;
				goto out;
			}
		}

		ipa_region = ipa.r;
		ipa_page   = util_balign_down(ipa_region, PGTABLE_VM_PAGE_SIZE);

		if (kernel_protect_ipa_in_assets_region(vmid, ipa_page)) {
#if defined(VERBOSE) && VERBOSE
			TRACE_AND_LOG(
				DEBUG, INFO,
				"GH_KERNEL_PROTECT: write hit protected Range IPA valid %llx %lu",
				ipa_page, vmid);
#endif
			result = VCPU_TRAP_RESULT_FAULT;
			goto out;
		}
	}

out:
	(void)ipa;
	(void)far;
	return result;
}
#endif
#if defined(S1_PTE_WALK)

// Query S1 page-table attributes for an IPA range in the given addrspace.
//
// Walks the VM's stage-2 page table one block at a time and verifies that
// every page in [ipa_base, ipa_base + size) is:
//   - mapped (no holes), and
//   - mapped with identical kernel_access, user_access, and memtype.
//
// On success (*out_phys) is set to the PA of ipa_base when the range is
// physically contiguous, or PADDR_INVALID otherwise.
//
// Returns:
//   OK                     - range fully mapped with uniform attributes
//   ERROR_ADDR_INVALID     - one or more pages are unmapped
//   ERROR_ARGUMENT_SIZE    - size is 0 or not PGTABLE_VM_PAGE_SIZE-aligned
//   ERROR_ARGUMENT_INVALID - ipa_base is not PGTABLE_VM_PAGE_SIZE-aligned
//   ERROR_DENIED           - attributes differ across the range
error_t
vkernel_protect_check_s1_range_attr(addrspace_t *addrspace, vmaddr_t ipa_base,
				    size_t size, pgtable_access_t *out_access,
				    pgtable_vm_memtype_t *out_memtype,
				    paddr_t		 *out_phys);

// ---------------------------------------------------------------------------
// vkernel_protect_check_s1_range_attr
//
// Query the S1 page-table attributes (access permissions, memory type, and
// physical address) for a contiguous IPA range belonging to the calling VM's
// address space.
//
// The function iterates over the range [ipa_base, ipa_base + size) one
// pgtable_vm_lookup() block at a time.  It succeeds only when:
//   - every page in the range is mapped (no holes), AND
//   - all blocks carry identical kernel_access, user_access, and memtype.
//
// Parameters:
//   addrspace   - the VM's addrspace (must not be NULL)
//   ipa_base    - start of the IPA range (must be PGTABLE_VM_PAGE_SIZE-aligned)
//   size        - length of the range in bytes (must be > 0 and
//   PGTABLE_VM_PAGE_SIZE-aligned) out_access  - [out] kernel-mode
//   pgtable_access_t for the range out_memtype - [out] pgtable_vm_memtype_t for
//   the range out_phys    - [out] PA of ipa_base (valid only when the range is
//                       physically contiguous; set to PADDR_INVALID otherwise)
//
// Returns:
//   OK                     - range is fully mapped with uniform attributes
//   ERROR_ADDR_INVALID     - one or more pages in the range are unmapped
//   ERROR_ARGUMENT_SIZE    - size is zero or not PGTABLE_VM_PAGE_SIZE-aligned
//   ERROR_ARGUMENT_INVALID - ipa_base is not PGTABLE_VM_PAGE_SIZE-aligned
//   ERROR_DENIED           - attributes are not uniform across the range
// ---------------------------------------------------------------------------
error_t
vkernel_protect_check_s1_range_attr(addrspace_t *addrspace, vmaddr_t ipa_base,
				    size_t size, pgtable_access_t *out_access,
				    pgtable_vm_memtype_t *out_memtype,
				    paddr_t		 *out_phys)
{
	error_t ret;

	assert(addrspace != NULL);
	assert(out_access != NULL);
	assert(out_memtype != NULL);
	assert(out_phys != NULL);

	if (size == 0U || (size & (PGTABLE_VM_PAGE_SIZE - 1U)) != 0U) {
		ret = (uint64_t)ERROR_ARGUMENT_SIZE;
		goto out;
	}

	if ((ipa_base & (PGTABLE_VM_PAGE_SIZE - 1U)) != 0U) {
		ret = (uint64_t)ERROR_ARGUMENT_INVALID;
		goto out;
	}

	pgtable_access_t     first_kernel_access = PGTABLE_ACCESS_NONE;
	pgtable_access_t     first_user_access	 = PGTABLE_ACCESS_NONE;
	pgtable_vm_memtype_t first_memtype	 = (pgtable_vm_memtype_t)0;
	paddr_t		     base_phys		 = PADDR_INVALID;
	paddr_t		     expected_next_phys	 = PADDR_INVALID;
	bool		     first_block	 = true;
	bool		     phys_contig	 = true;
	size_t		     offset		 = 0U;

	while (offset < size) {
		vmaddr_t	     curr = ipa_base + offset;
		paddr_t		     mapped_phys;
		size_t		     mapped_size = 0U;
		pgtable_vm_memtype_t mapped_memtype;
		pgtable_access_t     mapped_kernel_access;
		pgtable_access_t     mapped_user_access;

		bool mapped = pgtable_vm_lookup(&addrspace->vm_pgtable, curr,
						&mapped_phys, &mapped_size,
						&mapped_memtype,
						&mapped_kernel_access,
						&mapped_user_access);
		if (!mapped) {
#if defined(VERBOSE) && VERBOSE
			TRACE_AND_LOG(ERROR, WARN,
				      "GH_KERNEL_PROTECT: "
				      "check_s1_range_attr: "
				      "unmapped IPA 0x%lx",
				      curr);
#endif
			ret = (uint64_t)ERROR_ADDR_INVALID;
			goto out;
		}

		// Adjust for intra-block offset and clamp to remaining range.
		size_t block_offset = curr & (mapped_size - 1U);
		mapped_phys += block_offset;
		mapped_size =
			util_min(mapped_size - block_offset, size - offset);

		if (first_block) {
			first_kernel_access = mapped_kernel_access;
			first_user_access   = mapped_user_access;
			first_memtype	    = mapped_memtype;
			base_phys	    = mapped_phys;
			expected_next_phys  = mapped_phys + mapped_size;
			first_block	    = false;
		} else {
			// Enforce uniform attributes across the whole range.
			if (!pgtable_access_is_equal(mapped_kernel_access,
						     first_kernel_access) ||
			    !pgtable_access_is_equal(mapped_user_access,
						     first_user_access) ||
			    (mapped_memtype != first_memtype)) {
#if defined(VERBOSE) && VERBOSE
				TRACE_AND_LOG(ERROR, WARN,
					      "GH_KERNEL_PROTECT: "
					      "check_s1_range_attr: "
					      "non-uniform attrs at IPA 0x%lx",
					      curr);
#endif
				ret = (uint64_t)ERROR_DENIED;
				goto out;
			}

			// Track physical contiguity.
			if (phys_contig) {
				if (mapped_phys != expected_next_phys) {
					phys_contig = false;
				} else {
					expected_next_phys += mapped_size;
				}
			}
		}

		offset += mapped_size;
	}

	if (first_block) {
		// size was non-zero but we never entered the loop — shouldn't
		// happen given the size check above, but be defensive.
		ret = (uint64_t)ERROR_ADDR_INVALID;
		goto out;
	}

	*out_access  = first_kernel_access;
	*out_memtype = first_memtype;
	*out_phys    = phys_contig ? base_phys : PADDR_INVALID;

out:
	return ret;
}

#endif
