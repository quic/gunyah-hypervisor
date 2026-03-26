// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypconstants.h>
#include <hypcontainers.h>
#include <hyprights.h>

#include <addrspace.h>
#include <atomic.h>
#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <list.h>
#include <log.h>
#include <memdb.h>
#include <memextent.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <pgtable.h>
#include <platform_mem.h>
#include <preempt.h>
#include <rcu.h>
#include <smccc.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <util.h>

#include <asm/cache.h>
#include <asm/cpu.h>

#include "event_handlers.h"
#include "ffa.h"

static_assert(sizeof(ffa_memory_transaction_desc_t) == 48U, "struct size");

static_assert(sizeof(ffa_memory_access_desc_t) == 32U, "struct size");

static_assert(sizeof(ffa_composite_memory_region_desc_t) == 16U, "struct size");

static_assert(sizeof(ffa_constituent_memory_region_desc_t) == 16U,
	      "struct size");

static const ffa_memory_handle_t FFA_MEMORY_INVALID_HANDLE =
	ffa_memory_handle_cast(FFA_MEMORY_INVALID_HANDLE_RAW);

error_t
ffa_memory_create_addrspace(addrspace_create_t addrspace_create)
{
	assert(addrspace_create.addrspace != NULL);

	spinlock_init(&addrspace_create.addrspace->ffa_memory_lock);
	list_init(&addrspace_create.addrspace->ffa_memory_list);
	atomic_init(&addrspace_create.addrspace->ffa_memory_handles, 0U);

	return OK;
}

static ffa_ret_t
ffa_memory_send_flags_checks(ffa_memory_send_flags_t flags,
			     pgtable_access_t access, bool is_share)
{
	ffa_ret_t ffa_ret;

	// Validate zero flag: SPMC handles zeroing, so hypervisor skips it to
	// avoid duplicate operations. Zero flag is invalid for SHARE
	// operations and requires write permission for LEND/DONATE operations.
	if (ffa_memory_send_flags_get_zero(&flags)) {
		if (is_share) {
			ffa_ret = FFA_RET_INVALID_PARAMETERS;
			goto out;
		} else if (!pgtable_access_check(access, PGTABLE_ACCESS_W)) {
			ffa_ret = FFA_RET_DENIED;
			goto out;
		} else {
			// Valid configuration for LEND/DONATE with write
			// access.
		}
	}
	// Note: Device memory zeroing is technically allowed by the spec but
	// impractical. Efficient memset implementations use DC ZVA which causes
	// alignment faults on device memory. If device sharing is supported in
	// the future, zeroing of device memory should be explicitly denied.

	if (ffa_memory_send_flags_get_time_slice(&flags)) {
		// Time-sliced operations are not supported.
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
		goto out;
	}

	if (!ffa_memory_send_flags_is_clean(flags)) {
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
		goto out;
	}

	ffa_ret = FFA_RET_SUCCESS;
out:
	return ffa_ret;
}

// FIXME: QC Gunyah issue #286
// Since the VM may pass memory access descriptors (MAD)s of different sizes,
// we should calculate the actual MAD based on its size, rather than accessing
// it directly using array indexing.
static ffa_memory_access_desc_t *
ffa_memory_get_mad(uintptr_t mad_base, size_t mad_size, index_t i)
{
	return (ffa_memory_access_desc_t *)(mad_base + (mad_size * i));
}

static ffa_ret_t
ffa_memory_validate_acl(memextent_t *me, vmid_t vmid,
			ffa_trans_type_t		trans_type,
			const ffa_memory_access_desc_t *mad, size_t mad_size,
			count_t mad_count, pgtable_access_t *access,
			bool *identity_mapping)
{
	ffa_ret_t ffa_ret;

	spinlock_acquire(&me->lock);

	count_t acl_len = me->mem_acl_len;
	if (!me->mem_acl_registered || (acl_len != 1U)) {
		// ACL must be explicitly registered for SP sharing.
		// Only two valid scenarios exist:
		// 1. ACL represents the owner VM of the memextent
		// 2. ACL represents the sole VM that was lent the memory region
		ffa_ret = FFA_RET_DENIED;
		goto out;
	}

	mem_acl_info_t *acl = &me->mem_acl_info[0];

	vmid_t acl_vmid = mem_acl_info_get_vmid(acl);
	if (vmid != acl_vmid) {
		// Only the ACL-registered VM can share memory with SPs.
		ffa_ret = FFA_RET_DENIED;
		goto out;
	}

	uint8_t			acl_perm = mem_acl_info_get_perm(acl);
	pgtable_access_result_t acl_access_res =
		pgtable_access_cast_safe(acl_perm);
	if (acl_access_res.e != OK) {
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
		goto out;
	}
	pgtable_access_t acl_access = acl_access_res.r;

	if ((trans_type == FFA_TRANS_TYPE_LEND) ||
	    (trans_type == FFA_TRANS_TYPE_SHARE)) {
		// Verify sender's specified permissions are less permissive
		// than or equal to the VM's actual permissions in the ACL.
		for (index_t i = 0U; i < mad_count; i++) {
			ffa_memory_access_desc_t *curr_mad =
				ffa_memory_get_mad((uintptr_t)mad, mad_size, i);

			ffa_memory_data_access_t data_access =
				ffa_memory_access_permission_desc_get_data_access(
					&curr_mad->permission);
			if (data_access == FFA_MEMORY_DATA_ACCESS_RO) {
				if (!pgtable_access_check(acl_access,
							  PGTABLE_ACCESS_R)) {
					ffa_ret = FFA_RET_DENIED;
					goto out;
				}
			} else if (data_access == FFA_MEMORY_DATA_ACCESS_RW) {
				if (!pgtable_access_check(acl_access,
							  PGTABLE_ACCESS_RW)) {
					ffa_ret = FFA_RET_DENIED;
					goto out;
				}
			} else {
				ffa_ret = FFA_RET_INVALID_PARAMETERS;
				goto out;
			}
		}
	} else {
		// Donation is not supported
		panic("FF-A: donate not supported");
	}

	if (access != NULL) {
		*access = acl_access;
	}
	if (identity_mapping != NULL) {
		*identity_mapping = mem_acl_info_get_identity_mapping(acl);
	}

	ffa_ret = FFA_RET_SUCCESS;
out:
	spinlock_release(&me->lock);
	return ffa_ret;
}

static bool
ffa_memory_get_memory_region_desc(
	void *buffer, size_t buffer_size, ffa_memory_access_desc_t *mad_0,
	size_t				       mad_array_end,
	ffa_composite_memory_region_desc_t   **composite_mrd,
	ffa_constituent_memory_region_desc_t **constituent_mrd,
	count_t				      *constituent_mrd_count)
{
	bool ret;

	ffa_composite_memory_region_desc_t   *l_composite_mrd;
	ffa_constituent_memory_region_desc_t *l_constituent_mrd;

	size_t composite_mrd_end = mad_0->composite_mrd_offset +
				   sizeof(ffa_composite_memory_region_desc_t);
	if ((mad_0->composite_mrd_offset < mad_array_end) ||
	    (composite_mrd_end > buffer_size)) {
		ret = false;
		goto out;
	}
	l_composite_mrd =
		(ffa_composite_memory_region_desc_t
			 *)((uintptr_t)buffer + mad_0->composite_mrd_offset);
	*composite_mrd = l_composite_mrd;

	if ((l_composite_mrd->range_count == 0U) ||
	    (l_composite_mrd->range_count >
	     FFA_CONSTITUENT_MEMORY_REGION_DESC_COUNT_MAX)) {
		LOG(ERROR, WARN,
		    "FF-A: invalid constituent memory region count({:d}), limitation({:d}))",
		    l_composite_mrd->range_count,
		    FFA_CONSTITUENT_MEMORY_REGION_DESC_COUNT_MAX);
		ret = false;
		goto out;
	}

	size_t constituent_mrd_end =
		composite_mrd_end +
		(sizeof(ffa_constituent_memory_region_desc_t) *
		 l_composite_mrd->range_count);
	// For fragmented transfers, constituent_mrd_end may exceed buffer size.
	if (constituent_mrd_end > buffer_size) {
		assert(((constituent_mrd_end - buffer_size) %
			sizeof(ffa_constituent_memory_region_desc_t)) == 0U);

		constituent_mrd_end = buffer_size;
	}

	l_constituent_mrd =
		(ffa_constituent_memory_region_desc_t *)((uintptr_t)buffer +
							 composite_mrd_end);
	*constituent_mrd = l_constituent_mrd;
	*constituent_mrd_count =
		(count_t)((constituent_mrd_end - composite_mrd_end) /
			  sizeof(ffa_constituent_memory_region_desc_t));

	ret = true;
out:
	return ret;
}

