// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcontainers.h>

#include <addrspace.h>
#include <bitmap.h>
#include <compiler.h>
#include <list.h>
#include <memdb.h>
#include <memextent.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <pgtable.h>
#include <rcu.h>
#include <spinlock.h>
#include <util.h>

#include <events/memextent.h>

#include "event_handlers.h"

static error_t
allocate_mappings(memextent_t *me)
{
	error_t	     ret       = OK;
	partition_t *partition = me->header.partition;
	const size_t alloc_size =
		sizeof(memextent_basic_mapping_t) * MEMEXTENT_MAX_MAPS;
	const size_t alloc_align = alignof(memextent_basic_mapping_t);

	void_ptr_result_t alloc_ret =
		partition_alloc(partition, alloc_size, alloc_align);
	if (alloc_ret.e != OK) {
		ret = alloc_ret.e;
		goto out;
	}

	(void)memset_s(alloc_ret.r, alloc_size, 0, alloc_size);

	me->mappings.basic = alloc_ret.r;

out:
	return ret;
}

static void
free_mappings(memextent_t *me)
{
	partition_t *partition = me->header.partition;
	const size_t alloc_size =
		sizeof(memextent_basic_mapping_t) * MEMEXTENT_MAX_MAPS;

	assert(me->mappings.basic != NULL);

	(void)partition_free(partition, me->mappings.basic, alloc_size);

	me->mappings.basic = NULL;
}

// Needs to be called holding a reference to the addrspace to be used
static error_t
memextent_do_map(memextent_t *me, memextent_basic_mapping_t *map, size_t offset,
		 size_t size)
{
	assert((me != NULL) && (map != NULL));
	assert((size > 0U) && (size <= me->size));
	assert(!util_add_overflows(me->phys_base, offset));
	assert(!util_add_overflows(map->vbase, offset));
	assert(!util_add_overflows(me->phys_base + offset, size - 1U));
	assert(!util_add_overflows(map->vbase + offset, size - 1U));

	addrspace_t *const s = atomic_load_relaxed(&map->addrspace);
	assert((s != NULL) && !s->read_only);

	return addrspace_map(
		s, map->vbase + offset, size, me->phys_base + offset,
		memextent_mapping_attrs_get_memtype(&map->attrs),
		memextent_mapping_attrs_get_kernel_access(&map->attrs),
		memextent_mapping_attrs_get_user_access(&map->attrs));
}

// Needs to be called holding a reference to the addrspace to be used
static void
memextent_remove_map_from_addrspace_list(memextent_basic_mapping_t *map)
{
	assert(map != NULL);

	addrspace_t *as = atomic_load_relaxed(&map->addrspace);
	assert(as != NULL);

	spinlock_acquire(&as->mapping_list_lock);
	(void)list_delete_node(&as->basic_mapping_list,
			       &map->mapping_list_node);
	spinlock_release(&as->mapping_list_lock);

	atomic_store_relaxed(&map->addrspace, NULL);
}

error_t
memextent_activate_basic(memextent_t *me)
{
	error_t	     ret;
	partition_t *hyp_partition = partition_get_private();

	assert(me != NULL);
	assert(hyp_partition != NULL);

	ret = allocate_mappings(me);
	if (ret != OK) {
		goto out;
	}

	if (me->device_mem) {
		assert(me->memtype == MEMEXTENT_MEMTYPE_DEVICE);

		ret = memdb_insert(hyp_partition, me->phys_base,
				   me->phys_base + (me->size - 1U),
				   (uintptr_t)me, MEMDB_TYPE_EXTENT);
	} else {
		partition_t *partition = me->header.partition;
		assert(partition != NULL);

		ret = memdb_update(hyp_partition, me->phys_base,
				   me->phys_base + (me->size - 1U),
				   (uintptr_t)me, MEMDB_TYPE_EXTENT,
				   (uintptr_t)partition, MEMDB_TYPE_PARTITION);

		if (ret == ERROR_MEMDB_NOT_OWNER) {
			// We might have failed to take ownership
			// because a previously deleted memextent has
			// not yet been cleaned up, so wait for an RCU
			// grace period and then retry. If it still
			// fails after that, there's a real conflict.
			rcu_sync();
			ret = memdb_update(hyp_partition, me->phys_base,
					   me->phys_base + (me->size - 1U),
					   (uintptr_t)me, MEMDB_TYPE_EXTENT,
					   (uintptr_t)partition,
					   MEMDB_TYPE_PARTITION);
		}
	}

	if (ret != OK) {
		free_mappings(me);
	}

out:
	return ret;
}

