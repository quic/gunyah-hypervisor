// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcontainers.h>

#include <addrspace.h>
#include <atomic.h>
#include <gpt.h>
#include <list.h>
#include <memdb.h>
#include <memextent.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <pgtable.h>
#include <rcu.h>
#include <spinlock.h>
#include <util.h>

#include "event_handlers.h"

void
memextent_mapping_add_offset(gpt_value_t *value, size_t offset)
{
	vmaddr_t vbase = memextent_gpt_map_get_vbase(&value->me_map);

	memextent_gpt_map_set_vbase(&value->me_map, vbase + offset);
}

bool
memextent_mappings_equal(gpt_value_t x, gpt_value_t y)
{
	bool ret;

	if (memextent_gpt_map_get_ignore_attrs(&x.me_map) ||
	    memextent_gpt_map_get_ignore_attrs(&y.me_map)) {
		// We only need to check if the vbases are equal.
		ret = memextent_gpt_map_get_vbase(&x.me_map) ==
		      memextent_gpt_map_get_vbase(&y.me_map);
	} else {
		ret = memextent_gpt_map_is_equal(x.me_map, y.me_map);
	}

	return ret;
}

static error_t
allocate_sparse_mappings(memextent_t *me)
{
	error_t	     ret       = OK;
	partition_t *partition = me->header.partition;
	const size_t alloc_size =
		sizeof(memextent_sparse_mapping_t) * MEMEXTENT_MAX_MAPS;
	const size_t alloc_align = alignof(memextent_sparse_mapping_t);

	void_ptr_result_t alloc_ret =
		partition_alloc(partition, alloc_size, alloc_align);
	if (alloc_ret.e != OK) {
		ret = alloc_ret.e;
		goto out;
	}

	(void)memset_s(alloc_ret.r, alloc_size, 0, alloc_size);

	me->mappings.sparse = alloc_ret.r;

	for (index_t i = 0U; i < MEMEXTENT_MAX_MAPS; i++) {
		gpt_config_t config = gpt_config_default();
		gpt_config_set_max_bits(&config, GPT_PHYS_BITS);

		ret = gpt_init(&me->mappings.sparse[i].gpt, partition, config,
			       util_bit(GPT_TYPE_MEMEXTENT_MAPPING));
		assert(ret == OK);
	}

out:
	return ret;
}

static void
free_sparse_mappings(memextent_t *me)
{
	partition_t *partition = me->header.partition;
	const size_t alloc_size =
		sizeof(memextent_sparse_mapping_t) * MEMEXTENT_MAX_MAPS;

	assert(me->mappings.sparse != NULL);

	for (index_t i = 0U; i < MEMEXTENT_MAX_MAPS; i++) {
		gpt_destroy(&me->mappings.sparse[i].gpt);
	}

	(void)partition_free(partition, me->mappings.sparse, alloc_size);

	me->mappings.sparse = NULL;
}

static error_t
insert_gpt_mapping(memextent_sparse_mapping_t *map, paddr_t phys, size_t size,
		   vmaddr_t vbase, memextent_mapping_attrs_t attrs)
{
	assert(map != NULL);

	pgtable_vm_memtype_t memtype =
		memextent_mapping_attrs_get_memtype(&attrs);
	pgtable_access_t user_access =
		memextent_mapping_attrs_get_user_access(&attrs);
	pgtable_access_t kernel_access =
		memextent_mapping_attrs_get_kernel_access(&attrs);

	memextent_gpt_map_t gpt_map = memextent_gpt_map_default();
	memextent_gpt_map_set_vbase(&gpt_map, vbase);
	memextent_gpt_map_set_memtype(&gpt_map, memtype);
	memextent_gpt_map_set_user_access(&gpt_map, user_access);
	memextent_gpt_map_set_kernel_access(&gpt_map, kernel_access);

	gpt_entry_t gpt_entry = {
		.type  = GPT_TYPE_MEMEXTENT_MAPPING,
		.value = { .me_map = gpt_map },
	};

	return gpt_insert(&map->gpt, phys, size, gpt_entry, true);
}

static error_t
remove_gpt_mapping(memextent_sparse_mapping_t *map, paddr_t phys, size_t size,
		   vmaddr_t vbase)
{
	assert(map != NULL);

	memextent_gpt_map_t gpt_map = memextent_gpt_map_default();
	memextent_gpt_map_set_vbase(&gpt_map, vbase);
	memextent_gpt_map_set_ignore_attrs(&gpt_map, true);

	gpt_entry_t gpt_entry = {
		.type  = GPT_TYPE_MEMEXTENT_MAPPING,
		.value = { .me_map = gpt_map },
	};

	return gpt_remove(&map->gpt, phys, size, gpt_entry);
}

static error_t
update_gpt_mapping(memextent_sparse_mapping_t *map, paddr_t phys, size_t size,
		   memextent_gpt_map_t old_gpt_map,
		   memextent_gpt_map_t new_gpt_map)
{
	assert(map != NULL);
	assert(!memextent_gpt_map_get_ignore_attrs(&old_gpt_map));
	assert(!memextent_gpt_map_get_ignore_attrs(&new_gpt_map));

	gpt_entry_t old_gpt_entry = {
		.type  = GPT_TYPE_MEMEXTENT_MAPPING,
		.value = { .me_map = old_gpt_map },
	};

	gpt_entry_t new_gpt_entry = {
		.type  = GPT_TYPE_MEMEXTENT_MAPPING,
		.value = { .me_map = new_gpt_map },
	};

	return gpt_update(&map->gpt, phys, size, old_gpt_entry, new_gpt_entry);
}

