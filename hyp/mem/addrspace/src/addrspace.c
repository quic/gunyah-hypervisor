// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypconstants.h>
#include <hypcontainers.h>
#include <hypregisters.h>
#include <hyprights.h>

#include <addrspace.h>
#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <cpulocal.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <hyp_aspace.h>
#include <list.h>
#include <memclear.h>
#include <memextent.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <pgtable.h>
#include <qcbor.h>
#include <range_tree.h>
#include <rcu.h>
#include <spinlock.h>
#include <thread.h>
#include <util.h>
#if defined(INTERFACE_VCPU)
#include <vcpu.h>
#endif

#include <events/addrspace.h>

#include <asm/barrier.h>
#include <asm/cache.h>
#include <asm/cpu.h>

#include "event_handlers.h"
#include "useraccess.h"

// FIXME: This file contains architecture specific details and should be
// refactored.

// Defining fault-handling ranges only makes sense if proxy scheduling is
// available. Otherwise, there is no way to report the faults.
#if defined(INTERFACE_VCPU_RUN)
#define ADDRSPACE_FAULT_RANGES 1
#else
#define ADDRSPACE_FAULT_RANGES 0
#endif

static _Atomic BITMAP_DECLARE(ADDRSPACE_NUM_VMIDS, addrspace_vmids);

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
addrspace_handle_vcpu_load_state(void)
{
	thread_t *thread = thread_get_self();

	pgtable_vm_load_regs(&thread->addrspace->vm_pgtable);
}