error_t
memextent_activate_derive_basic(memextent_t *me)
{
	error_t	     ret	   = OK;
	partition_t *hyp_partition = partition_get_private();

	assert(me != NULL);
	assert(me->parent != NULL);

	ret = allocate_mappings(me);
	if (ret != OK) {
		goto out;
	}

	bool retried = false;
	while (1) {
		spinlock_acquire(&me->parent->lock);

		if (me->parent->attached_size != 0U) {
			ret = ERROR_BUSY;
			goto out_locked_parent;
		}

		// Take the mapping lock before the memdb update, because we
		// haven't set up the mapping pointers yet. We do that after the
		// memdb update so we don't have to undo them if the memdb
		// update fails.
		spinlock_acquire_nopreempt(&me->lock);

		ret = memdb_update(hyp_partition, me->phys_base,
				   me->phys_base + (me->size - 1U),
				   (uintptr_t)me, MEMDB_TYPE_EXTENT,
				   (uintptr_t)me->parent, MEMDB_TYPE_EXTENT);
		if (ret == OK) {
			break;
		}
		if ((ret != ERROR_MEMDB_NOT_OWNER) || retried) {
			goto out_locked;
		}

		// We might have failed to take ownership because a previously
		// deleted memextent has not yet been cleaned up, so drop the
		// locks, wait for an RCU grace period, and then retry. If it
		// still fails after that, there's a real conflict.
		spinlock_release_nopreempt(&me->lock);
		spinlock_release(&me->parent->lock);
		rcu_sync();
		retried = true;
	}

	memextent_retain_mappings(me->parent);

	for (index_t i = 0U; (i < MEMEXTENT_MAX_MAPS); i++) {
		memextent_basic_mapping_t *map = &me->mappings.basic[i];

		memextent_mapping_t parent_map = memextent_lookup_mapping(
			me->parent, me->phys_base, me->size, i);
		if (parent_map.size != me->size) {
			// The parent is partially mapped over the child's
			// range; we cannot handle this with a basic memextent.
			ret = ERROR_DENIED;
			break;
		}

		addrspace_t *as = parent_map.addrspace;
		if (as == NULL) {
			continue;
		}

		atomic_store_relaxed(&map->addrspace, as);
		map->vbase = parent_map.vbase;
		map->attrs = parent_map.attrs;

		spinlock_acquire_nopreempt(&as->mapping_list_lock);
		list_insert_at_head(&as->basic_mapping_list,
				    &map->mapping_list_node);
		spinlock_release_nopreempt(&as->mapping_list_lock);

		pgtable_access_t access_user =
			memextent_mapping_attrs_get_user_access(&map->attrs);
		pgtable_access_t access_kernel =
			memextent_mapping_attrs_get_kernel_access(&map->attrs);

		// Reduce access rights on the map
		memextent_mapping_attrs_set_user_access(
			&map->attrs,
			pgtable_access_mask(access_user, me->access));
		memextent_mapping_attrs_set_kernel_access(
			&map->attrs,
			pgtable_access_mask(access_kernel, me->access));

		// If accesses are the same then mapping can be inherited from
		// parent, if not, remap memextent to update access.
		if (!memextent_mapping_attrs_is_equal(map->attrs,
						      parent_map.attrs)) {
			ret = memextent_do_map(me, map, 0, me->size);
			if (ret != OK) {
				memextent_remove_map_from_addrspace_list(map);
				break;
			}
		}
	}

	if (ret != OK) {
		// Revert any remappings that were made.
		error_t err;
		for (index_t i = 0; i < MEMEXTENT_MAX_MAPS; i++) {
			memextent_basic_mapping_t *map = &me->mappings.basic[i];

			addrspace_t *as = atomic_load_relaxed(&map->addrspace);
			if (as == NULL) {
				continue;
			}

			memextent_mapping_t parent_map =
				memextent_lookup_mapping(
					me->parent, me->phys_base, me->size, i);
			assert(as == parent_map.addrspace);

			if (!memextent_mapping_attrs_is_equal(
				    map->attrs, parent_map.attrs)) {
				map->attrs = parent_map.attrs;

				err = memextent_do_map(me, map, 0, me->size);
				assert(err == OK);
			}

			memextent_remove_map_from_addrspace_list(map);
		}

		// Revert the earlier memdb update.
		err = memdb_update(hyp_partition, me->phys_base,
				   me->phys_base + (me->size - 1U),
				   (uintptr_t)me->parent, MEMDB_TYPE_EXTENT,
				   (uintptr_t)me, MEMDB_TYPE_EXTENT);
		assert(err == OK);
	}

	memextent_release_mappings(me->parent, false);

	list_insert_at_head(&me->parent->children_list,
			    &me->children_list_node);

out_locked:
	spinlock_release_nopreempt(&me->lock);
out_locked_parent:
	spinlock_release(&me->parent->lock);

	if (ret != OK) {
		free_mappings(me);
	}

out:
	return ret;
}