static void
delete_sparse_mapping(memextent_sparse_mapping_t *map, addrspace_t *addrspace)
	REQUIRE_PREEMPT_DISABLED
{
	assert(atomic_load_relaxed(&map->addrspace) == addrspace);
	assert(gpt_is_empty(&map->gpt));

	spinlock_acquire_nopreempt(&addrspace->mapping_list_lock);
	(void)list_delete_node(&addrspace->basic_mapping_list,
			       &map->mapping_list_node);
	spinlock_release_nopreempt(&addrspace->mapping_list_lock);

	atomic_store_relaxed(&map->addrspace, NULL);
}

static bool
apply_access_mask(memextent_t *me, memextent_mapping_attrs_t *attrs)
{
	pgtable_access_t old_user_access =
		memextent_mapping_attrs_get_user_access(attrs);
	pgtable_access_t old_kernel_access =
		memextent_mapping_attrs_get_kernel_access(attrs);

	pgtable_access_t new_user_access =
		pgtable_access_mask(old_user_access, me->access);
	pgtable_access_t new_kernel_access =
		pgtable_access_mask(old_kernel_access, me->access);

	memextent_mapping_attrs_set_user_access(attrs, new_user_access);
	memextent_mapping_attrs_set_kernel_access(attrs, new_kernel_access);

	return (old_user_access != new_user_access) ||
	       (old_kernel_access != new_kernel_access);
}

static error_t
add_sparse_mapping(memextent_t *me, addrspace_t *addrspace, paddr_t phys,
		   size_t size, vmaddr_t vbase, memextent_mapping_attrs_t attrs)
	REQUIRE_SPINLOCK(me->lock)
{
	error_t err    = OK;
	bool	mapped = false;

	assert(me != NULL);
	assert(addrspace != NULL);

	memextent_sparse_mapping_t *empty_map = NULL;

	// First, try to use an existing mapping with matching addrspace.
	for (index_t i = 0U; !mapped && (i < MEMEXTENT_MAX_MAPS); i++) {
		memextent_sparse_mapping_t *map = &me->mappings.sparse[i];

		addrspace_t *as = atomic_load_relaxed(&map->addrspace);
		if (as == addrspace) {
			err = insert_gpt_mapping(map, phys, size, vbase, attrs);
			if (err == OK) {
				mapped = true;
			} else if (err == ERROR_BUSY) {
				// There is an overlapping entry in this
				// mapping's GPT, but we can try again with a
				// different mapping.
				err = OK;
			} else {
				// Unexpected GPT error.
				break;
			}
		} else if ((as == NULL) && (empty_map == NULL)) {
			empty_map = map;
		} else {
			// Mapping in use by another addrspace, or we have
			// already found an earlier empty mapping, continue.
		}
	}

	if (mapped || (err != OK)) {
		goto out;
	}

	if (empty_map == NULL) {
		err = ERROR_MEMEXTENT_MAPPINGS_FULL;
		goto out;
	}

	// We need an acquire fence as the empty mapping may have been cleared
	// without the memextent lock if the previous addrspace was destroyed.
	// This synchronises the earlier relaxed load of map->addrspace with the
	// store-release in memextent_deactivate_addrspace_sparse().
	atomic_thread_fence(memory_order_acquire);

	err = insert_gpt_mapping(empty_map, phys, size, vbase, attrs);
	if (err != OK) {
		goto out;
	}

	spinlock_acquire_nopreempt(&addrspace->mapping_list_lock);
	list_insert_at_head(&addrspace->sparse_mapping_list,
			    &empty_map->mapping_list_node);
	spinlock_release_nopreempt(&addrspace->mapping_list_lock);
	atomic_store_relaxed(&empty_map->addrspace, addrspace);

out:
	return err;
}

static error_t
remove_sparse_mapping(memextent_t *me, addrspace_t *addrspace, paddr_t phys,
		      size_t size, vmaddr_t vbase) REQUIRE_SPINLOCK(me->lock)
{
	error_t err	 = OK;
	bool	unmapped = false;

	assert(me != NULL);
	assert(addrspace != NULL);

	for (index_t i = 0U; !unmapped && (i < MEMEXTENT_MAX_MAPS); i++) {
		memextent_sparse_mapping_t *map = &me->mappings.sparse[i];

		addrspace_t *as = atomic_load_relaxed(&map->addrspace);
		if (as != addrspace) {
			continue;
		}

		err = remove_gpt_mapping(map, phys, size, vbase);
		if (err == OK) {
			unmapped = true;
			if (gpt_is_empty(&map->gpt)) {
				delete_sparse_mapping(map, addrspace);
			}
		} else if (err == ERROR_BUSY) {
			// The entry was not found in this mapping's GPT, but
			// may be in another mapping.
			err = OK;
		} else {
			// Unexpected GPT error.
			goto out;
		}
	}

	if (!unmapped) {
		err = ERROR_ADDR_INVALID;
	}

out:
	return err;
}

static error_t
memextent_map_range_sparse(paddr_t phys, size_t size, void *arg)
{
	error_t err;

	assert(size != 0U);
	assert(!util_add_overflows(phys, size - 1U));
	assert(arg != NULL);

	memextent_sparse_arg_t *me_arg = (memextent_sparse_arg_t *)arg;

	size_t offset = phys - me_arg->pbase;

	err = addrspace_map(me_arg->addrspace, me_arg->vbase + offset, size,
			    phys, me_arg->memtype, me_arg->kernel_access,
			    me_arg->user_access);
	if (err != OK) {
		me_arg->fail_addr = phys;
	}

	return err;
}

static error_t
memextent_unmap_range_sparse(paddr_t phys, size_t size, void *arg)
{
	assert(size != 0U);
	assert(!util_add_overflows(phys, size - 1U));
	assert(arg != NULL);

	memextent_sparse_arg_t *me_arg = (memextent_sparse_arg_t *)arg;

	size_t offset = phys - me_arg->pbase;

	return addrspace_unmap(me_arg->addrspace, me_arg->vbase + offset, size,
			       phys);
}