static void
addrspace_detach_thread(thread_t *thread)
{
	assert(thread != NULL);
	assert(vcpu_is_vcpu(thread));
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

	if (!vcpu_is_vcpu(thread)) {
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

	if ((vcpu_is_vcpu(thread)) && (thread->addrspace == NULL)) {
		ret = ERROR_OBJECT_CONFIG;
	}

	return ret;
}

void
addrspace_handle_object_deactivate_thread(thread_t *thread)
{
	if ((vcpu_is_vcpu(thread)) && (thread->addrspace != NULL)) {
		addrspace_detach_thread(thread);
	}
}

uintptr_t
addrspace_handle_thread_get_stack_base(thread_t *thread)
{
	assert(thread != NULL);
	assert(vcpu_is_vcpu(thread));
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
	// FIXME: QC Gunyah issue #60
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
addrspace_handle_object_create_addrspace(addrspace_create_t addrspace_create)
{
	error_t ret;

	addrspace_t *addrspace = addrspace_create.addrspace;
	assert(addrspace != NULL);
	spinlock_init(&addrspace->mapping_list_lock);
#if ADDRSPACE_FAULT_RANGES
	ret = range_tree_init(&addrspace->fault_ranges,
			      addrspace->header.partition);
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
			(uint8_t *)(alloc_range.r.base + stack_area_size);
	}

	ret = alloc_range.e;
#if ADDRSPACE_FAULT_RANGES
out:
#endif
	return ret;
}

void
addrspace_handle_object_cleanup_addrspace(addrspace_t *addrspace)
{
	assert(addrspace != NULL);

#if ADDRSPACE_FAULT_RANGES
	range_tree_lock(&addrspace->fault_ranges);
	range_tree_destroy(&addrspace->fault_ranges,
			   RANGE_TREE_NODE_TYPE_ADDRSPACE_RANGE);
	range_tree_unlock(&addrspace->fault_ranges);
#endif

	if (addrspace->hyp_va_range.size != 0U) {
		hyp_aspace_unmap_and_deallocate(addrspace->header.partition,
						addrspace->hyp_va_range);
		addrspace->info_area.hyp_va = NULL;
	}

	if (addrspace->info_area.me != NULL) {
		object_put_memextent(addrspace->info_area.me);
		addrspace->info_area.me = NULL;
	}
}

void
addrspace_unwind_object_create_addrspace(addrspace_create_t addrspace_create)
{
	addrspace_handle_object_cleanup_addrspace(addrspace_create.addrspace);
}

error_t
addrspace_configure(addrspace_t *addrspace, vmid_t vmid)
{
	error_t ret = OK;

	assert(addrspace != NULL);

	if ((vmid == 0U) || (vmid >= ADDRSPACE_NUM_VMIDS)) {
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
	error_t ret;

	assert(addrspace != NULL);

	if (info_area_me->type != MEMEXTENT_TYPE_BASIC) {
		ret = ERROR_MEMEXTENT_TYPE;
		goto out;
	}

	size_t size = info_area_me->size;

	if ((size < PGTABLE_VM_PAGE_SIZE) || (size > MAX_VM_INFO_AREA_SIZE)) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	if (!util_is_baligned(ipa, PGTABLE_VM_PAGE_SIZE)) {
		ret = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}

	if (util_add_overflows(ipa, size - 1U) ||
	    ((ipa + size - 1U) >= util_bit(PLATFORM_VM_ADDRESS_SPACE_BITS))) {
		ret = ERROR_ADDR_OVERFLOW;
		goto out;
	}

	if (!pgtable_access_check(info_area_me->access, PGTABLE_ACCESS_RW) ||
	    (info_area_me->memtype != MEMEXTENT_MEMTYPE_ANY)) {
		ret = ERROR_DENIED;
		goto out;
	}

	addrspace->info_area.ipa = ipa;
	if (addrspace->info_area.me != NULL) {
		object_put_memextent(addrspace->info_area.me);
	}
	addrspace->info_area.me = object_get_memextent_additional(info_area_me);

	ret = OK;

out:
	return ret;
}

addrspace_alloc_info_area_result_t
addrspace_alloc_info_area(addrspace_t *addrspace, size_t size, size_t alignment,
			  bool alloc_desc)
{
	addrspace_alloc_info_area_result_t ret;

	static_assert(util_sizeof_member(addrspace_info_area_entry_t, size) ==
			      4U,
		      "size should be 32 bits");
	static_assert(util_sizeof_member(addrspace_info_area_entry_t, offset) ==
			      4U,
		      "offset should be 32 bits");
	static_assert(MAX_VM_INFO_AREA_SIZE < ((size_t)UINT32_MAX + 1U),
		      "MAX_VM_INFO_AREA_SIZE invalid");

	if ((size == 0U) || (size > UINT32_MAX)) {
		ret = (addrspace_alloc_info_area_result_t){
			.e = ERROR_ARGUMENT_SIZE
		};
		goto out;
	}

	if (!util_is_p2(alignment) || (alignment > PGTABLE_VM_PAGE_SIZE)) {
		ret = (addrspace_alloc_info_area_result_t){
			.e = ERROR_ARGUMENT_ALIGNMENT
		};
		goto out;
	}

	spinlock_acquire(&addrspace->info_area.alloc_lock);

	if (addrspace->info_area.next_header == NULL) {
		ret = (addrspace_alloc_info_area_result_t){ .e = ERROR_IDLE };
		goto out_unlock;
	}

	// Ensure that offset - size will not underflow when calculating the
	// offset for the new allocation
	if (size > addrspace->info_area.data_offset) {
		ret = (addrspace_alloc_info_area_result_t){ .e = ERROR_NOMEM };
		goto out_unlock;
	}

	// Calculate the offset for the new data range
	size_t data_offset = util_balign_down(
		addrspace->info_area.data_offset - size, alignment);
	uintptr_t ptr = (uintptr_t)addrspace->info_area.hyp_va + data_offset;

	// The data and headers must not overlap the next header (after
	// one is appended, if applicable), because it must have a type
	// of NONE to terminate an iteration by the VM.
	uintptr_t headers_end =
		(uintptr_t)&addrspace->info_area.next_header[alloc_desc ? 2 : 1];
	if (ptr < headers_end) {
		ret = (addrspace_alloc_info_area_result_t){ .e = ERROR_NOMEM };
		goto out_unlock;
	}

	addrspace_info_area_entry_t *desc = NULL;

	if (alloc_desc) {
		desc = addrspace->info_area.next_header;

		// Construct the new entry descriptor
		*desc = (addrspace_info_area_entry_t){
			.size = (uint32_t)size, .offset = (uint32_t)data_offset
		};

		// Update the info area tracking
		addrspace->info_area.next_header++;
	}

	addrspace->info_area.data_offset = data_offset;

	ret = (addrspace_alloc_info_area_result_t){
		.e	    = OK,
		.ptr	    = ptr,
		.ipa	    = addrspace->info_area.ipa + data_offset,
		.descriptor = desc,
	};

out_unlock:
	spinlock_release(&addrspace->info_area.alloc_lock);
out:
	return ret;
}

static_assert((uint32_t)ADDRSPACE_INFO_AREA_ID_OWNER_INVALID ==
		      ADDRSPACE_INFO_AREA_ID_OWNER_INDEX,
	      "invalid enum");

addrspace_info_area_entry_result_t
addrspace_info_area_get_by_index(addrspace_t *addrspace, index_t index)
{
	addrspace_info_area_entry_result_t ret;

	uintptr_t va = (uintptr_t)addrspace->info_area.hyp_va;

	if (va == (uintptr_t)NULL) {
		ret = addrspace_info_area_entry_result_error(ERROR_IDLE);
		goto out;
	}
	addrspace_info_area_entry_t *items = (addrspace_info_area_entry_t *)va;

	ret = addrspace_info_area_entry_result_error(ERROR_ADDR_NOTFOUND);

	for (index_t i = 0U; i <= index; i++) {
		assert(((uintptr_t)&items[i + 1U] - va) <=
		       addrspace->info_area.data_offset);

		if (addrspace_info_area_entry_type_get_owner(&items[i].type) ==
		    ADDRSPACE_INFO_AREA_ID_OWNER_INVALID) {
			break;
		}
		if (i == index) {
			// Found index
			ret = addrspace_info_area_entry_result_ok(items[i]);
		}
	}

out:
	return ret;
}

addrspace_info_area_entry_result_t
addrspace_info_area_get_by_type(addrspace_t			*addrspace,
				addrspace_info_area_entry_type_t type)
{
	addrspace_info_area_entry_result_t ret;

	uintptr_t va = (uintptr_t)addrspace->info_area.hyp_va;

	if (va == (uintptr_t)NULL) {
		ret = addrspace_info_area_entry_result_error(ERROR_IDLE);
		goto out;
	}
	addrspace_info_area_entry_t *items = (addrspace_info_area_entry_t *)va;

	index_t i = 0U;

	ret = addrspace_info_area_entry_result_error(ERROR_ADDR_NOTFOUND);

	while (addrspace_info_area_entry_type_get_owner(&items[i].type) !=
	       ADDRSPACE_INFO_AREA_ID_OWNER_INVALID) {
		assert(((uintptr_t)&items[i + 1U] - va) <=
		       addrspace->info_area.data_offset);
		if (addrspace_info_area_entry_type_is_equal(items[i].type,
							    type)) {
			// Found the entry
			ret = addrspace_info_area_entry_result_ok(items[i]);
			break;
		}
		i += 1U;
	}

out:
	return ret;
}

uintptr_t
addrspace_info_area_get_data_addr(addrspace_t *addrspace, size_t offset)
{
	uintptr_t va = (uintptr_t)addrspace->info_area.hyp_va;

	assert(va != (uintptr_t)NULL);
	assert(addrspace->info_area.me != NULL);
	assert(offset < addrspace->info_area.me->size);

	return va + offset;
}

void
addrspace_enable_info_area(addrspace_info_area_entry_t	   *descriptor,
			   addrspace_info_area_entry_type_t type)
{
	assert(descriptor != NULL);

	descriptor->type = type;
	atomic_device_fence(memory_order_seq_cst);
	addrspace_info_area_entry_flags_set_valid(&descriptor->flags, true);

	CACHE_CLEAN_OBJECT(*descriptor);
}

error_t
addrspace_handle_object_activate_addrspace(addrspace_t *addrspace)
{
	error_t ret, err;

	assert(addrspace != NULL);

	// FIXME: QC Gunyah issue #61
	bool already_set = bitmap_atomic_test_and_set(
		addrspace_vmids, addrspace->vmid, memory_order_relaxed);
	if (already_set) {
		ret = ERROR_BUSY;
		goto out_busy;
	}

	partition_t *partition = addrspace->header.partition;
	err = pgtable_vm_init(partition, &addrspace->vm_pgtable,
			      addrspace->vmid);
	if (err != OK) {
		ret = err;
		goto out_vmid_dealloc;
	}

	if (addrspace->info_area.me != NULL) {
		vmaddr_t  ipa  = addrspace->info_area.ipa;
		uintptr_t va   = (uintptr_t)addrspace->info_area.hyp_va;
		size_t	  size = addrspace->info_area.me->size;

		// Ensure the memextent still has a valid size
		if ((size < PGTABLE_VM_PAGE_SIZE) ||
		    (size > MAX_VM_INFO_AREA_SIZE)) {
			ret = ERROR_ARGUMENT_SIZE;
			goto out_release_me;
		}

		// Ensure the IPA is within VM's addressing range
		err = addrspace_check_range(addrspace, ipa, size);
		if (err != OK) {
			ret = err;
			goto out_release_me;
		}

		// Attach the extent to the addrspace object.
		err = memextent_attach(partition, addrspace->info_area.me, va,
				       size);
		if (err != OK) {
			ret = err;
			goto out_release_me;
		}

		assert_debug(va != 0U);
		memclear((uint8_t *)va, size);

		addrspace->info_area.next_header =
			(addrspace_info_area_entry_t *)va;
		addrspace->info_area.data_offset = size;
	}

	ret = OK;
out_release_me:
	if ((ret != OK) && (addrspace->info_area.me != NULL)) {
		object_put_memextent(addrspace->info_area.me);
		addrspace->info_area.me = NULL;
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
addrspace_destroy_pgtables(addrspace_t *addrspace)
{
	if (!pgtable_vm_is_platform_managed(&addrspace->vm_pgtable)) {
		pgtable_vm_destroy(addrspace->header.partition,
				   &addrspace->vm_pgtable);
	}
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

	bool set = bitmap_atomic_test_and_clear(
		addrspace_vmids, addrspace->vm_pgtable.control.vmid,
		memory_order_relaxed);
	if (!set) {
		panic("VMID bitmap never set or already cleared.");
	}
	addrspace->vmid = 0U;
}

void
addrspace_start(addrspace_t *addrspace, addrspace_map_flags_t map_flags)
{
	(void)map_flags;
	pgtable_vm_start(&addrspace->vm_pgtable);
}

error_t
addrspace_map_locked(addrspace_t *addrspace, vmaddr_t vbase, size_t size,
		     paddr_t phys, pgtable_vm_memtype_t memtype,
		     pgtable_access_t	   kernel_access,
		     pgtable_access_t	   user_access,
		     addrspace_map_flags_t map_flags)
{
	error_t err;

	if (addrspace_map_flags_get_private(&map_flags)) {
#if ADDRSPACE_FAULT_RANGES
		// Protected mappings can be used by the guest kernel to zero
		// the memory by calling addrspace_modify_pages(), and therefore
		// must always be kernel-writable. We can't just check the
		// permissions when the page is zeroed, because the protected
		// memory must be sanitised unconditionally on VM exit.
		if (!pgtable_access_check(kernel_access, PGTABLE_ACCESS_W)) {
			err = ERROR_ARGUMENT_INVALID;
			goto out;
		}

		rcu_read_start();
		range_tree_lookup_result_t result = range_tree_lookup(
			&addrspace->fault_ranges, vbase, size);

		if ((result.size < size) || (result.node == NULL) ||
		    addrspace_range_container_of_range(result.node)->is_vmmio) {
			// Not a private address range; a private operation
			// should not be allowed to map here.
			rcu_read_finish();
			err = ERROR_DENIED;
			goto out;
		}
		rcu_read_finish();

		// For protected guest mappings, we must clean the cache before
		// mapping, to prevent write-backs occurring after the memory
		// becomes accessible to the guest VM.
		void *va = partition_phys_map(phys, size);
		partition_phys_access_enable(va);
		cache_clean_invalidate_range(va, size);
		partition_phys_access_disable(va);
		partition_phys_unmap(va, phys, size);

		// Protected guest mappings that are marked executable must
		// be coherent in the instruction cache.
		cache_invalidate_inst_all();
#else
		// Without fault range registration, there's no way to create
		// any mappings.
		err = ERROR_UNIMPLEMENTED;
		goto out;
#endif
	} else if (addrspace_map_flags_get_vmmio(&map_flags)) {
#if ADDRSPACE_FAULT_RANGES
		// VMMIO mappings can be arbitrarily modified by the host,
		// and therefore should never be allowed to execute code.
		if (pgtable_access_check(kernel_access, PGTABLE_ACCESS_X) ||
		    pgtable_access_check(user_access, PGTABLE_ACCESS_X)) {
			err = ERROR_ARGUMENT_INVALID;
			goto out;
		}

		rcu_read_start();
		range_tree_lookup_result_t result = range_tree_lookup(
			&addrspace->fault_ranges, vbase, size);

		if ((result.size < size) || (result.node == NULL) ||
		    !addrspace_range_container_of_range(result.node)->is_vmmio) {
			// Not a VMMIO address range; a VMMIO operation
			// should not be allowed to map here.
			rcu_read_finish();
			err = ERROR_DENIED;
			goto out;
		}
		rcu_read_finish();
#else
		// Without fault range registration, there's no way to create
		// any mappings.
		err = ERROR_UNIMPLEMENTED;
		goto out;
#endif
	} else {
		// Nothing special to do at this point.
	}

	err = trigger_addrspace_map_event(addrspace, vbase, size, phys, memtype,
					  kernel_access, user_access);
	if ((err != ERROR_UNIMPLEMENTED) ||
	    pgtable_vm_is_platform_managed(&addrspace->vm_pgtable)) {
		goto out;
	}

	// We do not set the try_map option; we expect the caller to know if it
	// is overwriting an existing mapping.
	err = pgtable_vm_map(addrspace->header.partition,
			     &addrspace->vm_pgtable, vbase, size, phys, memtype,
			     kernel_access, user_access, false, false,
			     addrspace_map_flags_get_private(&map_flags));

out:
	return err;
}

error_t
addrspace_unmap_locked(addrspace_t *addrspace, vmaddr_t vbase, size_t size,
		       paddr_t phys, addrspace_map_flags_t map_flags)
{
	error_t err;

	if (addrspace_map_flags_get_vmmio(&map_flags)) {
#if ADDRSPACE_FAULT_RANGES
		rcu_read_start();
		range_tree_lookup_result_t result = range_tree_lookup(
			&addrspace->fault_ranges, vbase, size);

		if ((result.size < size) || (result.node == NULL) ||
		    !addrspace_range_container_of_range(result.node)->is_vmmio) {
			// Not a VMMIO address range; a VMMIO operation should
			// not be allowed to unmap here.
			rcu_read_finish();
			err = ERROR_DENIED;
			goto out;
		}
		rcu_read_finish();
#else
		// Without fault range registration, there's no way to determine
		// whether a VMMIO mapping can be removed.
		err = ERROR_UNIMPLEMENTED;
		goto out;
#endif
	}

	err = trigger_addrspace_unmap_event(
		addrspace, vbase, size, phys,
		addrspace_map_flags_get_no_sync(&map_flags));
	if ((err != ERROR_UNIMPLEMENTED) ||
	    pgtable_vm_is_platform_managed(&addrspace->vm_pgtable)) {
		goto out;
	}

	// Unmap only if the physical address is matching.
	err = pgtable_vm_unmap_matching(
		addrspace->header.partition, &addrspace->vm_pgtable, vbase,
		phys, size, addrspace_map_flags_get_private(&map_flags));

out:
	return err;
}

void
addrspace_commit(addrspace_t *addrspace, addrspace_map_flags_t map_flags)
{
	pgtable_vm_commit(&addrspace->vm_pgtable,
			  addrspace_map_flags_get_no_sync(&map_flags));
}

error_t
addrspace_unmap_sync(addrspace_t *addrspace)
{
	error_t err;

	err = trigger_addrspace_sync_event(addrspace);
	if (err != ERROR_UNIMPLEMENTED) {
		goto out;
	}

	// All we need to do is start a page table operation and then commit it
	// with the no_sync_unmap flag set to false. This will synchronise any
	// previous unmaps in the address space.
	pgtable_vm_start(&addrspace->vm_pgtable);
	pgtable_vm_commit(&addrspace->vm_pgtable, false);
	err = OK;

out:
	return err;
}

size_result_t
addrspace_modify_pages(addrspace_t *addrspace, vmaddr_t vbase, size_t size,
		       addrspace_modify_pages_flags_t flags)
{
	size_t	      size_remaining = size;
	error_t	      err;
	size_result_t ret;

	if ((size > 0U) && util_add_overflows(vbase, size - 1U)) {
		err = ERROR_ADDR_OVERFLOW;
		goto out;
	}

	if (pgtable_vm_is_platform_managed(&addrspace->vm_pgtable)) {
		err = ERROR_UNIMPLEMENTED;
		goto out;
	}

	pgtable_vm_start(&addrspace->vm_pgtable);

	size_result_t modify_ret = pgtable_vm_modify_protected(
		addrspace->header.partition, &addrspace->vm_pgtable, vbase,
		size, addrspace_modify_pages_flags_get_unlock(&flags),
		!addrspace_modify_pages_flags_get_do_not_sanitise(&flags),
		!addrspace_modify_pages_flags_get_no_sync_unlock(&flags));

	pgtable_vm_commit(&addrspace->vm_pgtable, false);

	assert(size >= modify_ret.r);

	size_remaining -= modify_ret.r;
	err = modify_ret.e;
out:
	if (err == OK) {
		assert(size_remaining == 0U);
		ret = size_result_ok(0U);
	} else {
		ret = (size_result_t){ .e = err, .r = size_remaining };
	}
	return ret;
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

	bool   mapped	   = true;
	size_t mapped_size = 0U;
	size_t offset	   = 0U;
	while (offset < size) {
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

		offset += mapped_size;
	}

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
addrspace_add_fault_range(addrspace_t *addrspace, vmaddr_t base, size_t size,
			  bool vmmio)
{
	error_t ret;

#if ADDRSPACE_FAULT_RANGES
	if (size == 0U) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	if (util_add_overflows(base, size - 1U)) {
		ret = ERROR_ADDR_OVERFLOW;
		goto out;
	}

	range_tree_lock(&addrspace->fault_ranges);

	addrspace_range_t *fault_range = NULL;

	if (addrspace->fault_range_count == ADDRSPACE_MAX_FAULT_RANGES) {
		ret = ERROR_NORESOURCES;
		goto out_locked;
	}

	void_ptr_result_t alloc_r = partition_alloc(addrspace->header.partition,
						    sizeof(*fault_range),
						    alignof(*fault_range));
	if (alloc_r.e != OK) {
		ret = alloc_r.e;
		goto out_locked;
	}
	fault_range  = (addrspace_range_t *)alloc_r.r;
	*fault_range = (addrspace_range_t){ .is_vmmio = vmmio };

	ret = range_tree_insert(&addrspace->fault_ranges, &fault_range->range,
				base, size);

	if (ret == OK) {
		// Note: it's safe to set this after insertion because it is
		// only accessed in the RCU update handler, and that can only be
		// enqueued for this range after we release the range tree lock.
		fault_range->partition = object_get_partition_additional(
			addrspace->header.partition);

		addrspace->fault_range_count++;
	} else {
		// Insertion failed because of a conflicting range. Determine
		// whether the existing range completely covers the requested
		// range and is of the same type; if so, we return an alternate
		// error code so the caller knows its accesses will still work.
		rcu_read_start();
		range_tree_lookup_result_t range_r = range_tree_lookup_locked(
			&addrspace->fault_ranges, base, size);
		if ((range_r.node != NULL) && (range_r.size == size) &&
		    (addrspace_range_container_of_range(range_r.node)
			     ->is_vmmio == vmmio)) {
			ret = ERROR_BUSY;
		}
		rcu_read_finish();
	}

out_locked:
	if ((ret != OK) && (fault_range != NULL)) {
		(void)partition_free(addrspace->header.partition, fault_range,
				     sizeof(*fault_range));
	}
	range_tree_unlock(&addrspace->fault_ranges);
out:
#else // !ADDRSPACE_FAULT_RANGES
	(void)addrspace;
	(void)base;
	(void)size;
	(void)vmmio;
	ret = ERROR_UNIMPLEMENTED;
#endif
	return ret;
}

error_t
addrspace_remove_fault_range(addrspace_t *addrspace, vmaddr_t base, size_t size,
			     bool vmmio)
{
	error_t ret;

#if ADDRSPACE_FAULT_RANGES
	if (size == 0U) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	if (util_add_overflows(base, size - 1U)) {
		ret = ERROR_ADDR_OVERFLOW;
		goto out;
	}

	range_tree_lock(&addrspace->fault_ranges);
	rcu_read_start();

	range_tree_lookup_result_t range_r = range_tree_lookup_locked(
		&addrspace->fault_ranges, base, SIZE_MAX);

	if ((range_r.node != NULL) && (range_r.size == size) &&
	    (addrspace_range_container_of_range(range_r.node)->is_vmmio ==
	     vmmio)) {
		addrspace_range_t *range =
			addrspace_range_container_of_range(range_r.node);
		ret = range_tree_remove(&addrspace->fault_ranges,
					&range->range);
		if (ret == OK) {
			assert(addrspace->fault_range_count > 0U);
			rcu_enqueue(&range->rcu_entry,
				    RCU_UPDATE_CLASS_ADDRSPACE_RELEASE_RANGE);
			addrspace->fault_range_count--;
		}
	} else if ((range_r.node == NULL) && (range_r.size >= size)) {
		// The entire specified range is already empty
		ret = ERROR_IDLE;
	} else {
		// The specified range is not empty, but the request doesn't
		// match a registered range
		ret = ERROR_ARGUMENT_INVALID;
	}

	rcu_read_finish();
	range_tree_unlock(&addrspace->fault_ranges);
out:
#else // !ADDRSPACE_FAULT_RANGES
	(void)addrspace;
	(void)base;
	(void)size;
	(void)vmmio;
	ret = ERROR_UNIMPLEMENTED;
#endif
	return ret;
}

#if ADDRSPACE_FAULT_RANGES
static void
addrspace_range_free(addrspace_range_t *range)
{
	assert(range != NULL);
	partition_t *partition = range->partition;
	assert(partition != NULL);
	(void)partition_free(partition, range, sizeof(*range));
	object_put_partition(partition);
}

bool
addrspace_handle_range_tree_release_node(range_tree_node_t *node)
{
	addrspace_range_free(addrspace_range_container_of_range(node));
	return true;
}

rcu_update_status_t
addrspace_handle_rcu_update(rcu_entry_t *entry)
{
	addrspace_range_free(addrspace_range_container_of_rcu_entry(entry));
	return rcu_update_status_default();
}
#endif

error_t
addrspace_trigger_msi(addrspace_t *addrspace, vmaddr_t mailbox,
		      uint32_t message, index_t device_id)
{
	error_t ret;

	// First, attempt the MSI trigger fast path. This should only succeed
	// if the write is to an MSI mailbox register.
	if (trigger_addrspace_trigger_msi_event(addrspace, mailbox, message,
						device_id) == OK) {
		ret = OK;
		goto out;
	}

	// At this point we could try a regular vdevice access, but we don't
	// have a use case that requires it and the vdevice module does not
	// expose the necessary API.
	//
	// Fall back to useraccess, to attempt to write the MSI to normal
	// memory. Linux uses this in its SMMUv3 driver.
	ret = useraccess_copy_to_guest_ipa(addrspace, mailbox, sizeof(uint32_t),
					   &message, sizeof(message), false,
					   false)
		      .e;

out:
	return ret;
}