// Needs to be called holding a reference to the addrspace to be used
static void
memextent_do_unmap(memextent_t *me, memextent_basic_mapping_t *map,
		   size_t offset, size_t size)
{
	assert((me != NULL) && (map != NULL));
	assert((size > 0U) && (size <= me->size));
	assert(!util_add_overflows(map->vbase, offset));
	assert(!util_add_overflows(map->vbase + offset, size - 1U));

	addrspace_t *const s = atomic_load_relaxed(&map->addrspace);
	assert((s != NULL) && !s->read_only);

	error_t err = addrspace_unmap(s, map->vbase + offset, size,
				      me->phys_base + offset);
	assert(err == OK);
}

static error_t
memextent_map_range(paddr_t base, size_t size, void *arg)
{
	error_t ret = OK;

	if ((size == 0U) || (util_add_overflows(base, size - 1))) {
		ret = ERROR_ARGUMENT_SIZE;
		goto error;
	}

	assert(arg != NULL);

	memextent_basic_arg_t *args = (memextent_basic_arg_t *)arg;

	assert((args->me != NULL) && (args->map[0] != NULL));

	size_t offset = base - args->me->phys_base;

	ret = memextent_do_map(args->me, args->map[0], offset, size);
	if (ret != OK) {
		args->failed_address = base;
	}

error:
	return ret;
}

static error_t
memextent_unmap_range(paddr_t base, size_t size, void *arg)
{
	error_t ret = OK;

	if ((size == 0U) || (util_add_overflows(base, size - 1))) {
		ret = ERROR_ARGUMENT_SIZE;
		goto error;
	}

	assert(arg != NULL);

	memextent_basic_arg_t *args = (memextent_basic_arg_t *)arg;

	assert((args->me != NULL) && (args->map[0] != NULL));

	size_t	offset = base - args->me->phys_base;
	index_t i      = 0;

	while ((args->map[i] != NULL) && (i < util_array_size(args->map))) {
		memextent_do_unmap(args->me, args->map[i], offset, size);
		i++;
	}

error:
	return ret;
}