static error_t
do_as_map(addrspace_t *as, vmaddr_t vbase, size_t size, paddr_t phys,
	  memextent_mapping_attrs_t attrs)
{
	assert(as != NULL);

	pgtable_vm_memtype_t memtype =
		memextent_mapping_attrs_get_memtype(&attrs);
	pgtable_access_t kernel_access =
		memextent_mapping_attrs_get_kernel_access(&attrs);
	pgtable_access_t user_access =
		memextent_mapping_attrs_get_user_access(&attrs);

	return addrspace_map(as, vbase, size, phys, memtype, kernel_access,
			     user_access);
}

static error_t
apply_mappings(memextent_t *me, paddr_t phys, size_t size, bool unmap,
	       size_t *fail_offset) REQUIRE_SPINLOCK(me->lock)
	REQUIRE_LOCK(me->mappings)
{
	error_t err = OK;

	memextent_mapping_t maps[MEMEXTENT_MAX_MAPS] = { 0 };

	size_t offset = 0U;
	while (offset < size) {
		paddr_t curr_phys = phys + offset;
		size_t	curr_size = size - offset;

		for (index_t i = 0U; i < MEMEXTENT_MAX_MAPS; i++) {
			maps[i] = memextent_lookup_mapping(me, curr_phys,
							   curr_size, i);
			// For each iteration, we only want to transfer the
			// range covered by the smallest mapping (or unmapped
			// range).
			curr_size = util_min(maps[i].size, curr_size);
		}

		index_t fail_idx = 0U;
		for (index_t i = 0U; i < MEMEXTENT_MAX_MAPS; i++) {
			if (maps[i].addrspace == NULL) {
				continue;
			}

			if (unmap) {
				err = addrspace_unmap(maps[i].addrspace,
						      maps[i].vbase, curr_size,
						      curr_phys);
			} else {
				err = do_as_map(maps[i].addrspace,
						maps[i].vbase, curr_size,
						curr_phys, maps[i].attrs);
			}

			if (err != OK) {
				fail_idx = i;
				break;
			}
		}
		if (unmap && (err != OK)) {
			break;
		}

		if (err != OK) {
			if (fail_offset != NULL) {
				*fail_offset = offset;
			} else {
				// If fail_offset wasn't provided then we assume
				// the caller cannot recover from the error.
				panic("Failed to apply sparse mappings");
			}

			for (index_t i = 0U; i < fail_idx; i++) {
				if (maps[i].addrspace == NULL) {
					continue;
				}

				error_t revert_err;
				if (unmap) {
					revert_err = do_as_map(
						maps[i].addrspace,
						maps[i].vbase, curr_size,
						curr_phys, maps[i].attrs);
				} else {
					revert_err = addrspace_unmap(
						maps[i].addrspace,
						maps[i].vbase, curr_size,
						curr_phys);
				}

				if (revert_err != OK) {
					panic("Failed to revert sparse mappings");
				}
			}

			break;
		}

		offset += curr_size;
	}

	return err;
}

static void
revert_mapping_transfer(memextent_mapping_t x_mappings[],
			memextent_mapping_t y_mappings[], const bool x_match[],
			const bool y_match[], paddr_t curr_phys,
			size_t curr_size, index_t x_idx, index_t y_idx)
{
	for (index_t i = 0U; i < MEMEXTENT_MAX_MAPS; i++) {
		memextent_mapping_t *xmap = &x_mappings[i];
		memextent_mapping_t *ymap = &y_mappings[i];

		error_t revert_err = OK;

		if ((i < x_idx) && (xmap->addrspace != NULL) && !x_match[i]) {
			revert_err = do_as_map(xmap->addrspace, xmap->vbase,
					       curr_size, curr_phys,
					       xmap->attrs);
		}

		if ((revert_err == OK) && (i < y_idx) &&
		    (ymap->addrspace != NULL) && !y_match[i]) {
			revert_err = addrspace_unmap(ymap->addrspace,
						     ymap->vbase, curr_size,
						     curr_phys);
		}

		if (revert_err != OK) {
			panic("Failed to revert mapping transfer");
		}
	}
}

static error_t
do_mapping_transfer(memextent_t *x, memextent_t *y, paddr_t phys, size_t size,
		    size_t *fail_offset) REQUIRE_SPINLOCK(x->lock)
	REQUIRE_SPINLOCK(y->lock) REQUIRE_LOCK(x->mappings)
		REQUIRE_LOCK(y->mappings)
{
	error_t err = OK;

	memextent_mapping_t x_mappings[MEMEXTENT_MAX_MAPS] = { 0 };
	memextent_mapping_t y_mappings[MEMEXTENT_MAX_MAPS] = { 0 };

	size_t offset = 0U;
	while (offset < size) {
		paddr_t curr_phys = phys + offset;
		size_t	curr_size = size - offset;

		bool x_match[MEMEXTENT_MAX_MAPS] = { 0 };
		bool y_match[MEMEXTENT_MAX_MAPS] = { 0 };

		for (index_t i = 0U; i < MEMEXTENT_MAX_MAPS; i++) {
			x_mappings[i] = memextent_lookup_mapping(x, curr_phys,
								 curr_size, i);
			y_mappings[i] = memextent_lookup_mapping(y, curr_phys,
								 curr_size, i);

			// For each iteration, we only want to transfer the
			// range covered by the smallest mapping (or unmapped
			// range).
			curr_size = util_min(x_mappings[i].size, curr_size);
			curr_size = util_min(y_mappings[i].size, curr_size);
		}

		for (index_t i = 0U; i < MEMEXTENT_MAX_MAPS; i++) {
			memextent_mapping_t *xmap = &x_mappings[i];
			if (xmap->addrspace == NULL) {
				continue;
			}

			for (index_t j = 0U; j < MEMEXTENT_MAX_MAPS; j++) {
				memextent_mapping_t *ymap = &y_mappings[j];
				if (xmap->addrspace != ymap->addrspace) {
					continue;
				}

				bool vbase_match = xmap->vbase == ymap->vbase;
				bool attrs_match =
					memextent_mapping_attrs_is_equal(
						xmap->attrs, ymap->attrs);

				// We only need to unmap from x if the vbase
				// does not match. If the vbases match but the
				// attrs don't, applying y's mapping will
				// overwrite the mapping from x.
				x_match[i] = vbase_match;
				y_match[j] = vbase_match && attrs_match;
			}
		}

		index_t x_idx = 0U;
		index_t y_idx = 0U;
		for (index_t i = 0U; i < MEMEXTENT_MAX_MAPS; i++) {
			memextent_mapping_t *xmap = &x_mappings[i];
			memextent_mapping_t *ymap = &y_mappings[i];

			if ((xmap->addrspace != NULL) && !x_match[i]) {
				err = addrspace_unmap(xmap->addrspace,
						      xmap->vbase, curr_size,
						      curr_phys);
				if (err != OK) {
					break;
				}
			}

			x_idx++;

			if ((ymap->addrspace != NULL) && !y_match[i]) {
				err = do_as_map(ymap->addrspace, ymap->vbase,
						curr_size, curr_phys,
						ymap->attrs);
				if (err != OK) {
					break;
				}
			}

			y_idx++;
		}

		if (err != OK) {
			if (fail_offset != NULL) {
				*fail_offset = offset;
			} else {
				// If fail_offset wasn't provided then we assume
				// the caller cannot recover from the error.
				panic("Failed to do sparse mapping transfer");
			}

			revert_mapping_transfer(x_mappings, y_mappings, x_match,
						y_match, curr_phys, curr_size,
						x_idx, y_idx);
			break;
		}

		offset += curr_size;
	}

	return err;
}

