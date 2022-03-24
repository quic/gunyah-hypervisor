// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypconstants.h>
#include <hypregisters.h>
#include <hyprights.h>

#include <addrspace.h>
#include <atomic.h>
#include <bitmap.h>
#include <cpulocal.h>
#include <cspace.h>
#include <hyp_aspace.h>
#include <list.h>
#include <object.h>
#include <panic.h>
#include <partition_alloc.h>
#include <pgtable.h>
#include <spinlock.h>

#include "event_handlers.h"

// FIXME: This file contains architecture specific details and should be
// refactored.

extern VTTBR_EL2_t hlos_vm_vttbr;

// FIXME: Limit VMIDs to reduce bitmap size
#if defined(ARCH_ARM_8_1_VMID16)
#define NUM_VMIDS 256
#else
#define NUM_VMIDS 256
#endif

static _Atomic BITMAP_DECLARE(NUM_VMIDS, addrspace_vmids);

void
addrspace_handle_boot_cold_init(void)
{
	// Reserve VMID 0
	bool already_set = bitmap_atomic_test_and_set(addrspace_vmids, 0U,
						      memory_order_relaxed);
	assert(!already_set);
}

void
addrspace_context_switch_load(void)
{
	thread_t *thread = thread_get_self();

	if (thread->kind == THREAD_KIND_VCPU) {
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

void
addrspace_handle_rootvm_init(thread_t *root_thread, cspace_t *root_cspace,
			     boot_env_data_t *env_data)
{
	addrspace_create_t as_params = { NULL };

	// Create addrspace for root thread
	addrspace_ptr_result_t addrspace_ret = partition_allocate_addrspace(
		root_thread->header.partition, as_params);
	if (addrspace_ret.e != OK) {
		panic("Error creating root addrspace");
	}
	addrspace_t *root_addrspace = addrspace_ret.r;

	vmid_t vmid;

#if defined(ROOTVM_IS_HLOS) && ROOTVM_IS_HLOS
	assert(vcpu_option_flags_get_hlos_vm(&root_thread->vcpu_options));

	vmid = (vmid_t)VTTBR_EL2_get_VMID(&hlos_vm_vttbr);
#else
	assert(!vcpu_option_flags_get_hlos_vm(&root_thread->vcpu_options));

	vmid = ROOT_VM_VMID;
#endif

	spinlock_acquire(&root_addrspace->header.lock);
	// Root VM address space could be smaller
	if (addrspace_configure(root_addrspace, vmid) != OK) {
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

	env_data->addrspace_capid = capid_ret.r;

	if (object_activate_addrspace(root_addrspace) != OK) {
		panic("Error activating addrspace");
	}

	// Attach address space to thread
	error_t ret = addrspace_attach_thread(root_addrspace, root_thread);
	if (ret != OK) {
		panic("Error attaching root addrspace to root thread.");
	}

#if defined(ROOTVM_IS_HLOS) && ROOTVM_IS_HLOS
	// Attach all of the secondary root VM threads to the address space
	for (cpu_index_t i = 0; cpulocal_index_valid(i); i++) {
		thread_t *thread;
		if (i == root_thread->scheduler_affinity) {
			continue;
		}
		cap_id_t      thread_cap = env_data->psci_secondary_vcpus[i];
		object_type_t type;
		object_ptr_result_t o = cspace_lookup_object_any(
			root_cspace, thread_cap,
			CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE, &type);
		if ((o.e != OK) || (type != OBJECT_TYPE_THREAD)) {
			panic("VIC couldn't attach root VM thread");
		}
		thread = o.r.thread;

		// Attach thread to root addrspace
		if (addrspace_attach_thread(root_thread->addrspace, thread) !=
		    OK) {
			panic("Error attaching addrspace to secondary VCPU");
		}

		object_put_thread(thread);
	}
#endif
}

error_t
addrspace_handle_object_create_addrspace(addrspace_create_t params)
{
	addrspace_t *addrspace = params.addrspace;
	assert(addrspace != NULL);
	spinlock_init(&addrspace->mapping_list_lock);
	spinlock_init(&addrspace->pgtable_lock);
	list_init(&addrspace->mapping_list);

	// Allocate some hypervisor address space for the kernel stacks of
	// attached threads.
	size_t aspace_size =
		THREAD_STACK_MAP_ALIGN * (ADDRSPACE_MAX_THREADS + 1U);
	virt_range_result_t stack_range = hyp_aspace_allocate(aspace_size);
	if (stack_range.e == OK) {
		addrspace->stack_range = stack_range.r;
	}

	return stack_range.e;
}

void
addrspace_handle_object_cleanup_addrspace(addrspace_t *addrspace)
{
	assert(addrspace != NULL);

	hyp_aspace_deallocate(addrspace->header.partition,
			      addrspace->stack_range);
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

	if ((vmid == 0U) || vmid >= NUM_VMIDS) {
		ret = ERROR_ARGUMENT_INVALID;
	} else {
		addrspace->vmid = vmid;
	}

	return ret;
}

error_t
addrspace_handle_object_activate_addrspace(addrspace_t *addrspace)
{
	error_t ret = OK;

	assert(addrspace != NULL);

	bool already_set = bitmap_atomic_test_and_set(
		addrspace_vmids, addrspace->vmid, memory_order_relaxed);
	if (already_set) {
		ret = ERROR_BUSY;
		goto out;
	}

	partition_t *partition = addrspace->header.partition;
	ret = pgtable_vm_init(partition, &addrspace->vm_pgtable,
			      addrspace->vmid);
	if (ret != OK) {
		// Undo the vmid allocation
		(void)bitmap_atomic_test_and_clear(
			addrspace_vmids, addrspace->vmid, memory_order_relaxed);
		addrspace->vmid = 0U;
	}

out:
	return ret;
}

void
addrspace_handle_object_deactivate_addrspace(addrspace_t *addrspace)
{
	assert(addrspace != NULL);

	if (!addrspace->vm_read_only) {
		pgtable_vm_destroy(addrspace->header.partition,
				   &addrspace->vm_pgtable);
	}

	bool already_cleared = bitmap_atomic_test_and_clear(
		addrspace_vmids, addrspace->vm_pgtable.control.vmid,
		memory_order_relaxed);
	if (already_cleared) {
		panic("VMID bitmap never set or already cleared.");
	}
	addrspace->vmid = 0U;
}

uintptr_t
addrspace_handle_thread_get_stack_base(thread_t *thread)
{
	assert(thread != NULL);
	assert(thread->kind == THREAD_KIND_VCPU);
	assert(thread->addrspace != NULL);

	virt_range_t *range = &thread->addrspace->stack_range;

	// Align the starting base to the next boundary to ensure we have guard
	// pages before the first stack mapping.
	uintptr_t base =
		util_balign_up(range->base + 1U, THREAD_STACK_MAP_ALIGN);

	base += thread->stack_map_index * THREAD_STACK_MAP_ALIGN;

	assert((base + THREAD_STACK_MAX_SIZE) <
	       (range->base + (range->size - 1U)));

	return base;
}