error_t
memextent_map_basic(memextent_t *me, addrspace_t *addrspace, vmaddr_t vm_base,
		    memextent_mapping_attrs_t map_attrs)
{
	assert((me != NULL) && (addrspace != NULL));

	error_t ret	      = OK;
	bool	mappings_full = true;

	if (util_add_overflows(vm_base, me->size - 1U)) {
		ret = ERROR_ADDR_OVERFLOW;
		goto out;
	}

	spinlock_acquire(&me->lock);

	memextent_basic_mapping_t *map = NULL;
	for (index_t i = 0; i < MEMEXTENT_MAX_MAPS; i++) {
		map = &me->mappings.basic[i];

		// The mapping may have been used by a now deactivated
		// addrspace; use a load-acquire to ensure we observe the
		// removal from the addrspace's mapping list in
		// memextent_deactivate_addrspace_basic().
		if (atomic_load_acquire(&map->addrspace) == NULL) {
			mappings_full = false;
			break;
		}
	}

	if (mappings_full) {
		ret = ERROR_MEMEXTENT_MAPPINGS_FULL;
		goto out_locked;
	}

	pgtable_access_t access_user =
		memextent_mapping_attrs_get_user_access(&map_attrs);
	pgtable_access_t access_kernel =
		memextent_mapping_attrs_get_kernel_access(&map_attrs);
	pgtable_vm_memtype_t memtype =
		memextent_mapping_attrs_get_memtype(&map_attrs);

	// Add mapping to address space's list
	spinlock_acquire_nopreempt(&addrspace->mapping_list_lock);
	list_insert_at_head(&addrspace->basic_mapping_list,
			    &map->mapping_list_node);
	spinlock_release_nopreempt(&addrspace->mapping_list_lock);

	atomic_store_relaxed(&map->addrspace, addrspace);
	map->vbase = vm_base;

	memextent_mapping_attrs_set_memtype(&map->attrs, memtype);
	memextent_mapping_attrs_set_user_access(&map->attrs, access_user);
	memextent_mapping_attrs_set_kernel_access(&map->attrs, access_kernel);

	if (list_is_empty(&me->children_list)) {
		ret = memextent_do_map(me, map, 0, me->size);
		goto out_mapping_recorded;
	}

	memextent_basic_arg_t arg = { me, { map }, 0 };

	// Walk through the memory extent physical range and map the contiguous
	// ranges it owns.
	ret = memdb_range_walk((uintptr_t)me, MEMDB_TYPE_EXTENT, me->phys_base,
			       me->phys_base + (me->size - 1U),
			       memextent_map_range, (void *)&arg);

	// If a range failed to be mapped, we need to rollback and unmap the
	// ranges that have already been mapped
	if ((ret != OK) && (arg.failed_address != me->phys_base)) {
		(void)memdb_range_walk((uintptr_t)me, MEMDB_TYPE_EXTENT,
				       me->phys_base, arg.failed_address - 1U,
				       memextent_unmap_range, (void *)&arg);
	}

out_mapping_recorded:
	// If mapping failed, clear the map structure.
	if (ret != OK) {
		spinlock_acquire_nopreempt(&addrspace->mapping_list_lock);
		(void)list_delete_node(&addrspace->basic_mapping_list,
				       &map->mapping_list_node);
		spinlock_release_nopreempt(&addrspace->mapping_list_lock);
		atomic_store_relaxed(&map->addrspace, NULL);
	}
out_locked:
	spinlock_release(&me->lock);
out:
	return ret;
}