static error_t
update_memdb_partition_and_extent(memextent_t *me, paddr_t phys, size_t size,
				  bool to_partition) REQUIRE_SPINLOCK(me->lock)
{
	error_t	     ret;
	partition_t *hyp_partition = partition_get_private();

	assert(hyp_partition != NULL);
	assert(me != NULL);
	assert(!util_add_overflows(phys, size - 1U));

	partition_t *parent_partition = me->header.partition;

	assert(parent_partition != NULL);

	uintptr_t    object, prev_object;
	memdb_type_t type, prev_type;

	if (to_partition) {
		object	    = (uintptr_t)parent_partition;
		type	    = MEMDB_TYPE_PARTITION;
		prev_object = (uintptr_t)me;
		prev_type   = MEMDB_TYPE_EXTENT;
	} else {
		object	    = (uintptr_t)me;
		type	    = MEMDB_TYPE_EXTENT;
		prev_object = (uintptr_t)parent_partition;
		prev_type   = MEMDB_TYPE_PARTITION;
	}

	paddr_t end = phys + (size - 1U);

	ret = memdb_update(hyp_partition, phys, end, object, type, prev_object,
			   prev_type);
	if (ret == ERROR_MEMDB_NOT_OWNER) {
		// We might have failed to take ownership because a previously
		// deleted memextent has not yet been cleaned up, so wait for a
		// RCU grace period and then retry. If it still fails after
		// that, there's a real conflict.
		spinlock_release(&me->lock);
		rcu_sync();
		spinlock_acquire(&me->lock);
		ret = memdb_update(hyp_partition, phys, end, object, type,
				   prev_object, prev_type);
	}

	return ret;
}

static error_t
update_memdb_two_extents(memextent_t *from, memextent_t *to, paddr_t phys,
			 size_t size, bool from_locked_first)
	REQUIRE_SPINLOCK(from->lock) REQUIRE_SPINLOCK(to->lock)
{
	error_t	     ret;
	partition_t *hyp_partition = partition_get_private();

	assert(hyp_partition != NULL);
	assert(from != NULL);
	assert(to != NULL);
	assert(!util_add_overflows(phys, size - 1U));

	paddr_t end = phys + (size - 1U);

	ret = memdb_update(hyp_partition, phys, end, (uintptr_t)to,
			   MEMDB_TYPE_EXTENT, (uintptr_t)from,
			   MEMDB_TYPE_EXTENT);
	if (ret == ERROR_MEMDB_NOT_OWNER) {
		// We might have failed to take ownership because a previously
		// deleted memextent has not yet been cleaned up, so wait for a
		// RCU grace period and then retry. If it still fails after
		// that, there's a real conflict.
		if (from_locked_first) {
			spinlock_release_nopreempt(&to->lock);
			spinlock_release(&from->lock);
		} else {
			spinlock_release_nopreempt(&from->lock);
			spinlock_release(&to->lock);
		}

		rcu_sync();

		if (from_locked_first) {
			spinlock_acquire(&from->lock);
			spinlock_acquire_nopreempt(&to->lock);
		} else {
			spinlock_acquire(&to->lock);
			spinlock_acquire_nopreempt(&from->lock);
		}

		ret = memdb_update(hyp_partition, phys, end, (uintptr_t)to,
				   MEMDB_TYPE_EXTENT, (uintptr_t)from,
				   MEMDB_TYPE_EXTENT);
	}

	return ret;
}

static error_t
get_phys_range(paddr_t phys, size_t size, void *arg)
{
	phys_range_result_t *ret = (phys_range_result_t *)arg;
	assert(ret != NULL);

	phys_range_t range = {
		.base = phys,
		.size = size,
	};

	*ret = phys_range_result_ok(range);

	return ERROR_RETRY;
}

