// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// rootvm_init() is allowed to call partition_get_root().
#define ROOTVM_INIT 1

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <attributes.h>
#include <bitmap.h>
#include <cpulocal.h>
#include <cspace.h>
#include <memdb.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <partition_init.h>
#include <platform_mem.h>
#include <scheduler.h>
#include <spinlock.h>
#include <thread.h>
#include <util.h>
#include <vcpu.h>

#include <events/rootvm.h>

#include <asm/cache.h>
#include <asm/cpu.h>

#include "boot_init.h"
#include "event_handlers.h"

// FIXME: remove when we have a device tree where to read it from
// dummy value.
#define MAX_CAPS 1024

void NOINLINE
rootvm_init(void)
{
	static_assert(SCHEDULER_NUM_PRIORITIES >= (priority_t)3U,
		      "unexpected scheduler configuration");
	static_assert(SCHEDULER_MAX_PRIORITY - 2U > SCHEDULER_DEFAULT_PRIORITY,
		      "unexpected scheduler configuration");

	thread_create_t params = {
		.scheduler_affinity	  = cpulocal_get_index(),
		.scheduler_affinity_valid = true,
		.kind			  = THREAD_KIND_VCPU,
		.scheduler_priority	  = SCHEDULER_MAX_PRIORITY - 2U,
		.scheduler_priority_valid = true,
	};

	partition_t *root_partition = partition_get_root();

	assert(root_partition != NULL);

	platform_add_root_heap(root_partition);

	// Create cspace for root partition
	cspace_create_t cs_params = { NULL };

	cspace_ptr_result_t cspace_ret =
		partition_allocate_cspace(root_partition, cs_params);
	if (cspace_ret.e != OK) {
		goto cspace_fail;
	}
	cspace_t *root_cspace = cspace_ret.r;

	spinlock_acquire(&root_cspace->header.lock);
	if (cspace_configure(root_cspace, MAX_CAPS) != OK) {
		spinlock_release(&root_cspace->header.lock);
		goto cspace_fail;
	}
	spinlock_release(&root_cspace->header.lock);

	if (object_activate_cspace(root_cspace) != OK) {
		goto cspace_fail;
	}

	// Allocate and setup the root thread
	thread_ptr_result_t thd_ret =
		partition_allocate_thread(root_partition, params);
	if (thd_ret.e != OK) {
		panic("Error allocating root thread");
	}
	thread_t *root_thread = (thread_t *)thd_ret.r;

	vcpu_option_flags_t vcpu_options = vcpu_option_flags_default();

#if defined(ROOTVM_IS_HLOS) && ROOTVM_IS_HLOS
	vcpu_option_flags_set_hlos_vm(&vcpu_options, true);
#endif

	if (vcpu_configure(root_thread, vcpu_options) != OK) {
		panic("Error configuring vcpu");
	}

	// Attach root cspace to root thread
	if (cspace_attach_thread(root_cspace, root_thread) != OK) {
		panic("Error attaching cspace to root thread");
	}

	// Give the root cspace a cap to itself
	object_ptr_t obj_ptr;

	obj_ptr.cspace		  = root_cspace;
	cap_id_result_t capid_ret = cspace_create_master_cap(
		root_cspace, obj_ptr, OBJECT_TYPE_CSPACE);
	if (capid_ret.e != OK) {
		goto cspace_fail;
	}

	void_ptr_result_t alloc_ret;
	boot_env_data_t * env_data;
	size_t		  env_data_size = sizeof(*env_data);

	alloc_ret = partition_alloc(root_partition, env_data_size,
				    alignof(*env_data));
	if (alloc_ret.e != OK) {
		panic("Allocate env_data failed");
	}
	env_data = (boot_env_data_t *)alloc_ret.r;
	memset(env_data, 0, env_data_size);

	env_data->cspace_capid = capid_ret.r;

	// Take extra reference so that the deletion of the master cap does not
	// accidentally destroy the partition.
	root_partition = object_get_partition_additional(root_partition);

	// Create caps for the root partition and thread
	obj_ptr.partition = root_partition;
	capid_ret	  = cspace_create_master_cap(root_cspace, obj_ptr,
					     OBJECT_TYPE_PARTITION);
	if (capid_ret.e != OK) {
		panic("Error creating root partition cap");
	}
	env_data->partition_capid = capid_ret.r;

	obj_ptr.thread = root_thread;
	capid_ret      = cspace_create_master_cap(root_cspace, obj_ptr,
						  OBJECT_TYPE_THREAD);
	if (capid_ret.e != OK) {
		panic("Error creating root partition cap");
	}
	env_data->vcpu_capid = capid_ret.r;

	// Do a memdb walk to get all the available memory ranges of the root
	// partition and save in the boot_env_data
	if (memdb_walk((uintptr_t)root_partition, MEMDB_TYPE_PARTITION,
		       boot_add_free_range, (void *)env_data) != OK) {
		panic("Error doing the memory database walk");
	}

	// FIXME: add event for converting env_data structure to a DTB
	trigger_rootvm_init_event(root_partition, root_thread, root_cspace,
				  env_data);

#if !defined(ROOTVM_IS_HLOS) || !ROOTVM_IS_HLOS
	// Copy the boot_env_data to the root VM memory
	paddr_t rootvm_env_phys = env_data->env_ipa - env_data->me_ipa_base +
				  PLATFORM_ROOTVM_LMA_BASE;
	void *va = partition_phys_map(rootvm_env_phys,
				      util_balign_up(env_data_size,
						     PGTABLE_VM_PAGE_SIZE));
	partition_phys_access_enable(va);

	memcpy(va, (void *)env_data, env_data_size);
	CACHE_CLEAN_RANGE((boot_env_data_t *)va, env_data_size);

	partition_phys_access_disable(va);
	partition_phys_unmap(va, rootvm_env_phys,
			     util_balign_up(env_data_size,
					    PGTABLE_VM_PAGE_SIZE));
#endif

	// Setup the root VM thread
	if (object_activate_thread(root_thread) != OK) {
		panic("Error activating root thread");
	}

	scheduler_lock(root_thread);
#if defined(ROOTVM_IS_HLOS) && ROOTVM_IS_HLOS
	// FIXME: add a platform interface for configuring root thread
	vcpu_poweron(root_thread, env_data->entry_hlos, 0U);
#else
	// FIXME: eventually pass as dtb, for now the boot_env_data ipa is passed
	// directly.
	vcpu_poweron(root_thread, env_data->entry_ipa, env_data->env_ipa);
#endif
	scheduler_unlock(root_thread);
	partition_free(root_partition, env_data, env_data_size);
	env_data = NULL;

	return;

cspace_fail:
	panic("Error creating root cspace cap");
}