error_t
memextent_unmap_basic(memextent_t *me, addrspace_t *addrspace, vmaddr_t vm_base)
{
	assert((me != NULL) && (addrspace != NULL));

	error_t ret		     = OK;
	bool	addrspace_not_mapped = true;

	spinlock_acquire(&me->lock);

	memextent_basic_mapping_t *map = NULL;
	for (index_t i = 0; i < MEMEXTENT_MAX_MAPS; i++) {
		map = &me->mappings.basic[i];

		if ((atomic_load_relaxed(&map->addrspace) == addrspace) &&
		    (map->vbase == vm_base)) {
			addrspace_not_mapped = false;
			break;
		}
	}

	if (addrspace_not_mapped) {
		ret = ERROR_ADDR_INVALID;
		goto out;
	}

	if (list_is_empty(&me->children_list)) {
		memextent_do_unmap(me, map, 0, me->size);
	} else {
		memextent_basic_arg_t arg = { me, { map }, 0 };

		// Walk through the memory extent physical range and unmap the
		// contiguous ranges it owns.
		ret = memdb_range_walk((uintptr_t)me, MEMDB_TYPE_EXTENT,
				       me->phys_base,
				       me->phys_base + (me->size - 1U),
				       memextent_unmap_range, (void *)&arg);
	}

	assert(ret == OK);
	memextent_remove_map_from_addrspace_list(map);
out:
	spinlock_release(&me->lock);
	return ret;
}

bool
memextent_unmap_all_basic(memextent_t *me)
{
	assert(me != NULL);

	memextent_basic_arg_t arg   = { me, { NULL }, 0 };
	index_t		      index = 0;

	spinlock_acquire(&me->lock);

	// RCU protects ->addrspace
	rcu_read_start();
	for (index_t j = 0; j < MEMEXTENT_MAX_MAPS; j++) {
		memextent_basic_mapping_t *map = &me->mappings.basic[j];

		addrspace_t *addrspace = atomic_load_consume(&map->addrspace);
		if (addrspace != NULL) {
			// Take a reference to the address space to ensure that
			// we don't race with its destruction.
			if (!object_get_addrspace_safe(addrspace)) {
				continue;
			}

			if (list_is_empty(&me->children_list)) {
				memextent_do_unmap(me, map, 0, me->size);
				memextent_remove_map_from_addrspace_list(map);
				object_put_addrspace(addrspace);
			} else {
				arg.map[index] = map;
				index++;
			}
		}
	}
	rcu_read_finish();

	if (index != 0U) {
		assert(!list_is_empty(&me->children_list));

		// Walk through the memory extent physical range and unmap the
		// contiguous ranges it owns.
		error_t ret = memdb_range_walk((uintptr_t)me, MEMDB_TYPE_EXTENT,
					       me->phys_base,
					       me->phys_base + (me->size - 1U),
					       memextent_unmap_range,
					       (void *)&arg);
		assert(ret == OK);

		// Remove mapping from their corresponding address space's list
		for (index_t j = 0; j < index; j++) {
			memextent_basic_mapping_t *map = arg.map[j];
			assert(map != NULL);

			addrspace_t *as = atomic_load_relaxed(&map->addrspace);
			assert(as != NULL);

			memextent_remove_map_from_addrspace_list(map);
			object_put_addrspace(as);
		}
	}

	spinlock_release(&me->lock);

	return true;
}