static phys_range_result_t
lookup_phys_range(memextent_t *me, size_t *offset)
{
	phys_range_result_t ret = phys_range_result_error(ERROR_FAILURE);

	assert(offset != NULL);
	assert(*offset < me->size);

	paddr_t start = me->phys_base + *offset;
	paddr_t end   = me->phys_base + (me->size - 1U);

	error_t err = memdb_range_walk((uintptr_t)me, MEMDB_TYPE_EXTENT, start,
				       end, get_phys_range, &ret);
	assert((err == OK) || (ret.e == OK));

	if (ret.e == OK) {
		*offset = ret.r.base + ret.r.size - me->phys_base;
	}

	return ret;
}

error_t
memextent_activate_sparse(memextent_t *me)
{
	error_t	     ret;
	partition_t *hyp_partition = partition_get_private();

	assert(me != NULL);
	assert(hyp_partition != NULL);

	ret = allocate_sparse_mappings(me);
	if (ret != OK) {
		goto out;
	}

	if (me->device_mem) {
		assert(me->memtype == MEMEXTENT_MEMTYPE_DEVICE);

		ret = memdb_insert(hyp_partition, me->phys_base,
				   me->phys_base + (me->size - 1U),
				   (uintptr_t)me, MEMDB_TYPE_EXTENT);
		if (ret != OK) {
			free_sparse_mappings(me);
		}
	} else {
		// Memory will be added to the memextent after
		// activation; there is nothing to do now.
	}

out:
	return ret;
}

error_t
memextent_activate_derive_sparse(memextent_t *me)
{
	error_t	     ret;
	partition_t *hyp_partition = partition_get_private();

	assert(me != NULL);
	assert(me->parent != NULL);

	ret = allocate_sparse_mappings(me);
	if (ret != OK) {
		goto out;
	}

	spinlock_acquire(&me->parent->lock);
	spinlock_acquire_nopreempt(&me->lock);

	if (me->parent->attached_size != 0U) {
		ret = ERROR_BUSY;
		goto out_locked;
	}

	bool transfer = !memextent_supports_donation(me->parent);
	if (transfer) {
		// The parent does not support donation, so we need to transfer
		// ownership of the memextent's entire range now.
		ret = update_memdb_two_extents(me->parent, me, me->phys_base,
					       me->size, true);
		if (ret != OK) {
			goto out_locked;
		}
	}

	memextent_retain_mappings(me->parent);

	bool access_changed = false;
	for (index_t i = 0U; i < MEMEXTENT_MAX_MAPS; i++) {
		size_t offset = 0U;
		while (offset < me->size) {
			paddr_t phys = me->phys_base + offset;
			size_t	size = me->size - offset;

			memextent_mapping_t parent_map =
				memextent_lookup_mapping(me->parent, phys, size,
							 i);
			offset += parent_map.size;

			if (parent_map.addrspace == NULL) {
				continue;
			}

			memextent_mapping_attrs_t attrs = parent_map.attrs;

			if (apply_access_mask(me, &attrs)) {
				access_changed = true;
			}

			ret = add_sparse_mapping(me, parent_map.addrspace, phys,
						 parent_map.size,
						 parent_map.vbase, attrs);
			if (ret != OK) {
				break;
			}
		}
	}

	if ((ret == OK) && transfer && access_changed) {
		// The child memextent has modified the mappings of memory it
		// now owns. Ensure these mappings changes are applied.
		size_t fail_offset = 0U;

		memextent_retain_mappings(me);

		ret = do_mapping_transfer(me->parent, me, me->phys_base,
					  me->size, &fail_offset);
		if (ret != OK) {
			// Revert mapping changes.
			(void)do_mapping_transfer(me, me->parent, me->phys_base,
						  fail_offset, NULL);
		}

		memextent_release_mappings(me, ret != OK);
	}

	memextent_release_mappings(me->parent, false);

	if (ret == OK) {
		list_insert_at_head(&me->parent->children_list,
				    &me->children_list_node);
	} else if (transfer) {
		error_t err = memdb_update(hyp_partition, me->phys_base,
					   me->phys_base + (me->size - 1U),
					   (uintptr_t)me->parent,
					   MEMDB_TYPE_EXTENT, (uintptr_t)me,
					   MEMDB_TYPE_EXTENT);
		assert(err == OK);
	} else {
		// Nothing to do.
	}

out_locked:
	spinlock_release_nopreempt(&me->lock);
	spinlock_release(&me->parent->lock);

	if (ret != OK) {
		free_sparse_mappings(me);
	}

out:
	return ret;
}

bool
memextent_supports_donation_sparse(void)
{
	return true;
}

static error_t
donate_memextents_common(memextent_t *from, memextent_t *to, paddr_t phys,
			 size_t size, bool lock_from_first)
{
	error_t ret;

	if (lock_from_first) {
		spinlock_acquire(&from->lock);
		spinlock_acquire_nopreempt(&to->lock);
	} else {
		spinlock_acquire(&to->lock);
		spinlock_acquire_nopreempt(&from->lock);
	}

	ret = update_memdb_two_extents(from, to, phys, size, lock_from_first);
	if (ret != OK) {
		goto out;
	}

	size_t fail_offset = 0U;

	memextent_retain_mappings(from);
	memextent_retain_mappings(to);

	ret = do_mapping_transfer(from, to, phys, size, &fail_offset);
	if (ret != OK) {
		(void)do_mapping_transfer(from, to, phys, fail_offset, NULL);
	}

	memextent_release_mappings(to, false);
	memextent_release_mappings(from, false);

out:
	if (lock_from_first) {
		spinlock_release_nopreempt(&to->lock);
		spinlock_release(&from->lock);
	} else {
		spinlock_release_nopreempt(&from->lock);
		spinlock_release(&to->lock);
	}

	return ret;
}

