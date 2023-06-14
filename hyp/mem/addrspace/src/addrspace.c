// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
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
#include <bitmap.h>
#include <compiler.h>
#include <cpulocal.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <gpt.h>
#include <hyp_aspace.h>
#include <list.h>
#include <memextent.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <pgtable.h>
#include <qcbor.h>
#include <spinlock.h>

#include <events/addrspace.h>

#include "event_handlers.h"

// FIXME: This file contains architecture specific details and should be
// refactored.

extern VTTBR_EL2_t hlos_vm_vttbr;

// FIXME: Limit VMIDs to reduce bitmap size
#if defined(ARCH_ARM_FEAT_VMID16)
#define NUM_VMIDS 256U
#else
#define NUM_VMIDS 256U
#endif

static _Atomic BITMAP_DECLARE(NUM_VMIDS, addrspace_vmids);

static_assert(ADDRSPACE_INFO_AREA_LAYOUT_SIZE <= MAX_VM_INFO_AREA_SIZE,
	      "Address space information area too small");

void
addrspace_handle_boot_cold_init(void)
{
	// Reserve VMID 0
	bool already_set = bitmap_atomic_test_and_set(addrspace_vmids, 0U,
						      memory_order_relaxed);
	assert(!already_set);
}

#if defined(INTERFACE_VCPU)
void
addrspace_context_switch_load(void)
{
	thread_t *thread = thread_get_self();

	if (compiler_expected(thread->kind == THREAD_KIND_VCPU)) {
		pgtable_vm_load_regs(&thread->addrspace->vm_pgtable);
	}
}

static void
addrspace_detach_thread(thread_t *thread)
{
	assert(thread != NULL);
	assert(thread->kind == THREAD_KIND_VCPU);
	assert(thread->addrspace != NULL);

	addrspace_t *addrspace = thread->addrspace;

	bitmap_atomic_clear(addrspace->stack_bitmap, thread->stack_map_index,
			    memory_order_relaxed);
	thread->addrspace = NULL;
	object_put_addrspace(addrspace);
}

error_t
addrspace_attach_thread(addrspace_t *addrspace, thread_t *thread)
{
	assert(thread != NULL);
	assert(addrspace != NULL);
	assert(atomic_load_relaxed(&addrspace->header.state) ==
	       OBJECT_STATE_ACTIVE);
	assert(atomic_load_relaxed(&thread->header.state) == OBJECT_STATE_INIT);

	error_t ret = OK;

	if (thread->kind != THREAD_KIND_VCPU) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	index_t stack_index;
	do {
		if (!bitmap_atomic_ffc(addrspace->stack_bitmap,
				       ADDRSPACE_MAX_THREADS, &stack_index)) {
			ret = ERROR_NOMEM;
			goto out;
		}
	} while (bitmap_atomic_test_and_set(addrspace->stack_bitmap,
					    stack_index, memory_order_relaxed));

	if (thread->addrspace != NULL) {
		addrspace_detach_thread(thread);
	}

	thread->addrspace	= object_get_addrspace_additional(addrspace);
	thread->stack_map_index = stack_index;

	trace_ids_set_vmid(&thread->trace_ids, addrspace->vmid);

out:
	return ret;
}

addrspace_t *
addrspace_get_self(void)
{
	return thread_get_self()->addrspace;
}

error_t
addrspace_handle_object_activate_thread(thread_t *thread)
{
	error_t ret = OK;

	assert(thread != NULL);

	if ((thread->kind == THREAD_KIND_VCPU) && (thread->addrspace == NULL)) {
		ret = ERROR_OBJECT_CONFIG;
	}

	return ret;
}

void
addrspace_handle_object_deactivate_thread(thread_t *thread)
{
	if ((thread->kind == THREAD_KIND_VCPU) && (thread->addrspace != NULL)) {
		addrspace_detach_thread(thread);
	}
}

uintptr_t
addrspace_handle_thread_get_stack_base(thread_t *thread)
{
	assert(thread != NULL);
	assert(thread->kind == THREAD_KIND_VCPU);
	assert(thread->addrspace != NULL);

	virt_range_t *range = &thread->addrspace->hyp_va_range;

	// Align the starting base to the next boundary to ensure we have guard
	// pages before the first stack mapping.
	uintptr_t base =
		util_balign_up(range->base + 1U, THREAD_STACK_MAP_ALIGN);

	base += (uintptr_t)thread->stack_map_index * THREAD_STACK_MAP_ALIGN;

	assert((base + THREAD_STACK_MAX_SIZE) <
	       (range->base + (range->size - 1U)));

	return base;
}
#endif