error_t
memextent_update_access_basic(memextent_t *me, addrspace_t *addrspace,
			      vmaddr_t		       vm_base,
			      memextent_access_attrs_t access_attrs)
{
	assert((me != NULL) && (addrspace != NULL));

	error_t ret		     = OK;
	bool	addrspace_not_mapped = true;

	memextent_basic_mapping_t *map = NULL;

	spinlock_acquire(&me->lock);

	for (index_t j = 0; j < MEMEXTENT_MAX_MAPS; j++) {
		map = &me->mappings.basic[j];

		if ((atomic_load_relaxed(&map->addrspace) == addrspace) &&
		    (map->vbase == vm_base)) {
			addrspace_not_mapped = false;
			break;
		}
	}

	if (addrspace_not_mapped) {
		ret = ERROR_ADDR_INVALID;
		goto out;
	}

	memextent_mapping_attrs_t old_attrs = map->attrs;

	pgtable_access_t access_user =
		memextent_access_attrs_get_user_access(&access_attrs);
	pgtable_access_t access_kernel =
		memextent_access_attrs_get_kernel_access(&access_attrs);

	memextent_mapping_attrs_set_user_access(&map->attrs, access_user);
	memextent_mapping_attrs_set_kernel_access(&map->attrs, access_kernel);

	if (list_is_empty(&me->children_list)) {
		ret = memextent_do_map(me, map, 0, me->size);
		if (ret != OK) {
			// Restore the old mapping attributes.
			map->attrs = old_attrs;
		}
	} else {
		memextent_basic_arg_t arg = { me, { map }, 0 };

		// Walk through the memory extent physical range and remap the
		// contiguous ranges it owns with the new mapping attributes.
		ret = memdb_range_walk((uintptr_t)me, MEMDB_TYPE_EXTENT,
				       me->phys_base,
				       me->phys_base + (me->size - 1U),
				       memextent_map_range, (void *)&arg);

		// If a range failed to be remapped, we need to rollback and
		// remap the modified ranges with the original attributes.
		if (ret != OK) {
			map->attrs = old_attrs;
			if (arg.failed_address != me->phys_base) {
				(void)memdb_range_walk(
					(uintptr_t)me, MEMDB_TYPE_EXTENT,
					me->phys_base, arg.failed_address - 1U,
					memextent_map_range, (void *)&arg);
			}
		}
	}

out:
	spinlock_release(&me->lock);

	return ret;
}