error_t
memextent_donate_child_sparse(memextent_t *me, paddr_t phys, size_t size,
			      bool reverse)
{
	error_t ret;

	assert(me != NULL);

	if (me->parent != NULL) {
		if (me->parent->type != MEMEXTENT_TYPE_SPARSE) {
			ret = ERROR_ARGUMENT_INVALID;
			goto out;
		}

		// The parent extent is always locked first.
		if (reverse) {
			ret = donate_memextents_common(me, me->parent, phys,
						       size, false);
		} else {
			ret = donate_memextents_common(me->parent, me, phys,
						       size, true);
		}

		goto out;
	}

	spinlock_acquire(&me->lock);

	ret = update_memdb_partition_and_extent(me, phys, size, reverse);
	if (ret != OK) {
		goto unlock_me;
	}

	size_t fail_offset = 0U;
	memextent_retain_mappings(me);

	ret = apply_mappings(me, phys, size, reverse, &fail_offset);
	if (ret != OK) {
		(void)apply_mappings(me, phys, fail_offset, true, NULL);
	}

	memextent_release_mappings(me, false);

unlock_me:
	spinlock_release(&me->lock);
out:
	return ret;
}

error_t
memextent_donate_sibling_sparse(memextent_t *from, memextent_t *to,
				paddr_t phys, size_t size)
{
	error_t ret;

	assert(from != to);
	assert(from->parent == to->parent);

	if (to->type != MEMEXTENT_TYPE_SPARSE) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	// To prevent deadlocks, we need to obtain the memextents' locks in a
	// consistent order. Lock the child at the lower address first.
	ret = donate_memextents_common(from, to, phys, size, from < to);

out:
	return ret;
}

error_t
memextent_map_sparse(memextent_t *me, addrspace_t *addrspace, vmaddr_t vm_base,
		     memextent_mapping_attrs_t map_attrs)
{
	return memextent_map_partial_sparse(me, addrspace, vm_base, 0U,
					    me->size, map_attrs);
}

error_t
memextent_map_partial_sparse(memextent_t *me, addrspace_t *addrspace,
			     vmaddr_t vm_base, size_t offset, size_t size,
			     memextent_mapping_attrs_t map_attrs)
{
	error_t ret;

	assert(!util_add_overflows(offset, size - 1U));
	assert(!util_add_overflows(vm_base, size - 1U));

	if (vm_base + (size - 1U) >= util_bit(GPT_VBASE_BITS)) {
		ret = ERROR_ADDR_INVALID;
		goto out;
	}

	paddr_t phys = me->phys_base + offset;

	spinlock_acquire(&me->lock);

	ret = add_sparse_mapping(me, addrspace, phys, size, vm_base, map_attrs);
	if (ret != OK) {
		goto out_locked;
	}

	pgtable_vm_memtype_t memtype =
		memextent_mapping_attrs_get_memtype(&map_attrs);
	pgtable_access_t user_access =
		memextent_mapping_attrs_get_user_access(&map_attrs);
	pgtable_access_t kernel_access =
		memextent_mapping_attrs_get_kernel_access(&map_attrs);

	memextent_sparse_arg_t arg = {
		.addrspace     = addrspace,
		.vbase	       = vm_base,
		.pbase	       = phys,
		.memtype       = memtype,
		.user_access   = user_access,
		.kernel_access = kernel_access,
	};

	ret = memdb_range_walk((uintptr_t)me, MEMDB_TYPE_EXTENT, phys,
			       phys + (size - 1U), memextent_map_range_sparse,
			       &arg);
	if (ret != OK) {
		error_t err;

		if (arg.fail_addr != phys) {
			// Unmap any ranges that were mapped in the memdb walk.
			err = memdb_range_walk((uintptr_t)me, MEMDB_TYPE_EXTENT,
					       phys, arg.fail_addr - 1U,
					       memextent_unmap_range_sparse,
					       &arg);
			assert(err == OK);
		}

		err = remove_sparse_mapping(me, addrspace, phys, size, vm_base);
		assert(err == OK);
	}

out_locked:
	spinlock_release(&me->lock);
out:
	return ret;
}

error_t
memextent_unmap_sparse(memextent_t *me, addrspace_t *addrspace,
		       vmaddr_t vm_base)
{
	return memextent_unmap_partial_sparse(me, addrspace, vm_base, 0U,
					      me->size);
}

error_t
memextent_unmap_partial_sparse(memextent_t *me, addrspace_t *addrspace,
			       vmaddr_t vm_base, size_t offset, size_t size)
{
	error_t ret;

	assert(!util_add_overflows(offset, size - 1U));
	assert(!util_add_overflows(vm_base, size - 1U));

	if (vm_base + (size - 1U) >= util_bit(GPT_VBASE_BITS)) {
		ret = ERROR_ADDR_INVALID;
		goto out;
	}

	paddr_t phys = me->phys_base + offset;

	spinlock_acquire(&me->lock);

	ret = remove_sparse_mapping(me, addrspace, phys, size, vm_base);
	if (ret != OK) {
		goto out_locked;
	}

	memextent_sparse_arg_t arg = {
		.addrspace = addrspace,
		.vbase	   = vm_base,
		.pbase	   = phys,
	};

	ret = memdb_range_walk((uintptr_t)me, MEMDB_TYPE_EXTENT, phys,
			       phys + (size - 1U), memextent_unmap_range_sparse,
			       &arg);
	assert(ret == OK);

out_locked:
	spinlock_release(&me->lock);
out:
	return ret;
}

bool
memextent_unmap_all_sparse(memextent_t *me)
{
	spinlock_acquire(&me->lock);
	memextent_retain_mappings(me);

	size_t offset = 0U;
	while (offset < me->size) {
		phys_range_result_t range = lookup_phys_range(me, &offset);
		if (range.e != OK) {
			break;
		}

		error_t err = apply_mappings(me, range.r.base, range.r.size,
					     true, NULL);
		assert(err == OK);
	}

	memextent_release_mappings(me, true);
	spinlock_release(&me->lock);

	return true;
}

error_t
memextent_update_access_sparse(memextent_t *me, addrspace_t *addrspace,
			       vmaddr_t			vm_base,
			       memextent_access_attrs_t access_attrs)
{
	return memextent_update_access_partial_sparse(me, addrspace, vm_base, 0,
						      me->size, access_attrs);
}

