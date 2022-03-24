// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#if !defined(UNIT_TESTS)
#include <cspace.h>
#include <memdb.h>
#include <memextent.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <platform_mem.h>
#include <spinlock.h>
#include <trace.h>

#include "event_handlers.h"

static platform_ram_info_t ram_info;

error_t
platform_ram_probe(void)
{
	// FIXME: The RAM memory size is currently hardcoded to 1GB. We need to
	// find a better solution for this, possibly by using a
	// system-device-tree approach. We need to make sure that hyp RAM memory
	// ranges do not overlap with the ranges specified in the QEMU start
	// command.
	ram_info.num_ranges	   = 0x1;
	ram_info.ram_range[0].base = 0x40000000;
	ram_info.ram_range[0].size = 0x80000000; // 1Gb of RAM

	return OK;
}

platform_ram_info_t *
platform_get_ram_info(void)
{
	assert(ram_info.num_ranges != 0U);
	return &ram_info;
}

void
platform_add_root_heap(partition_t *partition)
{
	// We allocate 36MiB of memory from the Hyp labelled memory in the ram
	// partition table freelist.
	//  - We give 36MiB to the root partition heap and then allocate 32 MiB
	//  from the allocator to the trace buffer
	size_t trace_size      = TRACE_AREA_SIZE;
	size_t heap_extra_size = EXTRA_ROOT_HEAP_SIZE;
	size_t priv_size       = EXTRA_PRIVATE_HEAP_SIZE;

	uint64_t alloc_size = trace_size + heap_extra_size + priv_size;

	// FIXME: Currently using the end memory of the hardcoded 1Gb hyp RAM
	// memory size. We need to find a better solution for this, possibly by
	// dynamically reading the RAM memory end address from a device tree.
	paddr_t base = PLATFORM_LMA_BASE + 0x40000000 - alloc_size;

	// Add 1MiB to the hypervisor private partition
	error_t err = partition_mem_donate(partition, base, priv_size,
					   partition_get_private());
	if (err != OK) {
		panic("Error donating memory");
	}

	err = partition_map_and_add_heap(partition_get_private(), base,
					 priv_size);
	if (err != OK) {
		panic("Error adding root partition heap memory");
	}

	base += priv_size;
	alloc_size -= priv_size;

	// Add the rest to the partition's heap.
	err = partition_map_and_add_heap(partition, base, alloc_size);
	if (err != OK) {
		panic("Error adding root partition heap memory");
	}

	// Allocate memory for the trace_buffer
	trace_init(partition, trace_size);
}

static memextent_t *
create_memextent(partition_t *root_partition, cspace_t *root_cspace,
		 paddr_t phys_base, size_t size, pgtable_access_t access,
		 memextent_memtype_t memtype, cap_id_t *new_cap_id)
{
	bool device_mem = (memtype == MEMEXTENT_MEMTYPE_DEVICE);

	memextent_create_t     params_me = { .memextent		   = NULL,
					     .memextent_device_mem = device_mem };
	memextent_ptr_result_t me_ret;
	me_ret = partition_allocate_memextent(root_partition, params_me);
	if (me_ret.e != OK) {
		panic("Failed creation of memextent");
	}
	memextent_t *me = me_ret.r;

	memextent_attrs_t attrs = memextent_attrs_default();
	memextent_attrs_set_access(&attrs, access);
	memextent_attrs_set_memtype(&attrs, memtype);

	spinlock_acquire(&me->header.lock);
	error_t ret = memextent_configure(me, phys_base, size, attrs);
	if (ret != OK) {
		panic("Failed configuration of memextent");
	}
	spinlock_release(&me->header.lock);

	// Create a master cap for the memextent
	object_ptr_t obj_ptr;
	obj_ptr.memextent	  = me;
	cap_id_result_t capid_ret = cspace_create_master_cap(
		root_cspace, obj_ptr, OBJECT_TYPE_MEMEXTENT);
	if (capid_ret.e != OK) {
		panic("Error create memextent cap id.");
	}

	ret = object_activate_memextent(me);
	if (ret != OK) {
		panic("Failed activation of mem extent");
	}

	*new_cap_id = capid_ret.r;

	return me;
}