bool
memextent_is_mapped_basic(memextent_t *me, addrspace_t *addrspace,
			  bool exclusive)
{
	bool ret = false;

	for (index_t i = 0; i < MEMEXTENT_MAX_MAPS; i++) {
		memextent_basic_mapping_t *map = &me->mappings.basic[i];

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

// Revert mappings of extent to the parent, assuming that the extent has no
// children.
static void
memextent_revert_mappings(memextent_t *me)
{
	assert((me != NULL) && (me->parent != NULL));

	memextent_t *parent = me->parent;

	memextent_mapping_t child_maps[MEMEXTENT_MAX_MAPS]  = { 0 };
	memextent_mapping_t parent_maps[MEMEXTENT_MAX_MAPS] = { 0 };

	spinlock_acquire(&parent->lock);
	spinlock_acquire_nopreempt(&me->lock);

	memextent_retain_mappings(me);
	memextent_retain_mappings(parent);

	for (index_t i = 0; i < MEMEXTENT_MAX_MAPS; i++) {
		child_maps[i] = memextent_lookup_mapping(me, me->phys_base,
							 me->size, i);
	}

	size_t offset = 0U;
	while (offset < me->size) {
		paddr_t phys = me->phys_base + offset;
		size_t	size = me->size - offset;

		bool child_match[MEMEXTENT_MAX_MAPS]  = { 0 };
		bool parent_match[MEMEXTENT_MAX_MAPS] = { 0 };

		for (index_t i = 0; i < MEMEXTENT_MAX_MAPS; i++) {
			parent_maps[i] =
				memextent_lookup_mapping(parent, phys, size, i);

			memextent_mapping_t *pmap = &parent_maps[i];

			// We only want to revert the range covered by the
			// parent's smallest mapping (or unmapped range).
			size = util_min(pmap->size, size);

			if (pmap->addrspace == NULL) {
				continue;
			}

			for (index_t j = 0; j < MEMEXTENT_MAX_MAPS; j++) {
				memextent_mapping_t *cmap = &child_maps[j];

				if ((cmap->addrspace == NULL) ||
				    (cmap->addrspace != pmap->addrspace)) {
					continue;
				}

				bool vbase_match = cmap->vbase == pmap->vbase;
				bool attrs_match =
					memextent_mapping_attrs_is_equal(
						cmap->attrs, pmap->attrs);

				// We only need to unmap the child's mapping if
				// the vbase does not match. If vbase matches
				// but attrs don't, applying the parent's
				// mapping will overwrite the child's.
				parent_match[i] = vbase_match && attrs_match;
				child_match[j]	= vbase_match;
			}
		}

		for (index_t i = 0; i < MEMEXTENT_MAX_MAPS; i++) {
			memextent_mapping_t *cmap = &child_maps[i];
			memextent_mapping_t *pmap = &parent_maps[i];

			if ((cmap->addrspace != NULL) && !child_match[i]) {
				error_t err = addrspace_unmap(cmap->addrspace,
							      cmap->vbase, size,
							      phys);
				assert(err == OK);
			}

			if ((pmap->addrspace != NULL) && !parent_match[i]) {
				pgtable_vm_memtype_t memtype =
					memextent_mapping_attrs_get_memtype(
						&pmap->attrs);
				pgtable_access_t kernel_access =
					memextent_mapping_attrs_get_kernel_access(
						&pmap->attrs);
				pgtable_access_t user_access =
					memextent_mapping_attrs_get_user_access(
						&pmap->attrs);

				error_t err = addrspace_map(pmap->addrspace,
							    pmap->vbase, size,
							    phys, memtype,
							    kernel_access,
							    user_access);
				if (err != OK) {
					panic("Failed revert to parent mapping");
				}
			}
		}

		offset += size;
	}

	memextent_release_mappings(parent, false);
	memextent_release_mappings(me, true);

	spinlock_release_nopreempt(&me->lock);
	spinlock_release(&parent->lock);
}

bool
memextent_deactivate_basic(memextent_t *me)
{
	assert(me != NULL);

	// There should be no children by this time
	assert(list_is_empty(&me->children_list));

	if (me->parent != NULL) {
		memextent_revert_mappings(me);
	} else {
		(void)memextent_unmap_all_basic(me);
	}

	return true;
}

bool
memextent_cleanup_basic(memextent_t *me)
{
	assert(me != NULL);

	if (!me->active) {
		// not active; we haven't claimed ownership of any memory
		goto out;
	}

	// release ownership of the range
	void	    *new_owner;
	memdb_type_t new_type;

	memextent_t *parent = me->parent;
	if (me->parent != NULL) {
		new_owner = parent;
		new_type  = MEMDB_TYPE_EXTENT;
	} else {
		new_owner = me->header.partition;
		new_type  = MEMDB_TYPE_PARTITION;
	}

	partition_t *hyp_partition = partition_get_private();

	error_t err = memdb_update(hyp_partition, me->phys_base,
				   me->phys_base + (me->size - 1U),
				   (uintptr_t)new_owner, new_type,
				   (uintptr_t)me, MEMDB_TYPE_EXTENT);
	assert(err == OK);

	// Remove extent from parent's children list
	if (parent != NULL) {
		spinlock_acquire(&parent->lock);
		(void)list_delete_node(&parent->children_list,
				       &me->children_list_node);
		spinlock_release(&parent->lock);
	}

	free_mappings(me);

out:
	return true;
}

bool
memextent_retain_mappings_basic(memextent_t *me)
{
	assert(me != NULL);

	// RCU protects ->addrspace
	rcu_read_start();
	for (index_t i = 0; i < MEMEXTENT_MAX_MAPS; i++) {
		memextent_basic_mapping_t *map = &me->mappings.basic[i];

		addrspace_t *as = atomic_load_consume(&map->addrspace);
		if ((as != NULL) && object_get_addrspace_safe(as)) {
			map->retained = true;
		}
	}
	rcu_read_finish();

	return true;
}

bool
memextent_release_mappings_basic(memextent_t *me, bool clear)
{
	assert(me != NULL);

	for (index_t i = 0; i < MEMEXTENT_MAX_MAPS; i++) {
		memextent_basic_mapping_t *map = &me->mappings.basic[i];

		if (!map->retained) {
			continue;
		}

		addrspace_t *as = atomic_load_relaxed(&map->addrspace);
		assert(as != NULL);

		if (clear) {
			memextent_remove_map_from_addrspace_list(map);
		}

		object_put_addrspace(as);
		map->retained = false;
	}

	return true;
}

memextent_mapping_result_t
memextent_lookup_mapping_basic(memextent_t *me, paddr_t phys, size_t size,
			       index_t i)
{
	assert(me != NULL);
	assert(i < MEMEXTENT_MAX_MAPS);
	assert((phys >= me->phys_base) &&
	       ((phys + (size - 1U)) <= (me->phys_base + (me->size - 1U))));

	memextent_mapping_t ret = {
		.size = size,
	};

	memextent_basic_mapping_t *map = &me->mappings.basic[i];

	if (map->retained) {
		addrspace_t *as = atomic_load_relaxed(&map->addrspace);
		assert(as != NULL);

		ret.addrspace = as;
		ret.vbase     = map->vbase + (phys - me->phys_base);
		ret.attrs     = map->attrs;
	}

	return memextent_mapping_result_ok(ret);
}

error_t
memextent_create_addrspace_basic(addrspace_create_t params)
{
	addrspace_t *addrspace = params.addrspace;
	assert(addrspace != NULL);

	list_init(&addrspace->basic_mapping_list);

	return OK;
}

error_t
memextent_attach_basic(memextent_t *me, uintptr_t hyp_va, size_t size,
		       pgtable_hyp_memtype_t memtype)
{
	error_t ret;

	assert(me != NULL);

	spinlock_acquire(&me->lock);

	if (!list_is_empty(&me->children_list)) {
		ret = ERROR_BUSY;
		goto out_locked;
	}

	pgtable_hyp_start();
	ret = pgtable_hyp_map(me->header.partition, hyp_va, size, me->phys_base,
			      memtype, PGTABLE_ACCESS_RW,
			      VMSA_SHAREABILITY_INNER_SHAREABLE);
	pgtable_hyp_commit();

	if (ret == OK) {
		me->attached_address = hyp_va;
		me->attached_size    = size;
	}

out_locked:
	spinlock_release(&me->lock);

	return ret;
}

bool
memextent_detach_basic(memextent_t *me)
{
	assert(me != NULL);

	spinlock_acquire(&me->lock);
	assert(me->attached_size != 0);

	pgtable_hyp_start();
	pgtable_hyp_unmap(me->header.partition, me->attached_address,
			  me->attached_size, me->attached_size);
	pgtable_hyp_commit();

	me->attached_size = 0;
	spinlock_release(&me->lock);

	return true;
}

void
memextent_deactivate_addrspace_basic(addrspace_t *addrspace)
{
	assert(addrspace != NULL);

	spinlock_acquire(&addrspace->mapping_list_lock);

	list_t *list = &addrspace->basic_mapping_list;

	// Remove all mappings from addrspace
	memextent_basic_mapping_t *map = NULL;
	list_foreach_container_maydelete (map, list, memextent_basic_mapping,
					  mapping_list_node) {
		(void)list_delete_node(list, &map->mapping_list_node);
		// We use a store-release to ensure that this list deletion is
		// observed before using this mapping for another addrspace in
		// memextent_map_basic().
		atomic_store_release(&map->addrspace, NULL);
	}

	spinlock_release(&addrspace->mapping_list_lock);
}

size_result_t
memextent_get_offset_for_pa_basic(memextent_t *me, paddr_t pa, size_t size)
{
	size_result_t ret;

	if (util_add_overflows(pa, size - 1U)) {
		ret = size_result_error(ERROR_ADDR_OVERFLOW);
	} else if ((pa < me->phys_base) ||
		   ((pa + size - 1U) > (me->phys_base + me->size - 1U))) {
		ret = size_result_error(ERROR_ADDR_INVALID);
	} else {
		ret = size_result_ok(pa - me->phys_base);
	}

	return ret;
}