error_t
memextent_update_access_partial_sparse(memextent_t *me, addrspace_t *addrspace,
				       vmaddr_t vm_base, size_t offset,
				       size_t			size,
				       memextent_access_attrs_t access_attrs)
{
	error_t ret;
	paddr_t phys = me->phys_base + offset;

	assert(!util_add_overflows(offset, size - 1U));
	assert(!util_add_overflows(vm_base, size - 1U));

	if (vm_base + (size - 1U) >= util_bit(GPT_VBASE_BITS)) {
		ret = ERROR_ADDR_INVALID;
		goto out;
	}

	spinlock_acquire(&me->lock);

	memextent_sparse_mapping_t *update_map	= NULL;
	memextent_gpt_map_t	    old_gpt_map = memextent_gpt_map_default();

	for (index_t i = 0U; i < MEMEXTENT_MAX_MAPS; i++) {
		memextent_sparse_mapping_t *map = &me->mappings.sparse[i];

		addrspace_t *as = atomic_load_relaxed(&map->addrspace);
		if (as != addrspace) {
			continue;
		}

		// We need to keep the existing memtype when updating access.
		// Perform a lookup on the first page of the mapping so we know
		// what it is. If the memtype isn't consistent for the range
		// then the GPT update will detect this and return an error.
		gpt_lookup_result_t lookup_ret =
			gpt_lookup(&map->gpt, phys, PGTABLE_VM_PAGE_SIZE);
		if (lookup_ret.entry.type == GPT_TYPE_EMPTY) {
			continue;
		}

		assert(lookup_ret.entry.type == GPT_TYPE_MEMEXTENT_MAPPING);

		old_gpt_map = lookup_ret.entry.value.me_map;
		if (memextent_gpt_map_get_vbase(&old_gpt_map) == vm_base) {
			break;
		}
	}

	if (update_map == NULL) {
		ret = ERROR_ADDR_INVALID;
		goto out_locked;
	}

	pgtable_access_t new_user_access =
		memextent_access_attrs_get_user_access(&access_attrs);
	pgtable_access_t new_kernel_access =
		memextent_access_attrs_get_kernel_access(&access_attrs);

	memextent_gpt_map_t new_gpt_map = old_gpt_map;
	memextent_gpt_map_set_user_access(&new_gpt_map, new_user_access);
	memextent_gpt_map_set_kernel_access(&new_gpt_map, new_kernel_access);

	ret = update_gpt_mapping(update_map, phys, size, old_gpt_map,
				 new_gpt_map);
	if (ret != OK) {
		goto out_locked;
	}

	memextent_sparse_arg_t arg = {
		.addrspace     = addrspace,
		.vbase	       = vm_base,
		.pbase	       = phys,
		.memtype       = memextent_gpt_map_get_memtype(&new_gpt_map),
		.user_access   = new_user_access,
		.kernel_access = new_kernel_access,
	};

	ret = memdb_range_walk((uintptr_t)me, MEMDB_TYPE_EXTENT, phys,
			       phys + (size - 1U), memextent_map_range_sparse,
			       &arg);
	if (ret != OK) {
		error_t err;

		if (arg.fail_addr != phys) {
			// Revert any access changes applied to the addrspace.
			arg.user_access =
				memextent_gpt_map_get_user_access(&old_gpt_map);
			arg.kernel_access = memextent_gpt_map_get_kernel_access(
				&old_gpt_map);

			err = memdb_range_walk((uintptr_t)me, MEMDB_TYPE_EXTENT,
					       phys, arg.fail_addr - 1U,
					       memextent_map_range_sparse,
					       &arg);
			assert(err == OK);
		}

		// Revert the GPT update.
		err = update_gpt_mapping(update_map, phys, size, new_gpt_map,
					 old_gpt_map);
		assert(err == OK);
	}

out_locked:
	spinlock_release(&me->lock);
out:
	return ret;
}

bool
memextent_is_mapped_sparse(memextent_t *me, addrspace_t *addrspace,
			   bool exclusive)
{
	bool ret = false;

	for (index_t i = 0; i < MEMEXTENT_MAX_MAPS; i++) {
		memextent_sparse_mapping_t *map = &me->mappings.sparse[i];

		addrspace_t *as = atomic_load_relaxed(&map->addrspace);
		if (as == addrspace) {
			ret = true;
		} else if (as != NULL) {
			ret = false;
		} else {
			continue;
		}

		if (ret != exclusive) {
			break;
		}
	}

	return ret;
}

bool
memextent_deactivate_sparse(memextent_t *me)
{
	assert(me != NULL);

	// There should be no children by this time
	assert(list_is_empty(&me->children_list));

	if (me->parent == NULL) {
		(void)memextent_unmap_all_sparse(me);
		goto out;
	}

	spinlock_acquire(&me->parent->lock);
	spinlock_acquire_nopreempt(&me->lock);

	memextent_retain_mappings(me->parent);
	memextent_retain_mappings(me);

	size_t offset = 0U;
	while (offset < me->size) {
		phys_range_result_t range = lookup_phys_range(me, &offset);
		if (range.e != OK) {
			break;
		}

		error_t err = do_mapping_transfer(me, me->parent, range.r.base,
						  range.r.size, NULL);
		assert(err == OK);
	}

	memextent_release_mappings(me->parent, false);
	memextent_release_mappings(me, true);

	spinlock_release_nopreempt(&me->lock);
	spinlock_release(&me->parent->lock);

out:
	return true;
}