void
soc_qemu_handle_rootvm_init(partition_t *root_partition, cspace_t *root_cspace,
			    boot_env_data_t *env_data)
{
	// FIXME: The memory layout for QEMU is hardcoded here. We need to find a
	// better solution for this, possibly by using a system-device-tree
	// approach, that is consumed by us, and used to generate the HLOS VM
	// device-tree. We will also need to get the addresses such as
	// hlos-entry from this config such that ultimately these can all be
	// inputs from QEMU/user.

	// VM memory node. Includes entry point, DT, and rootfs
	env_data->hlos_vm_base	  = 0x40000000;
	env_data->hlos_vm_size	  = 0x20000000;
	env_data->entry_hlos	  = 0x41080000;
	env_data->hlos_dt_base	  = 0x44200000;
	env_data->hlos_ramfs_base = 0x44400000;
	env_data->device_me_base  = PLATFORM_DEVICES_BASE;

#if defined(WATCHDOG_DISABLE)
	env_data->watchdog_supported = false;
#endif

	// Add memory of VM to memdb first, so that we can use it to create a
	// non-device memextent
	//paddr_t phys_start = env_data->hlos_vm_base;
	//paddr_t phys_end =
	//	env_data->hlos_vm_base + (env_data->hlos_vm_size - 1U);

	//partition_t *hyp_partition = partition_get_private();
	//error_t	     err = memdb_insert(hyp_partition, phys_start, phys_end,
	//				(uintptr_t)root_partition,
	//				MEMDB_TYPE_PARTITION);
	//if (err != OK) {
	//	panic("Error adding VM memory to hyp_partition");
	//}

	// Create a device memextent to cover the full HW physical address
	// space reserved for devices, so that the resource manager can derive
	// device memextents.
	// Long term the intention is for a system device-tree to allow fine
	// grained memextent creation.

	memextent_t *me = create_memextent(
		root_partition, root_cspace, PLATFORM_DEVICES_BASE,
		PLATFORM_DEVICES_SIZE, PGTABLE_ACCESS_RW,
		MEMEXTENT_MEMTYPE_DEVICE, &env_data->device_me_capid);

	// Derive memextents for GICD, GICR and watchdog to effectively remove
	// them from the device memextent we provide to the rootvm.

	memextent_ptr_result_t me_ret;
	me_ret = memextent_derive(me, PLATFORM_GICD_BASE, 0x10000U,
				  MEMEXTENT_MEMTYPE_DEVICE, PGTABLE_ACCESS_RW);
	if (me_ret.e != OK) {
		panic("Failed creation of gicd memextent");
	}
	me_ret = memextent_derive(me, PLATFORM_GICR_BASE,
				  (PLATFORM_MAX_CORES << GICR_STRIDE_SHIFT),
				  MEMEXTENT_MEMTYPE_DEVICE, PGTABLE_ACCESS_RW);
	if (me_ret.e != OK) {
		panic("Failed creation of gicr memextent");
	}

	// Derive extent for UART and share it with RM
	me_ret = memextent_derive(me, PLATFORM_UART_BASE, PLATFORM_UART_SIZE,
				  MEMEXTENT_MEMTYPE_DEVICE, PGTABLE_ACCESS_RW);
	if (me_ret.e != OK) {
		panic("Failed creation of uart memextent");
	}

	// Create a master cap for the uart memextent
	object_ptr_t obj_ptr;
	obj_ptr.memextent	  = me_ret.r;
	cap_id_result_t capid_ret = cspace_create_master_cap(
		root_cspace, obj_ptr, OBJECT_TYPE_MEMEXTENT);
	if (capid_ret.e != OK) {
		panic("Error create memextent cap id.");
	}

	env_data->uart_address	= PLATFORM_UART_BASE;
	env_data->uart_me_capid = capid_ret.r;
}
#endif