static bool
ffa_memory_get_all_descs(void *buffer, size_t buffer_size,
			 ffa_memory_transaction_desc_t **mtd,
			 ffa_memory_access_desc_t **mad, count_t *mad_count,
			 ffa_composite_memory_region_desc_t   **composite_mrd,
			 ffa_constituent_memory_region_desc_t **constituent_mrd,
			 count_t *constituent_mrd_count)
{
	bool ret;

	assert((buffer != NULL) && (buffer_size != 0U));
	assert(mtd != NULL);
	assert((mad != NULL) && (mad_count != NULL));
	assert((composite_mrd != NULL) && (constituent_mrd != NULL) &&
	       (constituent_mrd_count != NULL));

	ffa_memory_transaction_desc_t *l_mtd;
	ffa_memory_access_desc_t      *l_mad;

	if (!util_is_baligned(buffer_size, 16U)) {
		ret = false;
		goto out;
	}

	if (buffer_size < sizeof(ffa_memory_transaction_desc_t)) {
		ret = false;
		goto out;
	}

	l_mtd = (ffa_memory_transaction_desc_t *)buffer;
	*mtd  = l_mtd;

	// FIXME: QC Gunyah issue #286
	// Only check MAD size should be equal with ffa_1_2_mad_size after this
	// WA is removed.
	size_t ffa_1_0_mad_size =
		sizeof(ffa_memory_access_desc_t) -
		util_sizeof_member(ffa_memory_access_desc_t, impl_def);
	size_t ffa_1_2_mad_size = sizeof(ffa_memory_access_desc_t);

	if ((l_mtd->mad_count == 0U) ||
	    (l_mtd->mad_count > FFA_MEMORY_ACCESS_DESC_COUNT_MAX) ||
	    ((l_mtd->mad_size != ffa_1_0_mad_size) &&
	     (l_mtd->mad_size != ffa_1_2_mad_size))) {
		ret = false;
		goto out;
	}

	uint32_t mad_array_end =
		l_mtd->mad_array_offset + (l_mtd->mad_size * l_mtd->mad_count);
	if ((l_mtd->mad_array_offset < sizeof(ffa_memory_transaction_desc_t)) ||
	    (mad_array_end > buffer_size)) {
		ret = false;
		goto out;
	}
	l_mad	   = (ffa_memory_access_desc_t *)((uintptr_t)buffer +
						  l_mtd->mad_array_offset);
	*mad	   = l_mad;
	*mad_count = l_mtd->mad_count;

	ffa_memory_access_desc_t *l_mad_0 = l_mad;

	ret = ffa_memory_get_memory_region_desc(buffer, buffer_size, l_mad_0,
						mad_array_end, composite_mrd,
						constituent_mrd,
						constituent_mrd_count);
	if (!ret) {
		goto out;
	}

	for (index_t i = 1U; i < l_mtd->mad_count; i++) {
		// We let all endpoints reference the same composite memory
		// region.
		ffa_memory_access_desc_t *curr_mad = ffa_memory_get_mad(
			(uintptr_t)l_mad, l_mtd->mad_size, i);
		if (curr_mad->composite_mrd_offset !=
		    l_mad_0->composite_mrd_offset) {
			ret = false;
			goto out;
		}
	}

	ret = true;
out:
	if (!ret) {
		LOG(ERROR, WARN, "FF-A: Memory Descriptor Format Error.");
	}
	return ret;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch-enum"

static bool
ffa_memory_check_device_attr(pgtable_vm_memtype_t      memtype,
			     ffa_memory_device_attri_t device_attri)
{
	bool success;

	switch (memtype) {
	case PGTABLE_VM_MEMTYPE_DEVICE_NGNRNE:
		success = device_attri <= FFA_MEMORY_DEVICE_ATTRI_NGNRNE;
		break;
	case PGTABLE_VM_MEMTYPE_DEVICE_NGNRE:
		success = device_attri <= FFA_MEMORY_DEVICE_ATTRI_NGNRE;
		break;
	case PGTABLE_VM_MEMTYPE_DEVICE_NGRE:
		success = device_attri <= FFA_MEMORY_DEVICE_ATTRI_NGRE;
		break;
	case PGTABLE_VM_MEMTYPE_DEVICE_GRE:
		success = device_attri <= FFA_MEMORY_DEVICE_ATTRI_GRE;
		break;
	default:
		success = false;
		break;
	}

	return success;
}

static bool
ffa_memory_check_cacheability(pgtable_vm_memtype_t	memtype,
			      ffa_memory_cacheability_t cacheability)
{
	bool success;

	switch (memtype) {
	case PGTABLE_VM_MEMTYPE_NORMAL_NC:
		success = cacheability <= FFA_MEMORY_CACHEABILITY_NON_CACHEABLE;
		break;
	case PGTABLE_VM_MEMTYPE_NORMAL_WB:
		success = cacheability <= FFA_MEMORY_CACHEABILITY_WB;
		break;
	default:
		success = false;
		break;
	}

	return success;
}

static bool
ffa_memory_check_shareability(pgtable_vm_memtype_t	memtype,
			      ffa_memory_shareability_t shareability)
{
	bool success;

	switch (memtype) {
	case PGTABLE_VM_MEMTYPE_NORMAL_NC:
	case PGTABLE_VM_MEMTYPE_NORMAL_ONC_IWT:
	case PGTABLE_VM_MEMTYPE_NORMAL_ONC_IWB:
	case PGTABLE_VM_MEMTYPE_NORMAL_OWT_INC:
	case PGTABLE_VM_MEMTYPE_NORMAL_WT:
	case PGTABLE_VM_MEMTYPE_NORMAL_OWT_IWB:
	case PGTABLE_VM_MEMTYPE_NORMAL_OWB_INC:
	case PGTABLE_VM_MEMTYPE_NORMAL_OWB_IWT:
	case PGTABLE_VM_MEMTYPE_NORMAL_WB: {
		ffa_memory_shareability_t current;
#if SCHEDULER_CAN_MIGRATE
		current = FFA_MEMORY_SHAREABILITY_INNER_SHAREABLE;
#else
		current = FFA_MEMORY_SHAREABILITY_NON_SHAREABLE;
#endif
		success =
			(current == shareability) ||
			((current == FFA_MEMORY_SHAREABILITY_INNER_SHAREABLE) &&
			 (shareability ==
			  FFA_MEMORY_SHAREABILITY_NON_SHAREABLE));
	} break;
	default:
		success = false;
		break;
	}

	return success;
}

#pragma clang diagnostic pop

static ffa_ret_t
ffa_memory_check_permitted_attributes(pgtable_vm_memtype_t	memtype,
				      ffa_memory_type_t		ffa_memtype,
				      uint8_t			cacheability,
				      ffa_memory_shareability_t shareability)
{
	ffa_ret_t ffa_ret;

	if (ffa_memtype == FFA_MEMORY_TYPE_DEVICE) {
		if (shareability != FFA_MEMORY_SHAREABILITY_NON_SHAREABLE) {
			ffa_ret = FFA_RET_INVALID_PARAMETERS;
			goto out;
		}

		ffa_memory_device_attri_t device_attri =
			ffa_memory_device_attri_raw_cast(cacheability);
		if (!ffa_memory_check_device_attr(memtype, device_attri)) {
			ffa_ret = FFA_RET_DENIED;
			goto out;
		}
	} else if (ffa_memtype == FFA_MEMORY_TYPE_NORMAL) {
		if (shareability == FFA_MEMORY_SHAREABILITY_RESERVED) {
			ffa_ret = FFA_RET_INVALID_PARAMETERS;
			goto out;
		}
		if (!ffa_memory_check_shareability(memtype, shareability)) {
			ffa_ret = FFA_RET_DENIED;
			goto out;
		}

		ffa_memory_cacheability_t normal_cache =
			ffa_memory_cacheability_raw_cast(cacheability);
		if ((normal_cache == FFA_MEMORY_CACHEABILITY_RESERVED_0) ||
		    (normal_cache == FFA_MEMORY_CACHEABILITY_RESERVED_1)) {
			ffa_ret = FFA_RET_INVALID_PARAMETERS;
			goto out;
		}
		if (!ffa_memory_check_cacheability(memtype, normal_cache)) {
			ffa_ret = FFA_RET_DENIED;
			goto out;
		}
	} else {
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
		goto out;
	}

	ffa_ret = FFA_RET_SUCCESS;
out:
	return ffa_ret;
}

// Lookup and increment reference count for the owner memextent.
static memextent_t *
ffa_memory_get_owner_me(addrspace_t *addrspace, vmaddr_t vbase, size_t vsize)
{
	memextent_t *me_ret = NULL;

	addrspace_lookup_result_t lookup_ret =
		addrspace_lookup(addrspace, vbase, vsize);
	if (lookup_ret.e != OK) {
		goto out;
	}

	// Maintain the RCU critical section until we finish using res.r.object
	rcu_read_start();

	memdb_obj_type_result_t res = memdb_lookup(lookup_ret.r.phys);
	if ((res.e != OK) || (res.r.type != MEMDB_TYPE_EXTENT)) {
		goto out_rcu_read_finish;
	}

	memextent_t *me = (memextent_t *)res.r.object;

	if (me->type != MEMEXTENT_TYPE_SPARSE) {
		goto out_rcu_read_finish;
	}

	// FIXME: QC Gunyah issue #274
	// Device memory sharing support may be added in the future, but must
	// comply with passthrough rules.
	if (me->memtype == MEMEXTENT_MEMTYPE_DEVICE) {
		goto out_rcu_read_finish;
	}

	// Acquire reference to safely exit RCU critical section and derive
	// from the extent.
	if (!object_get_memextent_safe(me)) {
		goto out_rcu_read_finish;
	}

	me_ret = me;

out_rcu_read_finish:
	rcu_read_finish();
out:
	return me_ret;
}

static void
ffa_memory_transaction_desc_add_phys_range(
	ffa_memory_transaction_desc_t *mtd, size_t composite_mrd_offset,
	size_t constituent_mrd_offset, size_t offset_limit,
	const ffa_memory_phys_range_t *phys_range_info)
{
	ffa_constituent_memory_region_desc_t *new_cons_mrd =
		(ffa_constituent_memory_region_desc_t *)((uintptr_t)mtd +
							 constituent_mrd_offset);
	count_t total_page_count = 0;
	for (index_t i = 0U; i < phys_range_info->count; i++) {
		assert(util_is_baligned(
			phys_range_info->phys_range[i].base,
			util_bit(FFA_CONSTITUENT_MRD_PAGE_SHIFT)));
		assert(util_is_baligned(
			phys_range_info->phys_range[i].size,
			util_bit(FFA_CONSTITUENT_MRD_PAGE_SHIFT)));

		new_cons_mrd[i].address = phys_range_info->phys_range[i].base;
		new_cons_mrd[i].page_count =
			(count_t)(phys_range_info->phys_range[i].size >>
				  FFA_CONSTITUENT_MRD_PAGE_SHIFT);
		total_page_count += new_cons_mrd[i].page_count;
	}
	// The caller should make sure the growing constituent MRD never exceeds
	// the limitation.
	assert(((uintptr_t)&new_cons_mrd[phys_range_info->count] -
		(uintptr_t)mtd) <= offset_limit);

	ffa_composite_memory_region_desc_t *new_comp_mrd =
		(ffa_composite_memory_region_desc_t *)((uintptr_t)mtd +
						       composite_mrd_offset);
	new_comp_mrd->total_page_count = total_page_count;
	new_comp_mrd->range_count      = phys_range_info->count;
}

static ffa_ret_t
ffa_memory_transaction_desc_generate(
	ffa_memory_transaction_desc_t *mtd, size_t mtd_max_size,
	ffa_memory_transaction_desc_t *from_mtd, size_t composite_mrd_offset,
	size_t			       constituent_mrd_offset,
	const ffa_memory_phys_range_t *phys_range_info)
{
	ffa_ret_t ffa_ret;

	assert(phys_range_info != NULL);

	size_t mtd_len = constituent_mrd_offset +
			 (phys_range_info->count *
			  sizeof(ffa_constituent_memory_region_desc_t));
	if (mtd_len > mtd_max_size) {
		// For IPA != PA scenarios, constituent MRD must fit in the
		// first fragment. Multi-fragment transfers are not supported
		// for non-identity mappings, so physical ranges cannot exceed
		// one RXTX buffer.
		// FFA_RET_NOT_SUPPORTED may be more appropriate as the error
		// code, but FFA_RET_NOT_SUPPORTED is not in the range legal
		// error code for MEM_LEND, we use FFA_RET_DENIED instead.
		ffa_ret = FFA_RET_DENIED;
		goto out;
	}

	size_t copy_size = memscpy((void *)mtd, mtd_max_size, (void *)from_mtd,
				   composite_mrd_offset);
	assert(copy_size == composite_mrd_offset);

	// Initialize memory region to zero to ensure no stale data is present.
	memset_s((void *)((uintptr_t)mtd + composite_mrd_offset),
		 mtd_max_size - composite_mrd_offset, 0,
		 mtd_len - composite_mrd_offset);

	ffa_memory_transaction_desc_add_phys_range(mtd, composite_mrd_offset,
						   constituent_mrd_offset,
						   mtd_max_size,
						   phys_range_info);

	ffa_ret = FFA_RET_SUCCESS;
out:
	return ffa_ret;
}

static ffa_share_t *
ffa_memory_lookup(addrspace_t *addrspace, ffa_memory_handle_t handle)
{
	ffa_share_t *found = NULL;

	spinlock_acquire(&addrspace->ffa_memory_lock);

	LIST_FOREACH_CONTAINER_BEGIN(ffa_share_t, &addrspace->ffa_memory_list,
				     ffa_share, node, item)
		if (ffa_memory_handle_is_equal(item->handle, handle)) {
			found = item;
			break;
		}
	LIST_FOREACH_CONTAINER_END

	spinlock_release(&addrspace->ffa_memory_lock);

	return found;
}

static ffa_ret_t
ffa_memory_lookup_and_update_state(addrspace_t	      *addrspace,
				   ffa_memory_handle_t handle,
				   ffa_memory_state_t  expected_state,
				   ffa_memory_state_t  new_state,
				   ffa_share_t	     **share_ret)
{
	ffa_ret_t    ffa_ret;
	ffa_share_t *found = NULL;

	spinlock_acquire(&addrspace->ffa_memory_lock);

	LIST_FOREACH_CONTAINER_BEGIN(ffa_share_t, &addrspace->ffa_memory_list,
				     ffa_share, node, item)
		if (ffa_memory_handle_is_equal(item->handle, handle)) {
			found = item;
			break;
		}
	LIST_FOREACH_CONTAINER_END

	if (found == NULL) {
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
		goto out;
	}

	bool success = atomic_compare_exchange_strong_explicit(
		&found->state, &expected_state, new_state, memory_order_acquire,
		memory_order_relaxed);
	if (!success) {
		ffa_ret = FFA_RET_BUSY;
		goto out;
	}

	assert_debug(share_ret != NULL);

	*share_ret = found;

	ffa_ret = FFA_RET_SUCCESS;
out:
	spinlock_release(&addrspace->ffa_memory_lock);

	return ffa_ret;
}

// We only check the reserved value which is required as MBZ in Spec.
// Don't check for SBZ parts.
static bool
ffa_memory_construct_parameter_checks(vmid_t vmid, ffa_trans_type_t trans_type,
				      const ffa_memory_transaction_desc_t *mtd,
				      const ffa_memory_access_desc_t	  *mad,
				      count_t mad_count)
{
	bool ret;

	// This checks the sender is not secure and is current VM.
	if (!ffa_partition_id_is_equal(mtd->sender, ffa_vmid_to_partid(vmid))) {
		ret = false;
		goto out;
	}

	if (ffa_memory_attributes_get_NS(&mtd->attributes)) {
		// NS bit is Reserved (MBZ) for MEM_SHARE/LEND/DONATE
		// operations.
		ret = false;
		goto out;
	}

	if (ffa_memory_handle_raw(mtd->handle) != 0x0U) {
		// Handle must be zero at the virtual FF-A instance.
		ret = false;
		goto out;
	}

	if ((trans_type == FFA_TRANS_TYPE_DONATE) ||
	    ((trans_type == FFA_TRANS_TYPE_LEND) && (mad_count == 1U))) {
		// For DONATE or single-borrower LEND, memory type must not be
		// specified.
		if ((ffa_memory_attributes_get_type(&mtd->attributes) !=
		     FFA_MEMORY_TYPE_NOT_SPECIFIED) ||
		    (ffa_memory_attributes_get_cacheability(&mtd->attributes) !=
		     0U) ||
		    (ffa_memory_attributes_get_shareability(&mtd->attributes) !=
		     FFA_MEMORY_SHAREABILITY_NON_SHAREABLE)) {
			ret = false;
			goto out;
		}
	}

	if ((trans_type == FFA_TRANS_TYPE_DONATE) && (mad_count != 1U)) {
		// DONATE operations require exactly one receiver.
		ret = false;
		goto out;
	}

	for (index_t i = 0U; i < mad_count; i++) {
		ffa_memory_access_desc_t *curr_mad =
			ffa_memory_get_mad((uintptr_t)mad, mtd->mad_size, i);

		ffa_partition_id_t curr_receiver =
			ffa_memory_access_permission_desc_get_receiver(
				&curr_mad->permission);

		// In an FFA_MEM_SHARE ABI, the sender could request the memory
		// region to be mapped with different data access permissions in
		// its own translation regime. It specifies these permissions
		// and its endpoint ID in a separate endpoint memory access
		// descriptor. -- FFA-MMP-1.2-REL0 section 1.10.3
		// Above should not be done by hypervisor, still under
		// discussion.
		if (ffa_partition_id_is_equal(curr_receiver, mtd->sender)) {
			// We don't support reducing access rights at the
			// moment so just check that the transaction is a
			// SHARE.
			if (trans_type != FFA_TRANS_TYPE_SHARE) {
				ret = false;
				goto out;
			}
		} else {
			if (!ffa_partition_id_get_is_secure(&curr_receiver)) {
				// Receiver must be a secure partition.
				ret = false;
				goto out;
			}
			if (ffa_get_secure_component(curr_receiver) == NULL) {
				// Non-existent SP.
				ret = false;
				goto out;
			}
		}

		for (index_t j = 0U; j < i; j++) {
			ffa_memory_access_desc_t *j_mad = ffa_memory_get_mad(
				(uintptr_t)mad, mtd->mad_size, j);

			if (ffa_partition_id_is_equal(
				    curr_receiver,
				    ffa_memory_access_permission_desc_get_receiver(
					    &j_mad->permission))) {
				// Duplicate SP.
				ret = false;
				goto out;
			}
		}

		if ((trans_type == FFA_TRANS_TYPE_DONATE) &&
		    (ffa_memory_access_permission_desc_get_data_access(
			     &curr_mad->permission) !=
		     FFA_MEMORY_DATA_ACCESS_NOT_SPECIFIED)) {
			// For MEM_DONATE, SPMC must validate the permission
			// specified by the SP receiver is same or less
			// permissive in MEM_RETRIEVE_REQ and specify the
			// determined permission in MEM_RETRIEVE_RESP. But SPMC
			// doesn't know the actual permission of the memory
			// regions for the sender, we should tell SPMC this by
			// an implemented scheme. mad[i].permission.impl_def[1]
			// might be used, please refer to
			// FFA-MMP-1.2-REL0-1.10.2 for more details.
			ret = false;
			goto out;
		}

		if (ffa_memory_access_permission_desc_get_instr_access(
			    &curr_mad->permission) !=
		    FFA_MEMORY_INSTR_ACCESS_NOT_SPECIFIED) {
			// The sender is not allowed to specify instruction
			// access for MEM_DONATE/LEND/SHARE. Please refer to
			// FFA-MMP-1.2-REL0-1.10.3 for more details.
			// mad[i].permission.impl_def[1] might be used to pass
			// the executable information to SPMC.
			//
			// For MEM_SHARE, MEM_LEND with multiple participants
			// and the later MEM_RETRIEVE_REQ, this field must be
			// set to 2b'00. SPMC would map the memory with XN and
			// specify XN in MEM_RETRIEVE_RESP.
			// For MEM_DONATE and MEM_LEND with a single
			// participant, this field must be set to 2b'00. And
			// this field can be specified in later MEM_RETRIEVE_REQ
			// with permissive permission.
			ret = false;
			goto out;
		}

		if (ffa_memory_access_permission_desc_get_flags(
			    &curr_mad->permission) != 0U) {
			ret = false;
			goto out;
		}
	}

	ret = true;
out:
	return ret;
}

static ffa_ret_t
ffa_memextent_error_translate(error_t error)
{
	ffa_ret_t ffa_ret;

	if ((error) == ERROR_ARGUMENT_INVALID) {
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
	} else if ((error) == ERROR_DENIED) {
		ffa_ret = FFA_RET_DENIED;
	} else if ((error) == ERROR_NOMEM) {
		ffa_ret = FFA_RET_NO_MEMORY;
	} else if ((error) == ERROR_BUSY) {
		ffa_ret = FFA_RET_BUSY;
	} else {
		/* ran into an unexpected error. */
		ffa_ret = FFA_RET_ABORTED;
		LOG(ERROR, WARN, "FF-A: unexpected memextent ret {:#x}.",
		    (register_t)error);
	}

	return ffa_ret;
}

static ffa_ret_t
ffa_memory_allocate_and_derive(addrspace_t *addrspace, memextent_t *owner_me,
			       ffa_trans_type_t trans_type,
			       pgtable_access_t access,
			       count_t total_page_count, count_t mad_count,
			       ffa_memory_attributes_t attributes,
			       bool identity_mapping, ffa_share_t **share_ret)
{
	ffa_ret_t ffa_ret;

	ffa_share_t *share;
	partition_t *partition = addrspace->header.partition;

	void_ptr_result_t alloc_ret =
		partition_alloc(partition, sizeof(*share), alignof(*share));
	if (alloc_ret.e != OK) {
		ffa_ret = FFA_RET_NO_MEMORY;
		goto out;
	}
	(void)memset_s(alloc_ret.r, sizeof(*share), 0, sizeof(*share));
	share = (ffa_share_t *)alloc_ret.r;

	share->handle		 = FFA_MEMORY_INVALID_HANDLE;
	share->trans_type	 = trans_type;
	share->access		 = access;
	share->total_size_expect = (size_t)total_page_count
				   << FFA_CONSTITUENT_MRD_PAGE_SHIFT;
	share->attributes	 = attributes;
	share->need_check_attributes =
		(trans_type == FFA_TRANS_TYPE_SHARE) ||
		((trans_type == FFA_TRANS_TYPE_LEND) && (mad_count > 1U));
	// Fragmented transfers are only supported for identity-mapped memory.
	share->identity_mapping = identity_mapping;
	list_init(&share->phys_range_list);

	// Derive child memextent to prevent later potential
	// donation/sharing/lending and other conflicting operations on this
	// memory.
	memextent_ptr_result_t me_ret = memextent_derive(
		owner_me, 0U, owner_me->size, MEMEXTENT_MEMTYPE_ANY, access,
		MEMEXTENT_TYPE_SPARSE);
	if (me_ret.e != OK) {
		ffa_ret = ffa_memextent_error_translate(me_ret.e);
		goto out_free;
	}
	if (trans_type != FFA_TRANS_TYPE_SHARE) {
		// For LEND/DONATE, unmap from sender's address space.
		error_t unmap_err = memextent_unmap_whole_extent(
			me_ret.r, addrspace, addrspace_map_flags_default());
		if (unmap_err != OK) {
			ffa_ret = ffa_memextent_error_translate(unmap_err);
			goto out_delete_me;
		}
	}

	share->me = me_ret.r;

	spinlock_acquire(&addrspace->ffa_memory_lock);
	list_insert_at_tail(&addrspace->ffa_memory_list, &share->node);
	spinlock_release(&addrspace->ffa_memory_lock);

	assert_debug(share != NULL);
	*share_ret = share;

	ffa_ret = FFA_RET_SUCCESS;
	goto out;

out_delete_me:
	object_put_memextent(me_ret.r);
out_free:
	partition_free(partition, share, sizeof(*share));
out:
	return ffa_ret;
}

static ffa_ret_t
ffa_memory_construct(addrspace_t *addrspace, ffa_trans_type_t trans_type,
		     ffa_memory_transaction_desc_t *mtd,
		     ffa_memory_access_desc_t *mad, count_t mad_count,
		     const ffa_composite_memory_region_desc_t *composite_mrd,
		     const ffa_constituent_memory_region_desc_t *constituent_mrd,
		     ffa_share_t **share)
{
	ffa_ret_t ffa_ret;
	vmid_t	  vmid = addrspace->vmid;

	count_t prev_count = atomic_fetch_add_explicit(
		&addrspace->ffa_memory_handles, 1U, memory_order_relaxed);
	if (prev_count >= FFA_MAX_MEMORY_HANDLES) {
		ffa_ret = FFA_RET_DENIED;
		goto out_sub_count;
	}

	bool checks_ret = ffa_memory_construct_parameter_checks(
		vmid, trans_type, mtd, mad, mad_count);
	if (!checks_ret) {
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
		LOG(ERROR, WARN,
		    "FF-A: illegal parameters in memory description.");
		goto out_sub_count;
	}

	vmaddr_t vbase_0 = (vmaddr_t)constituent_mrd->address;
	size_t	 vsize_0 = (size_t)constituent_mrd->page_count
			   << FFA_CONSTITUENT_MRD_PAGE_SHIFT;

	memextent_t *owner_me =
		ffa_memory_get_owner_me(addrspace, vbase_0, vsize_0);
	if (owner_me == NULL) {
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
		goto out_sub_count;
	}

	pgtable_access_t access;
	bool		 identity_mapping;

	// We can't share/lend any memory from owner_me if the VM doesn't meet
	// the acl restriction.
	ffa_ret = ffa_memory_validate_acl(owner_me, vmid, trans_type, mad,
					  mtd->mad_size, mad_count, &access,
					  &identity_mapping);
	if (ffa_ret != FFA_RET_SUCCESS) {
		LOG(ERROR, WARN, "FF-A: current VM violate the acl rule.");
		goto out_put_owner_me;
	}

	ffa_ret = ffa_memory_send_flags_checks(
		mtd->flags, access, trans_type == FFA_TRANS_TYPE_SHARE);
	if (ffa_ret != FFA_RET_SUCCESS) {
		LOG(ERROR, WARN, "FF-A: incorrect mtd flags.");
		goto out_put_owner_me;
	}

	ffa_ret = ffa_memory_allocate_and_derive(
		addrspace, owner_me, trans_type, access,
		composite_mrd->total_page_count, mad_count, mtd->attributes,
		identity_mapping, share);
	if (ffa_ret != FFA_RET_SUCCESS) {
		goto out_put_owner_me;
	}

	// Prevent addrspace deactivation until all ffa_share objects are
	// reclaimed.
	(void)object_get_addrspace_additional(addrspace);

	// Release parent memextent reference before returning.
	object_put_memextent(owner_me);

	goto out;

out_put_owner_me:
	object_put_memextent(owner_me);
out_sub_count:
	(void)atomic_fetch_sub_explicit(&addrspace->ffa_memory_handles, 1U,
					memory_order_relaxed);
out:
	return ffa_ret;
}

static phys_range_t *
realloc_phys_ranges(partition_t *partition, phys_range_t *phys_range,
		    count_t old_capacity, count_t new_capacity)
{
	phys_range_t *phys_range_ret;

	assert(new_capacity > old_capacity);

	size_t		  alloc_size = sizeof(*phys_range) * new_capacity;
	void_ptr_result_t alloc_ret =
		partition_alloc(partition, alloc_size, alignof(*phys_range));
	if (alloc_ret.e != OK) {
		phys_range_ret = NULL;
		goto out;
	}

	size_t copy_size = sizeof(*phys_range) * old_capacity;
	// Safe even when phys_range is NULL (first allocation).
	(void)memscpy(alloc_ret.r, alloc_size, phys_range, copy_size);

	// clear the unused memory.
	void  *clear_start = (void *)((uintptr_t)alloc_ret.r + copy_size);
	size_t clear_size  = alloc_size - copy_size;
	(void)memset_s(clear_start, clear_size, 0, clear_size);

	if (phys_range != NULL) {
		partition_free(partition, phys_range, copy_size);
	}

	phys_range_ret = (phys_range_t *)alloc_ret.r;
out:
	return phys_range_ret;
}

// The virtual address may be non-contiguous, and the function only adds the
// first segment of the physical address it finds.
static ffa_ret_t
ffa_memory_add_constituent_mrd(addrspace_t *addrspace, const ffa_share_t *share,
			       vmaddr_t vbase, size_t vsize,
			       phys_range_t *phys_range)
{
	ffa_ret_t ffa_ret;

	addrspace_lookup_result_t lookup_ret =
		addrspace_lookup(addrspace, vbase, vsize);
	if (lookup_ret.e != OK) {
		ffa_ret = ffa_memextent_error_translate(lookup_ret.e);
		goto out;
	}

	if (share->need_check_attributes) {
		ffa_memory_type_t ffa_memtype =
			ffa_memory_attributes_get_type(&share->attributes);
		uint8_t cacheability = ffa_memory_attributes_get_cacheability(
			&share->attributes);
		ffa_memory_shareability_t shareability =
			ffa_memory_attributes_get_shareability(
				&share->attributes);

		ffa_ret = ffa_memory_check_permitted_attributes(
			lookup_ret.r.memtype, ffa_memtype, cacheability,
			shareability);
		if (ffa_ret != FFA_RET_SUCCESS) {
			// Requested attributes must be less permissive
			// than actual memory attributes.
			// Ref: FFA-MMP-1.2-REL0 section 1.10.4
			LOG(ERROR, WARN,
			    "FF-A: requested attributes exceed actual memory attributes.");
			goto out;
		}
	}

	if (share->identity_mapping &&
	    ((vbase != lookup_ret.r.phys) || (vsize != lookup_ret.r.size))) {
		LOG(ERROR, WARN,
		    "FF-A: scattered lend/share of non identity_mapping");
		ffa_ret = FFA_RET_DENIED;
		goto out;
	}

	error_t donate_err = memextent_donate_child(
		share->me, lookup_ret.r.phys - share->me->phys_base,
		lookup_ret.r.size, false);
	if (donate_err != OK) {
		ffa_ret = ffa_memextent_error_translate(donate_err);
		goto out;
	}

	phys_range->base = lookup_ret.r.phys;
	phys_range->size = lookup_ret.r.size;

	ffa_ret = FFA_RET_SUCCESS;
out:
	return ffa_ret;
}

static void
ffa_memory_remove_constituent_mrd(const ffa_share_t  *share,
				  const phys_range_t *phys_range, bool mem_zero)
{
	paddr_t phys_base = phys_range->base;
	size_t	phys_size = phys_range->size;

	error_t donate_err = memextent_donate_child(
		share->me, phys_base - share->me->phys_base, phys_size, true);
	if (donate_err != OK) {
		panic("FF-A: donate child failed");
	}

	if (mem_zero) {
		char *addr = (char *)partition_phys_map(phys_base, phys_size);
		partition_phys_access_enable(addr);

		(void)memset_s(addr, phys_size, 0, phys_size);
		CACHE_CLEAN_RANGE((uint8_t *)addr, phys_size);

		partition_phys_access_disable(addr);
		partition_phys_unmap(addr, phys_base, phys_size);
	}
}

static ffa_ret_t
ffa_memory_insert_phys_ranges(partition_t *partition, ffa_share_t *share,
			      phys_range_t *phys_range,
			      count_t	    phys_range_capacity,
			      count_t phys_range_count, size_t total_size,
			      ffa_memory_phys_range_t **phys_range_info)
{
	ffa_ret_t ffa_ret;

	ffa_memory_phys_range_t *phys_range_info_allocated;

	void_ptr_result_t alloc_ret =
		partition_alloc(partition, sizeof(*phys_range_info_allocated),
				alignof(*phys_range_info_allocated));
	if (alloc_ret.e != OK) {
		ffa_ret = FFA_RET_NO_MEMORY;
		goto out;
	}
	(void)memset_s(alloc_ret.r, sizeof(*phys_range_info_allocated), 0,
		       sizeof(*phys_range_info_allocated));
	phys_range_info_allocated = (ffa_memory_phys_range_t *)alloc_ret.r;

	phys_range_info_allocated->phys_range = phys_range;
	phys_range_info_allocated->capacity   = phys_range_capacity;
	phys_range_info_allocated->count      = phys_range_count;
	phys_range_info_allocated->total_size = total_size;
	list_insert_at_tail(&share->phys_range_list,
			    &phys_range_info_allocated->node);

	share->total_size += total_size;

	assert(phys_range_info != NULL);
	*phys_range_info = phys_range_info_allocated;

	ffa_ret = FFA_RET_SUCCESS;
out:
	return ffa_ret;
}

static ffa_ret_t
ffa_memory_add_constituent_mrds(
	ffa_share_t *share, addrspace_t *addrspace,
	const ffa_constituent_memory_region_desc_t *constituent_mrd,
	count_t					    constituent_mrd_count,
	ffa_memory_phys_range_t			  **phys_range_info)
{
	ffa_ret_t ffa_ret;

	assert(share != NULL);
	assert(constituent_mrd_count > 0U);
	assert(atomic_load_relaxed(&share->state) ==
	       FFA_MEMORY_STATE_CONSTRUCTING);

	partition_t *partition = addrspace->header.partition;

	count_t	      phys_range_capacity = 0U;
	count_t	      phys_range_count	  = 0U;
	phys_range_t *phys_range	  = NULL;
	size_t	      total_size	  = 0U;

	index_t idx    = 0U;
	size_t	offset = 0U;
	while (idx < constituent_mrd_count) {
		if (phys_range_count == phys_range_capacity) {
			// Dynamically expand the physical range buffer. For the
			// first allocation, the physical range count is always
			// not less than constituent_mrd_count, for the next
			// allocation, 5U may be a good step.
			count_t new_capacity =
				(phys_range_capacity == 0U)
					? constituent_mrd_count
					: (phys_range_capacity + 5U);
			phys_range = realloc_phys_ranges(partition, phys_range,
							 phys_range_capacity,
							 new_capacity);
			if (phys_range == NULL) {
				ffa_ret = FFA_RET_NO_MEMORY;
				goto out_revert_phys_range;
			}
			phys_range_capacity = new_capacity;
		}

		vmaddr_t vbase_idx = (vmaddr_t)constituent_mrd[idx].address;
		size_t	 vsize_idx = (size_t)constituent_mrd[idx].page_count
				     << FFA_CONSTITUENT_MRD_PAGE_SHIFT;
		vmaddr_t vbase	   = vbase_idx + offset;
		size_t	 vsize	   = vsize_idx - offset;

		phys_range_t *added_range = &phys_range[phys_range_count];

		ffa_ret = ffa_memory_add_constituent_mrd(
			addrspace, share, vbase, vsize, added_range);
		if (ffa_ret != FFA_RET_SUCCESS) {
			goto out_revert_phys_range;
		}

		phys_range_count++;

		total_size += added_range->size;
		offset += added_range->size;
		if (offset == vsize_idx) {
			offset = 0U;
			idx++;
		}
	}

	ffa_ret = ffa_memory_insert_phys_ranges(partition, share, phys_range,
						phys_range_capacity,
						phys_range_count, total_size,
						phys_range_info);
	if (ffa_ret != FFA_RET_SUCCESS) {
		goto out_revert_phys_range;
	}

	ffa_ret = FFA_RET_SUCCESS;
	goto out;

out_revert_phys_range:
	// Rollback: return donated memory to parent memextent.
	for (index_t i = 0U; i < phys_range_count; i++) {
		ffa_memory_remove_constituent_mrd(share, &phys_range[i], false);
	}
	if (phys_range != NULL) {
		partition_free(partition, phys_range,
			       phys_range_capacity * sizeof(*phys_range));
	}
out:
	return ffa_ret;
}

static void
ffa_memory_delete_phys_range(ffa_share_t *share, partition_t *partition,
			     ffa_memory_phys_range_t *phys_range_info,
			     bool		      mem_zero)
{
	assert(share != NULL);

	(void)list_delete_node(&share->phys_range_list, &phys_range_info->node);

	phys_range_t *phys_range = phys_range_info->phys_range;

	for (index_t i = 0U; i < phys_range_info->count; i++) {
		ffa_memory_remove_constituent_mrd(share, &phys_range[i],
						  mem_zero);
	}

	share->total_size -= phys_range_info->total_size;

	partition_free(partition, phys_range,
		       sizeof(*phys_range) * phys_range_info->capacity);
	partition_free(partition, phys_range_info, sizeof(*phys_range_info));
}

static void
ffa_memory_delete(addrspace_t *addrspace, ffa_share_t *share, bool mem_zero)
{
	spinlock_acquire(&addrspace->ffa_memory_lock);
	(void)list_delete_node(&addrspace->ffa_memory_list, &share->node);
	spinlock_release(&addrspace->ffa_memory_lock);

	count_t count = atomic_fetch_sub_explicit(
		&addrspace->ffa_memory_handles, 1U, memory_order_relaxed);
	assert(count > 0U);

	partition_t *partition = addrspace->header.partition;

	// Clean up all physical range entries.
	LIST_FOREACH_CONTAINER_BEGIN(ffa_memory_phys_range_t,
				     &share->phys_range_list,
				     ffa_memory_phys_range, node,
				     phys_range_info)
		ffa_memory_delete_phys_range(share, partition, phys_range_info,
					     mem_zero);
	LIST_FOREACH_CONTAINER_END
	assert(share->total_size == 0U);

	// Delete the me.
	object_put_memextent(share->me);

	partition_free(partition, share, sizeof(*share));

	object_put_addrspace(addrspace);
}

static ffa_memory_handle_t
ffa_memory_get_handle(uint32_t rx, uint32_t ry)
{
	ffa_memory_handle_encode_t handle_encode =
		ffa_memory_handle_encode_default();
	ffa_memory_handle_encode_set_rx(&handle_encode, rx);
	ffa_memory_handle_encode_set_ry(&handle_encode, ry);

	return ffa_memory_handle_cast(
		ffa_memory_handle_encode_raw(handle_encode));
}

static ffa_ret_t
ffa_memory_check_first_fragment_args(smccc_function_id_t fn_id,
				     const register_t (*args)[SMCCC_1_2_ARGS],
				     ffa_rxtx_buffer_t *vm_rxtx_buffer,
				     ffa_trans_type_t  *trans_type,
				     size_t	       *total_length,
				     size_t	       *fragment_length)
{
	ffa_ret_t ffa_ret;

	size_t vm_tx_buffer_size;

	spinlock_acquire(&vm_rxtx_buffer->lock);

	if (!ffa_rxtx_buffer_is_valid(vm_rxtx_buffer)) {
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
		spinlock_release(&vm_rxtx_buffer->lock);
		goto out;
	}
	vm_tx_buffer_size = ffa_rxtx_buffer_size(vm_rxtx_buffer);

	spinlock_release(&vm_rxtx_buffer->lock);

	ffa_trans_type_t l_trans_type;
	smccc_function_t function = smccc_function_id_get_function(&fn_id);
	if (function == FFA_FUNCTION_FFA_MEM_LEND) {
		l_trans_type = FFA_TRANS_TYPE_LEND;
	} else if (function == FFA_FUNCTION_FFA_MEM_SHARE) {
		l_trans_type = FFA_TRANS_TYPE_SHARE;
	} else {
		ffa_ret = FFA_RET_NOT_SUPPORTED;
		goto out;
	}

	size_t	 l_total_length	   = (uint32_t)(*args)[0];
	size_t	 l_fragment_length = (uint32_t)(*args)[1];
	vmaddr_t buffer_address	   = smccc_function_id_get_is_smc64(&fn_id)
					     ? (*args)[2]
					     : (uint64_t)(uint32_t)(*args)[2];
	count_t	 page_count	   = (uint32_t)(*args)[3];

	if ((buffer_address != 0U) || (page_count != 0U)) {
		LOG(ERROR, WARN,
		    "FF-A: memory operation with external buffer not supported.");
		ffa_ret = FFA_RET_NOT_SUPPORTED;
		goto out;
	}
	if ((l_fragment_length > vm_tx_buffer_size) ||
	    (l_total_length < l_fragment_length)) {
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
		goto out;
	}

	*trans_type	 = l_trans_type;
	*total_length	 = l_total_length;
	*fragment_length = l_fragment_length;

	ffa_ret = FFA_RET_SUCCESS;
out:
	return ffa_ret;
}

static ffa_ret_t
ffa_memory_forward_first_fragment(
	smccc_function_id_t fn_id, register_t (*args)[SMCCC_1_2_ARGS],
	register_t (*ret)[SMCCC_1_2_RETS], vmid_t vmid, bool identity_mapping,
	bool last_fragment, void *fragment_buffer, size_t fragment_length,
	size_t composite_mrd_offset, size_t constituent_mrd_offset,
	count_t constituent_mrd_count, ffa_memory_phys_range_t *phys_range_info,
	ffa_memory_handle_t *handle_ret)
{
	ffa_ret_t ffa_ret;

	uint8_t *hyp_tx_buffer;
	size_t	 hyp_rxtx_buffer_size;
	ffa_hyp_tx_buffer_get_tx(&hyp_tx_buffer, &hyp_rxtx_buffer_size);

	// The RXTX buffer between hypervisor and SPMC must be acquired before
	// copying to it.
	if (!ffa_acquire_hyp_tx_buffer()) {
		ffa_ret = FFA_RET_BUSY;
		goto out;
	}

	if (identity_mapping) {
		// Identity mapping: forward descriptor as-is to SPMC.
		size_t copied_size = memscpy((void *)hyp_tx_buffer,
					     hyp_rxtx_buffer_size,
					     fragment_buffer, fragment_length);
		assert(copied_size == fragment_length);
	} else {
		// Non-identity mapping: IPA was translated to PA and descriptor
		// size should be adjusted accordingly.
		ffa_ret_t setup_ret = ffa_memory_transaction_desc_generate(
			(ffa_memory_transaction_desc_t *)(uintptr_t)
				hyp_tx_buffer,
			hyp_rxtx_buffer_size,
			(ffa_memory_transaction_desc_t *)fragment_buffer,
			composite_mrd_offset, constituent_mrd_offset,
			phys_range_info);
		if (setup_ret != FFA_RET_SUCCESS) {
			ffa_release_hyp_tx_buffer();
			ffa_ret = setup_ret;
			goto out;
		}

		size_t new_items = (size_t)phys_range_info->count -
				   (size_t)constituent_mrd_count;
		size_t new_added_length =
			new_items *
			sizeof(ffa_constituent_memory_region_desc_t);
		size_t new_fragment_length = fragment_length + new_added_length;

		// Set the new lengths
		(*args)[0] = new_fragment_length; // New total length
		(*args)[1] = new_fragment_length; // New fragment length
	}
	// Forward translated descriptor to SPMC.
	ffa_smccc_call(fn_id, args, ret);

	ffa_release_hyp_tx_buffer();

	smccc_function_id_t fn_id_ret =
		smccc_function_id_cast((uint32_t)(*ret)[0]);
	smccc_function_t function_ret =
		smccc_function_id_get_function(&fn_id_ret);

	ffa_memory_handle_t handle;
	if (function_ret == FFA_FUNCTION_FFA_ERROR) {
		// SPMC returned an error; propagate it to the caller.
		ffa_ret = ffa_ret_cast((uint32_t)(*ret)[2]);
		goto out;
	} else if (!last_fragment &&
		   (function_ret == FFA_FUNCTION_FFA_MEM_FRAG_RX)) {
		// Success but more fragments required, save the handle
		handle = ffa_memory_get_handle((uint32_t)(*ret)[1],
					       (uint32_t)(*ret)[2]);
		uint32_t     fragment_offset = (uint32_t)(*ret)[3];
		ffa_ep_ids_t ep_ids = ffa_ep_ids_cast((uint32_t)(*ret)[4]);
		ffa_partition_id_t part_id = ffa_ep_ids_get_src_id(&ep_ids);

		if (ffa_memory_handle_get_hyp_alloc(&handle) ||
		    (fragment_offset != fragment_length) ||
		    !ffa_partition_id_is_equal(ffa_vmid_to_partid(vmid),
					       part_id)) {
			// Unexpected result
			LOG(ERROR, WARN, "FF-A: {:#x} returned {:#x}.",
			    smccc_function_id_raw(fn_id), function_ret);
			panic("FF-A: bad SPMC result");
		}
		// MBZ at any virtual FF-A instance. Returning a FRAG_RX with an
		// owner ID of 0 indicates to the VM that a hypervisor is
		// present, and that the instance formed with the current VM is
		// a virtual FF-A instance.
		(*ret)[4] = 0U;

		ffa_ret = FFA_RET_SUCCESS;
	} else if (last_fragment &&
		   (function_ret == FFA_FUNCTION_FFA_SUCCESS)) {
		// Successful send, save the handle
		handle = ffa_memory_get_handle((uint32_t)(*ret)[2],
					       (uint32_t)(*ret)[3]);
		if (ffa_memory_handle_get_hyp_alloc(&handle)) {
			panic("FF-A: bad SPMC result");
		}

		ffa_ret = FFA_RET_SUCCESS;
	} else {
		// Unexpected result
		LOG(ERROR, WARN, "FF-A: frag tx returned {:#x}.", function_ret);
		panic("FF-A: bad SPMC result");
	}

	assert_debug(handle_ret != NULL);
	*handle_ret = handle;
out:
	return ffa_ret;
}

void
ffa_call_handle_ffa_mem_lend_share(smccc_function_id_t fn_id,
				   const register_t (*args)[SMCCC_1_2_ARGS],
				   register_t (*ret)[SMCCC_1_2_RETS])
{
	ffa_ret_t    ffa_ret;
	thread_t    *current   = thread_get_self();
	addrspace_t *addrspace = current->addrspace;

	// ret is a zero initialized hypervisor buffer, use it for input
	// registers. We do this so we don't have to check for and zero out any
	// additional arguments (e.g. as defined in future FF-A versions) that
	// may have been put in registers by the caller before relaying the
	// call to the SPMC.
	(*ret)[1] = (uint32_t)(*args)[0]; // w1 total length
	(*ret)[2] = (uint32_t)(*args)[1]; // w2 fragment lenfth
	(*ret)[3] = (*args)[2];		  // w3/x3 buffer address
	(*ret)[4] = (uint32_t)(*args)[3]; // w4 page count
	register_t(*hyp_args)[SMCCC_1_2_ARGS] =
		(register_t(*)[SMCCC_1_2_ARGS])(void *)&(*ret)[1];

	ffa_rxtx_buffer_t *vm_rxtx_buffer = &addrspace->ffa_rxtx_buffer;

	ffa_trans_type_t trans_type;
	size_t		 total_length;
	size_t		 fragment_length;

	ffa_ret = ffa_memory_check_first_fragment_args(
		fn_id, hyp_args, vm_rxtx_buffer, &trans_type, &total_length,
		&fragment_length);
	if (ffa_ret != FFA_RET_SUCCESS) {
		goto out_error;
	}

	// The ownership of the TX buffer is transferred from the Producer to
	// the Consumer upon invocation of an FF-A ABI that uses the TX buffer.
	// Completion of the invocation of the FF-A ABI transfers the ownership
	// of the TX buffer back to the Producer. So we can assume the sender
	// never modify the TX buffer until current call completes.
	if (!ffa_acquire_tx_buffer(vm_rxtx_buffer, fragment_length)) {
		// It is the VM's responsibility to make sure the send buffer is
		// not occupied.
		ffa_ret = FFA_RET_BUSY;
		goto out_error;
	}

	ffa_memory_transaction_desc_t	     *mtd;
	ffa_memory_access_desc_t	     *mad;
	count_t				      mad_count;
	ffa_composite_memory_region_desc_t   *composite_mrd;
	ffa_constituent_memory_region_desc_t *constituent_mrd;
	count_t				      constituent_mrd_count;

	// tx_buffer_hyp_private must be protected by is_tx_busy.
	uint8_t *tx_buffer_hyp_private =
		ffa_rxtx_buffer_get_hyp_private(vm_rxtx_buffer);

	bool get_all_descs_ret = ffa_memory_get_all_descs(
		tx_buffer_hyp_private, fragment_length, &mtd, &mad, &mad_count,
		&composite_mrd, &constituent_mrd, &constituent_mrd_count);
	if (!get_all_descs_ret) {
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
		goto out_release_vm_tx;
	}

	ffa_share_t *share = NULL;

	ffa_ret = ffa_memory_construct(addrspace, trans_type, mtd, mad,
				       mad_count, composite_mrd,
				       &constituent_mrd[0], &share);
	if (ffa_ret != FFA_RET_SUCCESS) {
		goto out_release_vm_tx;
	}

	atomic_store_relaxed(&share->state, FFA_MEMORY_STATE_CONSTRUCTING);
	share->total_msg_len_expect = total_length;

	ffa_memory_phys_range_t *phys_range_info = NULL;

	ffa_ret = ffa_memory_add_constituent_mrds(share, addrspace,
						  constituent_mrd,
						  constituent_mrd_count,
						  &phys_range_info);
	if (ffa_ret != FFA_RET_SUCCESS) {
		goto out_memory_delete;
	}

	size_t total_msg_len = fragment_length;
	bool   last_fragment = total_msg_len == share->total_msg_len_expect;

	// Verify all memory regions are sent properly when message is complete.
	if (last_fragment && (share->total_size != share->total_size_expect)) {
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
		goto out_memory_delete;
	}

	size_t composite_mrd_offset =
		(uintptr_t)composite_mrd - (uintptr_t)tx_buffer_hyp_private;
	size_t constituent_mrd_offset =
		(uintptr_t)constituent_mrd - (uintptr_t)tx_buffer_hyp_private;
	ffa_memory_handle_t handle;

	ffa_ret = ffa_memory_forward_first_fragment(
		fn_id, hyp_args, ret, addrspace->vmid, share->identity_mapping,
		last_fragment, (void *)tx_buffer_hyp_private, fragment_length,
		composite_mrd_offset, constituent_mrd_offset,
		constituent_mrd_count, phys_range_info, &handle);
	if (ffa_ret != FFA_RET_SUCCESS) {
		goto out_memory_delete;
	}

	LOG(DEBUG, INFO, "FF-A: memory handle {:#x} was created.",
	    ffa_memory_handle_raw(handle));

	// Handle must be unique and not already exist.
	ffa_share_t *lookup = ffa_memory_lookup(addrspace, handle);
	if (lookup != NULL) {
		// Handle already exists, could be SPMC or Hypervisor error?
		panic("FF-A: duplicate handle received");
	}

	share->handle	     = handle;
	share->total_msg_len = total_msg_len;
	atomic_store_relaxed(&share->state, FFA_MEMORY_STATE_CONSTRUCTED);

	ffa_release_tx_buffer(vm_rxtx_buffer, fragment_length);

	// ret is already set on successful forwarding SPMC.
	goto out;

out_memory_delete:
	ffa_memory_delete(addrspace, share, false);
out_release_vm_tx:
	ffa_release_tx_buffer(vm_rxtx_buffer, fragment_length);
out_error:
	if (ffa_ret != FFA_RET_SUCCESS) {
		ffa_set_error(ffa_ret, ret);
	}
out:
	return;
}

static ffa_ret_t
ffa_memory_check_next_fragment_args(const register_t (*args)[SMCCC_1_2_ARGS],
				    ffa_rxtx_buffer_t	*vm_rxtx_buffer,
				    ffa_memory_handle_t *handle,
				    size_t		*fragment_length)
{
	ffa_ret_t ffa_ret;

	size_t vm_tx_buffer_size;

	spinlock_acquire(&vm_rxtx_buffer->lock);

	if (!ffa_rxtx_buffer_is_valid(vm_rxtx_buffer)) {
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
		spinlock_release(&vm_rxtx_buffer->lock);
		goto out;
	}
	vm_tx_buffer_size = ffa_rxtx_buffer_size(vm_rxtx_buffer);

	spinlock_release(&vm_rxtx_buffer->lock);

	ffa_memory_handle_t l_handle = ffa_memory_get_handle(
		(uint32_t)(*args)[0], (uint32_t)(*args)[1]);
	size_t l_fragment_length = (uint32_t)(*args)[2];

	// MBZ at any virtual FF-A instance.
	if ((uint32_t)(*args)[3] != 0U) {
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
		goto out;
	}

	if ((l_fragment_length > vm_tx_buffer_size) ||
	    ((l_fragment_length %
	      sizeof(ffa_constituent_memory_region_desc_t)) != 0U)) {
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
		goto out;
	}

	*handle		 = l_handle;
	*fragment_length = l_fragment_length;

	ffa_ret = FFA_RET_SUCCESS;
out:
	return ffa_ret;
}

static ffa_ret_t
ffa_memory_forward_next_fragment(
	smccc_function_id_t fn_id, register_t (*args)[SMCCC_1_2_ARGS],
	register_t (*ret)[SMCCC_1_2_RETS], vmid_t vmid, bool last_fragment,
	size_t received_total_msg_len, const void *fragment_buffer,
	size_t fragment_length, ffa_memory_handle_t handle)
{
	ffa_ret_t ffa_ret;

	uint8_t *hyp_tx_buffer;
	size_t	 hyp_rxtx_buffer_size;

	ffa_partition_id_t part_id = ffa_vmid_to_partid(vmid);
	ffa_ep_ids_t	   ep_ids  = ffa_ep_ids_default();
	ffa_ep_ids_set_src_id(&ep_ids, part_id);

	// Hypervisor is responsible for setting the ID of owner endpoint.
	(*args)[3] = ffa_ep_ids_raw(ep_ids);

	// It is safe to get hyp_tx_buffer as the buffer address and size never
	// changes once created.
	ffa_hyp_tx_buffer_get_tx(&hyp_tx_buffer, &hyp_rxtx_buffer_size);

	// The RXTX buffer between hypervisor and SPMC must be acquired before
	// copying to it.
	if (!ffa_acquire_hyp_tx_buffer()) {
		ffa_ret = FFA_RET_BUSY;
		goto out;
	}

	// Copy fragment from VM's buffer and forward to SPMC.
	size_t copied_size = memscpy((void *)hyp_tx_buffer,
				     hyp_rxtx_buffer_size, fragment_buffer,
				     fragment_length);
	assert(copied_size == fragment_length);

	ffa_smccc_call(fn_id, args, ret);

	ffa_release_hyp_tx_buffer();

	// Process the SPMC's response, which could be a request for the next
	// fragment (FFA_MEM_FRAG_RX) or a success code on completion.
	smccc_function_id_t fn_id_ret =
		smccc_function_id_cast((uint32_t)(*ret)[0]);
	smccc_function_t function_ret =
		smccc_function_id_get_function(&fn_id_ret);

	if (function_ret == FFA_FUNCTION_FFA_ERROR) {
		// SPMC returned an error; propagate it to the caller.
		ffa_ret = ffa_ret_cast((uint32_t)(*ret)[2]);
	} else if (!last_fragment &&
		   (function_ret == FFA_FUNCTION_FFA_MEM_FRAG_RX)) {
		// Success but more fragments required
		ffa_memory_handle_t handle_ret = ffa_memory_get_handle(
			(uint32_t)(*ret)[1], (uint32_t)(*ret)[2]);
		uint32_t     fragment_offset = (uint32_t)(*ret)[3];
		ffa_ep_ids_t ep_ids_ret = ffa_ep_ids_cast((uint32_t)(*ret)[4]);

		// Sanity check the returned handle and Ids
		if (!ffa_memory_handle_is_equal(handle_ret, handle) ||
		    (fragment_offset != received_total_msg_len) ||
		    !ffa_ep_ids_is_equal(ep_ids, ep_ids_ret)) {
			// Unexpected result
			LOG(ERROR, WARN,
			    "FF-A: {:#x} returned invalid rets {:#x}, {:#x}, {:#x}.",
			    smccc_function_id_raw(fn_id),
			    ffa_memory_handle_raw(handle_ret), fragment_offset,
			    ffa_ep_ids_raw(ep_ids));
			panic("FF-A: bad SPMC result");
		} else {
			// MBZ at any virtual FF-A instance.
			(*ret)[4] = 0U;

			ffa_ret = FFA_RET_SUCCESS;
		}
	} else if ((last_fragment &&
		    (function_ret == FFA_FUNCTION_FFA_SUCCESS))) {
		ffa_ret = FFA_RET_SUCCESS;
	} else {
		// Unexpected result
		LOG(ERROR, WARN, "FF-A: frag tx returned {:#x}.", function_ret);
		panic("FF-A: bad SPMC result");
	}

out:
	return ffa_ret;
}

void
ffa_call_handle_ffa_mem_frag_tx(smccc_function_id_t fn_id,
				const register_t (*args)[SMCCC_1_2_ARGS],
				register_t (*ret)[SMCCC_1_2_RETS])
{
	ffa_ret_t ffa_ret;

	thread_t *current = thread_get_self();
	assert(current != NULL);
	addrspace_t *addrspace = current->addrspace;
	assert(addrspace != NULL);

	// ret is a zero initialized hypervisor buffer, use it for input
	// registers. We do this so we don't have to check for and zero out any
	// additional arguments (e.g. as defined in future FF-A versions) that
	// may have been put in registers by the caller before relaying the
	// call to the SPMC.
	(*ret)[1] = (uint32_t)(*args)[0]; // w1 handle
	(*ret)[2] = (uint32_t)(*args)[1]; // w2 handle
	(*ret)[3] = (uint32_t)(*args)[2]; // w3 fragment offset
	(*ret)[4] = (uint32_t)(*args)[3]; // w4 endpoint id
	register_t(*hyp_args)[SMCCC_1_2_ARGS] =
		(register_t(*)[SMCCC_1_2_ARGS])(void *)&(*ret)[1];

	ffa_rxtx_buffer_t  *vm_rxtx_buffer = &addrspace->ffa_rxtx_buffer;
	vmid_t		    vmid	   = addrspace->vmid;
	ffa_memory_handle_t handle;
	size_t		    fragment_length;

	ffa_ret = ffa_memory_check_next_fragment_args(
		hyp_args, vm_rxtx_buffer, &handle, &fragment_length);
	if (ffa_ret != FFA_RET_SUCCESS) {
		goto out_error;
	}

	if (!ffa_acquire_tx_buffer(vm_rxtx_buffer, fragment_length)) {
		// It is the VM's responsibility to make sure the send buffer is
		// not occupied.
		ffa_ret = FFA_RET_BUSY;
		goto out_error;
	}

	ffa_share_t *share = NULL;

	ffa_ret = ffa_memory_lookup_and_update_state(
		addrspace, handle, FFA_MEMORY_STATE_CONSTRUCTED,
		FFA_MEMORY_STATE_CONSTRUCTING, &share);
	if (ffa_ret != FFA_RET_SUCCESS) {
		goto out_release_vm_tx;
	}

	// Per hypervisor's implementation, fragmented sharing is only supported
	// for identity-mapped memory regions.
	if (!share->identity_mapping) {
		ffa_ret = FFA_RET_NOT_SUPPORTED;
		goto out_release_memory_state;
	}

	size_t total_msg_len = share->total_msg_len + fragment_length;
	bool   last_fragment = total_msg_len == share->total_msg_len_expect;

	if (total_msg_len > share->total_msg_len_expect) {
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
		goto out_release_memory_state;
	}

	// tx_buffer_hyp_private must be protected by is_tx_busy.
	uint8_t *tx_buffer_hyp_private =
		ffa_rxtx_buffer_get_hyp_private(vm_rxtx_buffer);

	ffa_constituent_memory_region_desc_t *constituent_mrd =
		(ffa_constituent_memory_region_desc_t *)(uintptr_t)
			tx_buffer_hyp_private;
	count_t constituent_mrd_count =
		(count_t)(fragment_length /
			  sizeof(ffa_constituent_memory_region_desc_t));

	ffa_memory_phys_range_t *phys_range_info = NULL;

	// Process and add the new constituent memory regions to the
	// transaction.
	ffa_ret = ffa_memory_add_constituent_mrds(share, addrspace,
						  constituent_mrd,
						  constituent_mrd_count,
						  &phys_range_info);
	if (ffa_ret != FFA_RET_SUCCESS) {
		goto out_release_memory_state;
	}

	if (last_fragment && (share->total_size != share->total_size_expect)) {
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
		goto out_delete_phys_range;
	}
	if (share->total_size > share->total_size_expect) {
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
		goto out_delete_phys_range;
	}

	ffa_ret = ffa_memory_forward_next_fragment(
		fn_id, hyp_args, ret, vmid, last_fragment, total_msg_len,
		(void *)tx_buffer_hyp_private, fragment_length, handle);
	if (ffa_ret != FFA_RET_SUCCESS) {
		goto out_delete_phys_range;
	}

	share->total_msg_len = total_msg_len;
	atomic_store_release(&share->state, FFA_MEMORY_STATE_CONSTRUCTED);
	ffa_release_tx_buffer(vm_rxtx_buffer, fragment_length);

	// ret is already set on successful forwarding SPMC.
	goto out;

out_delete_phys_range:
	ffa_memory_delete_phys_range(share, addrspace->header.partition,
				     phys_range_info, false);
out_release_memory_state:
	atomic_store_release(&share->state, FFA_MEMORY_STATE_CONSTRUCTED);
out_release_vm_tx:
	ffa_release_tx_buffer(vm_rxtx_buffer, fragment_length);
out_error:
	if (ffa_ret != FFA_RET_SUCCESS) {
		ffa_set_error(ffa_ret, ret);
	}
out:
	return;
}

void
ffa_call_handle_ffa_mem_reclaim(smccc_function_id_t fn_id,
				const register_t (*args)[SMCCC_1_2_ARGS],
				register_t (*ret)[SMCCC_1_2_RETS])
{
	ffa_ret_t ffa_ret;

	// ret is a zero initialized hypervisor buffer, use it for input
	// registers. We do this so we don't have to check for and zero out any
	// additional arguments (e.g. as defined in future FF-A versions) that
	// may have been put in registers by the caller before relaying the
	// call to the SPMC.
	(*ret)[1] = (uint32_t)(*args)[0]; // w1 handle
	(*ret)[2] = (uint32_t)(*args)[1]; // w2 handle
	(*ret)[3] = (uint32_t)(*args)[2]; // w3 flags
	const register_t(*hyp_args)[SMCCC_1_2_ARGS] =
		(const register_t(*)[SMCCC_1_2_ARGS])(void *)&(*ret)[1];

	ffa_memory_handle_t handle = ffa_memory_get_handle(
		(uint32_t)(*hyp_args)[0], (uint32_t)(*hyp_args)[1]);
	ffa_memory_send_flags_t flags =
		ffa_memory_send_flags_cast((uint32_t)(*hyp_args)[2]);

	// We assume handles must be allocated by the SPMC.
	if (ffa_memory_handle_get_hyp_alloc(&handle)) {
		ffa_ret = FFA_RET_INVALID_PARAMETERS;
		goto out_error;
	}

	thread_t    *current   = thread_get_self();
	addrspace_t *addrspace = current->addrspace;

	ffa_share_t *share = NULL;

	// Find the ffa_share and atomically update its state to RECLAIMING to
	// prevent concurrent access.
	ffa_ret = ffa_memory_lookup_and_update_state(
		addrspace, handle, FFA_MEMORY_STATE_CONSTRUCTED,
		FFA_MEMORY_STATE_RECLAIMING, &share);
	if (ffa_ret != FFA_RET_SUCCESS) {
		goto out_error;
	}

	ffa_ret = ffa_memory_send_flags_checks(flags, share->access, false);
	if (ffa_ret != FFA_RET_SUCCESS) {
		goto out_release_memory_state;
	}

	bool mem_zero = ffa_memory_send_flags_get_zero(&flags);
	if (mem_zero) {
		// Clear the zero flag to SPMC, we will do the zeroing
		ffa_memory_send_flags_set_zero(&flags, false);
		(*ret)[3] = ffa_memory_send_flags_raw(flags);
	}

	// Forward the reclaim call to the SPMC. The hypervisor trusts the SPMC
	// to manage the reclaim process with all borrowers and only returns
	// success after the SPMC confirms completion.
	ffa_smccc_call(fn_id, hyp_args, ret);

	smccc_function_id_t reclaim_fn_id_ret =
		smccc_function_id_cast((uint32_t)(*ret)[0]);
	if (smccc_function_id_get_function(&reclaim_fn_id_ret) !=
	    FFA_FUNCTION_FFA_SUCCESS) {
		// SPMC reported an error; propagate it to the caller.
		ffa_ret = FFA_RET_SUCCESS;
		goto out_release_memory_state;
	}

	LOG(DEBUG, INFO, "FF-A: memory handle {:#x} was reclaimed.",
	    ffa_memory_handle_raw(handle));

	// SPMC confirmed reclaim; perform final cleanup.
	ffa_memory_delete(addrspace, share, mem_zero);

	goto out;

out_release_memory_state:
	atomic_store_release(&share->state, FFA_MEMORY_STATE_CONSTRUCTED);
out_error:
	if (ffa_ret != FFA_RET_SUCCESS) {
		ffa_set_error(ffa_ret, ret);
	}
out:
	return;
}

error_t
ffa_memory_cleanup(addrspace_t *addrspace)
{
	error_t ret;

	while (true) {
		ffa_share_t *share;

		spinlock_acquire(&addrspace->ffa_memory_lock);
		list_node_t *node = list_get_head(&addrspace->ffa_memory_list);
		if (node != NULL) {
			share = ffa_share_container_of_node(node);

			ffa_memory_state_t expected_state =
				FFA_MEMORY_STATE_CONSTRUCTED;
			bool success = atomic_compare_exchange_strong_explicit(
				&share->state, &expected_state,
				FFA_MEMORY_STATE_RECLAIMING,
				memory_order_acquire, memory_order_relaxed);
			if (!success) {
				ret = ERROR_RETRY;
				spinlock_release(&addrspace->ffa_memory_lock);
				goto out;
			}
		} else {
			share = NULL;
		}
		spinlock_release(&addrspace->ffa_memory_lock);

		if (share == NULL) {
			// Reached end of the list
			ret = OK;
			break;
		}

		// Use default flags, we don't need SPMC to zero the memory.
		ffa_memory_send_flags_t flags = ffa_memory_send_flags_default();

		smccc_function_id_t reclaim_fn_id = smccc_create_fn_id(
			FFA_FUNCTION_FFA_MEM_RECLAIM, SMCCC_OWNER_ID_STANDARD,
			false, true);
		register_t hyp_ret[SMCCC_1_2_RETS] = { 0 };
		register_t(*hyp_args)[SMCCC_1_2_ARGS] =
			(register_t(*)[SMCCC_1_2_ARGS])(void *)&hyp_ret[1];

		ffa_memory_handle_encode_t handle =
			ffa_memory_handle_encode_cast(
				ffa_memory_handle_raw(share->handle));
		(*hyp_args)[0] = ffa_memory_handle_encode_get_rx(&handle);
		(*hyp_args)[1] = ffa_memory_handle_encode_get_ry(&handle);
		(*hyp_args)[2] = ffa_memory_send_flags_raw(flags);

		// Send the reclaim to the SPMC
		ffa_smccc_call(reclaim_fn_id, hyp_args, &hyp_ret);

		smccc_function_id_t reclaim_fn_id_ret =
			smccc_function_id_cast((uint32_t)hyp_ret[0]);
		if (smccc_function_id_get_function(&reclaim_fn_id_ret) ==
		    FFA_FUNCTION_FFA_ERROR) {
			ffa_ret_t ffa_ret =
				ffa_ret_raw_cast((int32_t)hyp_ret[2]);
			if (ffa_ret == FFA_RET_INVALID_PARAMETERS) {
				ret = ERROR_ARGUMENT_INVALID;
			} else if (ffa_ret == FFA_RET_DENIED) {
				ret = ERROR_DENIED;
			} else if (ffa_ret == FFA_RET_NO_MEMORY) {
				ret = ERROR_NOMEM;
			} else {
				// ran into an unexpected error. (ffa_ret ==
				// FFA_RET_ABORTED) or other illegal error code.
				ret = ERROR_FAILURE;
			}
			atomic_store_release(&share->state,
					     FFA_MEMORY_STATE_CONSTRUCTED);
			goto out;
		}

		ffa_memory_delete(addrspace, share, false);
	}

out:
	return ret;
}

void
ffa_memory_deactivate_addrspace(addrspace_t *addrspace)
{
	// As each ffa_share has a reference to its addrspace, it should not be
	// possible for the addrspace to be destroyed until all ffa_share have
	// been reclaimed.
	assert(atomic_load_relaxed(&addrspace->ffa_memory_handles) == 0U);
	assert(list_is_empty(&addrspace->ffa_memory_list));
}