static error_t
memextent_return_range(paddr_t base, size_t size, void *arg)
{
	partition_t *hyp_partition = partition_get_private();

	assert(hyp_partition != NULL);
	assert(size != 0U);
	assert(!util_add_overflows(base, size - 1U));
	assert(arg != NULL);

	memextent_t *me = (memextent_t *)arg;
	uintptr_t    parent;
	memdb_type_t parent_type;

	if (me->parent != NULL) {
		parent	    = (uintptr_t)me->parent;
		parent_type = MEMDB_TYPE_EXTENT;
	} else {
		parent	    = (uintptr_t)me->header.partition;
		parent_type = MEMDB_TYPE_PARTITION;
	}

	return memdb_update(hyp_partition, base, base + (size - 1U), parent,
			    parent_type, (uintptr_t)me, MEMDB_TYPE_EXTENT);
}

bool
memextent_cleanup_sparse(memextent_t *me)
{
	assert(me != NULL);

	if (!me->active) {
		goto out;
	}

	// Walk over the memextent's range and donate any memory still
	// owned by the extent back to the parent.
	error_t err = memdb_range_walk((uintptr_t)me, MEMDB_TYPE_EXTENT,
				       me->phys_base,
				       me->phys_base + (me->size - 1U),
				       memextent_return_range, me);
	assert(err == OK);

	memextent_t *parent = me->parent;
	if (parent != NULL) {
		// Remove extent from parent's list of children.
		spinlock_acquire(&parent->lock);
		(void)list_delete_node(&parent->children_list,
				       &me->children_list_node);
		spinlock_release(&parent->lock);
	}

	free_sparse_mappings(me);

out:
	return true;
}

bool
memextent_retain_mappings_sparse(memextent_t *me) REQUIRE_SPINLOCK(me->lock)
{
	assert(me != NULL);

	rcu_read_start();
	for (index_t i = 0U; i < MEMEXTENT_MAX_MAPS; i++) {
		memextent_sparse_mapping_t *map = &me->mappings.sparse[i];

		addrspace_t *as = atomic_load_consume(&map->addrspace);
		if ((as != NULL) && object_get_addrspace_safe(as)) {
			map->retained = true;
		}
	}
	rcu_read_finish();

	return true;
}

bool
memextent_release_mappings_sparse(memextent_t *me, bool clear)
	REQUIRE_SPINLOCK(me->lock)
{
	assert(me != NULL);

	for (index_t i = 0U; i < MEMEXTENT_MAX_MAPS; i++) {
		memextent_sparse_mapping_t *map = &me->mappings.sparse[i];

		if (!map->retained) {
			continue;
		}

		addrspace_t *as = atomic_load_relaxed(&map->addrspace);
		assert(as != NULL);

		if (clear) {
			gpt_clear_all(&map->gpt);
			delete_sparse_mapping(map, as);
		}

		object_put_addrspace(as);
		map->retained = false;
	}

	return true;
}

memextent_mapping_result_t
memextent_lookup_mapping_sparse(memextent_t *me, paddr_t phys, size_t size,
				index_t i)
{
	assert(me != NULL);
	assert(i < MEMEXTENT_MAX_MAPS);
	assert((phys >= me->phys_base) &&
	       ((phys + (size - 1U)) <= (me->phys_base + (me->size - 1U))));

	memextent_mapping_t ret = {
		.size = size,
	};

	memextent_sparse_mapping_t *map = &me->mappings.sparse[i];

	if (!map->retained) {
		goto out;
	}

	addrspace_t *as = atomic_load_relaxed(&map->addrspace);
	assert(as != NULL);

	gpt_lookup_result_t lookup = gpt_lookup(&map->gpt, phys, size);

	ret.size = lookup.size;

	if (lookup.entry.type == GPT_TYPE_EMPTY) {
		goto out;
	}

	assert(lookup.entry.type == GPT_TYPE_MEMEXTENT_MAPPING);

	memextent_gpt_map_t gpt_map = lookup.entry.value.me_map;
	assert(!memextent_gpt_map_get_ignore_attrs(&gpt_map));

	memextent_mapping_attrs_t attrs = memextent_mapping_attrs_default();
	memextent_mapping_attrs_set_memtype(
		&attrs, memextent_gpt_map_get_memtype(&gpt_map));
	memextent_mapping_attrs_set_user_access(
		&attrs, memextent_gpt_map_get_user_access(&gpt_map));
	memextent_mapping_attrs_set_kernel_access(
		&attrs, memextent_gpt_map_get_kernel_access(&gpt_map));

	ret.addrspace = as;
	ret.vbase     = memextent_gpt_map_get_vbase(&gpt_map);
	ret.attrs     = attrs;

out:
	return memextent_mapping_result_ok(ret);
}

error_t
memextent_create_addrspace_sparse(addrspace_create_t params)
{
	addrspace_t *addrspace = params.addrspace;
	assert(addrspace != NULL);

	list_init(&addrspace->sparse_mapping_list);

	return OK;
}

void
memextent_deactivate_addrspace_sparse(addrspace_t *addrspace)
{
	assert(addrspace != NULL);

	spinlock_acquire(&addrspace->mapping_list_lock);

	list_t *list = &addrspace->sparse_mapping_list;

	memextent_sparse_mapping_t *map = NULL;
	list_foreach_container_maydelete (map, list, memextent_sparse_mapping,
					  mapping_list_node) {
		// An object_put() call is a release operation, and if the
		// refcount reaches zero it is also an acquire operation. As
		// such, we should have observed all prior updates to the GPT
		// despite not holding the memextent lock. Additionally, the
		// mapping won't be reused until the addrspace pointer is
		// cleared below, so it is also safe to clear the GPT without
		// holding the lock.
		gpt_clear_all(&map->gpt);
		(void)list_delete_node(list, &map->mapping_list_node);

		// Use store-release to ensure the above updates are observed
		// when the empty mapping is reused. This matches with the
		// acquire fence in add_sparse_mapping().
		atomic_store_release(&map->addrspace, NULL);
	}

	spinlock_release(&addrspace->mapping_list_lock);
}