#if defined(MODULE_VM_ROOTVM)
void
addrspace_handle_rootvm_init(thread_t *root_thread, cspace_t *root_cspace,
			     qcbor_enc_ctxt_t *qcbor_enc_ctxt)
{
	addrspace_create_t as_params = { NULL };

	// Create addrspace for root thread
	addrspace_ptr_result_t addrspace_ret = partition_allocate_addrspace(
		root_thread->header.partition, as_params);
	if (addrspace_ret.e != OK) {
		panic("Error creating root addrspace");
	}
	addrspace_t *root_addrspace = addrspace_ret.r;

	assert(!vcpu_option_flags_get_hlos_vm(&root_thread->vcpu_options));

	spinlock_acquire(&root_addrspace->header.lock);
	// FIXME:
	// Root VM address space could be smaller
	if (addrspace_configure(root_addrspace, ROOT_VM_VMID) != OK) {
		spinlock_release(&root_addrspace->header.lock);
		panic("Error configuring addrspace");
	}
	spinlock_release(&root_addrspace->header.lock);

	// Create a master cap for the addrspace
	object_ptr_t	obj_ptr	  = { .addrspace = root_addrspace };
	cap_id_result_t capid_ret = cspace_create_master_cap(
		root_cspace, obj_ptr, OBJECT_TYPE_ADDRSPACE);
	if (capid_ret.e != OK) {
		panic("Error create addrspace cap id.");
	}

	assert(qcbor_enc_ctxt != NULL);

	QCBOREncode_AddUInt64ToMap(qcbor_enc_ctxt, "addrspace_capid",
				   capid_ret.r);

	if (object_activate_addrspace(root_addrspace) != OK) {
		panic("Error activating addrspace");
	}

	// Attach address space to thread
	error_t ret = addrspace_attach_thread(root_addrspace, root_thread);
	if (ret != OK) {
		panic("Error attaching root addrspace to root thread.");
	}
}
#endif

error_t
addrspace_handle_object_create_addrspace(addrspace_create_t params)
{
	error_t ret;

	addrspace_t *addrspace = params.addrspace;
	assert(addrspace != NULL);
	spinlock_init(&addrspace->mapping_list_lock);
	spinlock_init(&addrspace->pgtable_lock);
#if defined(INTERFACE_VCPU_RUN)
	spinlock_init(&addrspace->vmmio_range_lock);
	gpt_config_t gpt_config = gpt_config_default();
	gpt_config_set_max_bits(&gpt_config, GPT_MAX_SIZE_BITS);
	gpt_config_set_rcu_read(&gpt_config, true);
	ret = gpt_init(&addrspace->vmmio_ranges, addrspace->header.partition,
		       gpt_config, util_bit(GPT_TYPE_VMMIO_RANGE));
	if (ret != OK) {
		goto out;
	}
#endif

	addrspace->info_area.ipa = VMADDR_INVALID;
	addrspace->info_area.me	 = NULL;

	// Allocate some hypervisor address space for the addrspace object use,
	// including kernel stacks of attached threads and vm_info_page.

	// Stack region size, including start and end guard regions.
	size_t stack_area_size =
		THREAD_STACK_MAP_ALIGN * (ADDRSPACE_MAX_THREADS + 2U);
	size_t alloc_size =
		stack_area_size +
		util_balign_up(MAX_VM_INFO_AREA_SIZE, PGTABLE_HYP_PAGE_SIZE);

	virt_range_result_t alloc_range = hyp_aspace_allocate(alloc_size);
	if (alloc_range.e == OK) {
		addrspace->hyp_va_range = alloc_range.r;

		addrspace->info_area.hyp_va =
			(addrspace_info_area_layout_t *)(alloc_range.r.base +
							 stack_area_size);
	}

	ret = alloc_range.e;
#if defined(INTERFACE_VCPU_RUN)
out:
#endif
	return ret;
}

void
addrspace_handle_object_cleanup_addrspace(addrspace_t *addrspace)
{
	assert(addrspace != NULL);

#if defined(INTERFACE_VCPU_RUN)
	gpt_destroy(&addrspace->vmmio_ranges);
#endif

	hyp_aspace_deallocate(addrspace->header.partition,
			      addrspace->hyp_va_range);
}

void
addrspace_unwind_object_create_addrspace(addrspace_create_t params)
{
	addrspace_handle_object_cleanup_addrspace(params.addrspace);
}

error_t
addrspace_configure(addrspace_t *addrspace, vmid_t vmid)
{
	error_t ret = OK;

	assert(addrspace != NULL);

	if ((vmid == 0U) || (vmid >= NUM_VMIDS)) {
		ret = ERROR_ARGUMENT_INVALID;
	} else {
		addrspace->vmid = vmid;
	}

	return ret;
}

error_t
addrspace_configure_info_area(addrspace_t *addrspace, memextent_t *info_area_me,
			      vmaddr_t ipa)
{
	error_t ret = OK;

	assert(addrspace != NULL);

	size_t size = info_area_me->size;
	assert(size != 0);

#if (ADDRSPACE_INFO_AREA_LAYOUT_SIZE != 0)
	if ((size < (size_t)ADDRSPACE_INFO_AREA_LAYOUT_SIZE) ||
	    (size > (size_t)MAX_VM_INFO_AREA_SIZE))
#else
	if (size > (size_t)MAX_VM_INFO_AREA_SIZE)
#endif
	{
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (!util_is_baligned(ipa, PGTABLE_HYP_PAGE_SIZE) ||
	    util_add_overflows(ipa, size) ||
	    ((ipa + size) > util_bit(PLATFORM_VM_ADDRESS_SPACE_BITS))) {
		ret = ERROR_ADDR_INVALID;
		goto out;
	}

	if ((info_area_me->type != MEMEXTENT_TYPE_BASIC) ||
	    (!pgtable_access_check(info_area_me->access, PGTABLE_ACCESS_RW)) ||
	    (info_area_me->memtype != MEMEXTENT_MEMTYPE_ANY)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	addrspace->info_area.ipa = ipa;
	if (addrspace->info_area.me != NULL) {
		object_put_memextent(addrspace->info_area.me);
	}
	addrspace->info_area.me = object_get_memextent_additional(info_area_me);

out:
	return ret;
}

error_t
addrspace_handle_object_activate_addrspace(addrspace_t *addrspace)
{
	error_t ret = OK;

	assert(addrspace != NULL);

	// FIXME:
	bool already_set = bitmap_atomic_test_and_set(
		addrspace_vmids, addrspace->vmid, memory_order_relaxed);
	if (already_set) {
		ret = ERROR_BUSY;
		goto out_busy;
	}

	partition_t *partition = addrspace->header.partition;
	ret = pgtable_vm_init(partition, &addrspace->vm_pgtable,
			      addrspace->vmid);
	if (ret != OK) {
		goto out_vmid_dealloc;
	}

	if (addrspace->info_area.me != NULL) {
		vmaddr_t  ipa  = addrspace->info_area.ipa;
		uintptr_t va   = (uintptr_t)addrspace->info_area.hyp_va;
		size_t	  size = addrspace->info_area.me->size;

		// Ensure the IPA is within VM's range
		ret = addrspace_check_range(addrspace, ipa, size);
		if (ret != OK) {
			goto out_vmid_dealloc;
		}

		// Attach the extent to the addrspace object.
		ret = memextent_attach(partition, addrspace->info_area.me, va,
				       size);
		if (ret != OK) {
			object_put_memextent(addrspace->info_area.me);
			addrspace->info_area.me = NULL;
			goto out_vmid_dealloc;
		}

		assert(va != 0u);
		(void)memset_s((uint8_t *)va, size, 0, size);
	}

out_vmid_dealloc:
	if (ret != OK) {
		// Undo the vmid allocation
		(void)bitmap_atomic_test_and_clear(
			addrspace_vmids, addrspace->vmid, memory_order_relaxed);
		addrspace->vmid = 0U;
	}
out_busy:
	return ret;
}

void
addrspace_handle_object_deactivate_addrspace(addrspace_t *addrspace)
{
	assert(addrspace != NULL);

	if (addrspace->info_area.me != NULL) {
		memextent_detach(addrspace->header.partition,
				 addrspace->info_area.me);

		object_put_memextent(addrspace->info_area.me);
		addrspace->info_area.me = NULL;
	}

	if (!addrspace->read_only) {
		pgtable_vm_destroy(addrspace->header.partition,
				   &addrspace->vm_pgtable);
	}

	bool set = bitmap_atomic_test_and_clear(
		addrspace_vmids, addrspace->vm_pgtable.control.vmid,
		memory_order_relaxed);
	if (!set) {
		panic("VMID bitmap never set or already cleared.");
	}
	addrspace->vmid = 0U;
}

error_t
addrspace_map(addrspace_t *addrspace, vmaddr_t vbase, size_t size, paddr_t phys,
	      pgtable_vm_memtype_t memtype, pgtable_access_t kernel_access,
	      pgtable_access_t user_access)
{
	error_t err = trigger_addrspace_map_event(addrspace, vbase, size, phys,
						  memtype, kernel_access,
						  user_access);
	if (err != ERROR_UNIMPLEMENTED) {
		goto out;
	}

	if (addrspace->read_only) {
		err = ERROR_DENIED;
		goto out;
	}

	spinlock_acquire(&addrspace->pgtable_lock);
	pgtable_vm_start(&addrspace->vm_pgtable);

	// We do not set the try_map option; we expect the caller to know if it
	// is overwriting an existing mapping.
	err = pgtable_vm_map(addrspace->header.partition,
			     &addrspace->vm_pgtable, vbase, size, phys, memtype,
			     kernel_access, user_access, false, false);

	pgtable_vm_commit(&addrspace->vm_pgtable);
	spinlock_release(&addrspace->pgtable_lock);

out:
	return err;
}

error_t
addrspace_unmap(addrspace_t *addrspace, vmaddr_t vbase, size_t size,
		paddr_t phys)
{
	error_t err =
		trigger_addrspace_unmap_event(addrspace, vbase, size, phys);
	if (err != ERROR_UNIMPLEMENTED) {
		goto out;
	}

	if (addrspace->read_only) {
		err = ERROR_DENIED;
		goto out;
	}

	spinlock_acquire(&addrspace->pgtable_lock);
	pgtable_vm_start(&addrspace->vm_pgtable);

	// Unmap only if the physical address is matching.
	pgtable_vm_unmap_matching(addrspace->header.partition,
				  &addrspace->vm_pgtable, vbase, phys, size);
	err = OK;

	pgtable_vm_commit(&addrspace->vm_pgtable);
	spinlock_release(&addrspace->pgtable_lock);

out:
	return err;
}

addrspace_lookup_result_t
addrspace_lookup(addrspace_t *addrspace, vmaddr_t vbase, size_t size)
{
	addrspace_lookup_result_t ret;

	assert(addrspace != NULL);

	if (size == 0U) {
		ret = addrspace_lookup_result_error(ERROR_ARGUMENT_SIZE);
		goto out;
	}

	if (util_add_overflows(vbase, size - 1U)) {
		ret = addrspace_lookup_result_error(ERROR_ADDR_OVERFLOW);
		goto out;
	}

	if (!util_is_baligned(vbase, PGTABLE_VM_PAGE_SIZE) ||
	    !util_is_baligned(size, PGTABLE_VM_PAGE_SIZE)) {
		ret = addrspace_lookup_result_error(ERROR_ARGUMENT_ALIGNMENT);
		goto out;
	}

	bool		     first_lookup   = true;
	paddr_t		     lookup_phys    = 0U;
	size_t		     lookup_size    = 0U;
	pgtable_vm_memtype_t lookup_memtype = PGTABLE_VM_MEMTYPE_NORMAL_WB;
	pgtable_access_t     lookup_kernel_access = PGTABLE_ACCESS_NONE;
	pgtable_access_t     lookup_user_access	  = PGTABLE_ACCESS_NONE;

	spinlock_acquire(&addrspace->pgtable_lock);

	bool   mapped	   = true;
	size_t mapped_size = 0U;
	for (size_t offset = 0U; offset < size; offset += mapped_size) {
		vmaddr_t	     curr = vbase + offset;
		paddr_t		     mapped_phys;
		pgtable_vm_memtype_t mapped_memtype;
		pgtable_access_t     mapped_kernel_access, mapped_user_access;

		mapped = pgtable_vm_lookup(&addrspace->vm_pgtable, curr,
					   &mapped_phys, &mapped_size,
					   &mapped_memtype,
					   &mapped_kernel_access,
					   &mapped_user_access);
		if (mapped) {
			size_t mapping_offset = curr & (mapped_size - 1U);
			mapped_phys += mapping_offset;
			mapped_size = util_min(mapped_size - mapping_offset,
					       size - offset);

			if (first_lookup) {
				lookup_phys	     = mapped_phys;
				lookup_size	     = mapped_size;
				lookup_memtype	     = mapped_memtype;
				lookup_kernel_access = mapped_kernel_access;
				lookup_user_access   = mapped_user_access;
				first_lookup	     = false;
			} else if (((lookup_phys + lookup_size) ==
				    mapped_phys) &&
				   (lookup_memtype == mapped_memtype) &&
				   (lookup_kernel_access ==
				    mapped_kernel_access) &&
				   (lookup_user_access == mapped_user_access)) {
				lookup_size += mapped_size;
			} else {
				// Mapped range no longer contiguous, end the
				// lookup.
				break;
			}
		} else {
			break;
		}
	}

	spinlock_release(&addrspace->pgtable_lock);

	if (first_lookup) {
		ret = addrspace_lookup_result_error(ERROR_ADDR_INVALID);
		goto out;
	}

	addrspace_lookup_t lookup = {
		.phys	       = lookup_phys,
		.size	       = lookup_size,
		.memtype       = lookup_memtype,
		.kernel_access = lookup_kernel_access,
		.user_access   = lookup_user_access,
	};

	ret = addrspace_lookup_result_ok(lookup);

out:
	return ret;
}

error_t
addrspace_add_vmmio_range(addrspace_t *addrspace, vmaddr_t base, size_t size)
{
	error_t ret;

#if defined(INTERFACE_VCPU_RUN)
	if (size == 0U) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	if (util_add_overflows(base, size)) {
		ret = ERROR_ADDR_OVERFLOW;
		goto out;
	}

	spinlock_acquire(&addrspace->vmmio_range_lock);

	if (addrspace->vmmio_range_count == ADDRSPACE_MAX_VMMIO_RANGES) {
		ret = ERROR_NORESOURCES;
		goto out_locked;
	}

	gpt_entry_t entry = (gpt_entry_t){
		.type			= GPT_TYPE_VMMIO_RANGE,
		.value.vmmio_range_base = base,
	};

	ret = gpt_insert(&addrspace->vmmio_ranges, base, size, entry, true);

	if (ret == OK) {
		addrspace->vmmio_range_count++;
	}

out_locked:
	spinlock_release(&addrspace->vmmio_range_lock);
out:
#else // !INTERFACE_VCPU_RUN
	(void)addrspace;
	(void)base;
	(void)size;
	ret = ERROR_UNIMPLEMENTED;
#endif
	return ret;
}

error_t
addrspace_remove_vmmio_range(addrspace_t *addrspace, vmaddr_t base, size_t size)
{
	error_t ret;

#if defined(INTERFACE_VCPU_RUN)
	if (size == 0U) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	if (util_add_overflows(base, size)) {
		ret = ERROR_ADDR_OVERFLOW;
		goto out;
	}

	spinlock_acquire(&addrspace->vmmio_range_lock);

	gpt_entry_t entry = (gpt_entry_t){
		.type			= GPT_TYPE_VMMIO_RANGE,
		.value.vmmio_range_base = base,
	};

	ret = gpt_remove(&addrspace->vmmio_ranges, base, size, entry);

	if (ret == OK) {
		assert(addrspace->vmmio_range_count > 0U);
		addrspace->vmmio_range_count--;
	}

	spinlock_release(&addrspace->vmmio_range_lock);
out:
#else // !INTERFACE_VCPU_RUN
	(void)addrspace;
	(void)base;
	(void)size;
	ret = ERROR_UNIMPLEMENTED;
#endif
	return ret;
}
