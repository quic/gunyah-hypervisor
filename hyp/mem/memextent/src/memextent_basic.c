// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

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

// Needs to be called holding a reference to the addrspace to be used
static error_t
memextent_do_map(memextent_t *me, memextent_mapping_t *map, size_t offset,
		 size_t size)
{
	assert((me != NULL) && (map != NULL));

	addrspace_t *const s = atomic_load_consume(&map->addrspace);
	assert((s != NULL) && !s->vm_read_only);

	spinlock_acquire(&s->pgtable_lock);

	assert((size > 0U) && (size <= me->size));
	assert(!util_add_overflows(me->phys_base, offset));
	assert(!util_add_overflows(map->vbase, offset));
	assert(!util_add_overflows(me->phys_base + offset, size - 1U));
	assert(!util_add_overflows(map->vbase + offset, size - 1U));

	pgtable_vm_start(&s->vm_pgtable);

	// We do not set the try_map option, as we want to do the mapping even
	// if the specified range has already been mapped
	error_t ret = pgtable_vm_map(
		s->header.partition, &s->vm_pgtable, map->vbase + offset, size,
		me->phys_base + offset,
		memextent_mapping_attrs_get_memtype(&map->attrs),
		memextent_mapping_attrs_get_kernel_access(&map->attrs),
		memextent_mapping_attrs_get_user_access(&map->attrs), false);

	pgtable_vm_commit(&s->vm_pgtable);

	if (ret != OK) {
		goto error;
	}

error:
	spinlock_release(&s->pgtable_lock);

	return ret;
}

error_t
memextent_activate_derive_basic(memextent_t *me)
{
	error_t ret = OK;

	assert(me != NULL);
	assert(me->parent != NULL);
	assert(me->parent->type == MEMEXTENT_TYPE_BASIC);

	bool retried = false;
	while (1) {
		spinlock_acquire(&me->parent->lock);

		// Take the mapping lock before the memdb update, because we
		// haven't set up the mapping pointers yet. We do that after the
		// memdb update so we don't have to undo them if the memdb
		// update fails.
		spinlock_acquire_nopreempt(&me->lock);

		partition_t *hyp_partition = partition_get_private();

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

	size_t offset = me->phys_base - me->parent->phys_base;

	for (index_t i = 0; i < util_array_size(me->mappings); i++) {
		memextent_mapping_t	    *map = &me->mappings[i];
		const memextent_mapping_t *parent_map =
			&me->parent->mappings[i];

		// RCU protects ->addrspace
		rcu_read_start();
		addrspace_t *as = atomic_load_consume(&parent_map->addrspace);
		if (as == NULL) {
			memset((void *)map, 0U, sizeof(map));
			rcu_read_finish();
			continue;
		}

		assert(!util_add_overflows(parent_map->vbase, offset));

		vmaddr_t vbase = parent_map->vbase + offset;

		assert(!util_add_overflows(vbase, me->size - 1U));

		// Take a reference to the address space to ensure that
		// we don't race with its destruction.
		if (!object_get_addrspace_safe(as)) {
			// Either there is no mapping, or the address space is
			// in the process of being deleted.
			memset((void *)map, 0U, sizeof(map));
			rcu_read_finish();
			continue;
		}
		rcu_read_finish();

		*map = *parent_map;

		map->vbase = vbase;

		spinlock_acquire_nopreempt(&as->mapping_list_lock);
		list_insert_at_head(&as->mapping_list, &map->mapping_list_node);
		spinlock_release_nopreempt(&as->mapping_list_lock);

		pgtable_access_t access_user =
			memextent_mapping_attrs_get_user_access(&map->attrs);
		pgtable_access_t access_kernel =
			memextent_mapping_attrs_get_kernel_access(&map->attrs);

		// Reduce access rights on the map
		memextent_mapping_attrs_set_user_access(
			&map->attrs, access_user & me->access);
		memextent_mapping_attrs_set_kernel_access(
			&map->attrs, access_kernel & me->access);

		// If accesses are the same then mapping can be inherited from
		// parent, if not, remap memextent to update access.
		if (memextent_mapping_attrs_raw(map->attrs) ==
		    memextent_mapping_attrs_raw(parent_map->attrs)) {
			object_put_addrspace(as);
			continue;
		}

		ret = memextent_do_map(me, map, 0, me->size);
		if (ret != OK) {
			panic("unhandled memextent remap failure");
		}

		object_put_addrspace(as);
	}

	list_insert_at_head(&me->parent->children_list,
			    &me->children_list_node);

out_locked:
	spinlock_release_nopreempt(&me->lock);
	spinlock_release(&me->parent->lock);

	return ret;
}

// Needs to be called holding a reference to the addrspace to be used
static void
memextent_do_unmap(memextent_t *me, memextent_mapping_t *map, size_t offset,
		   size_t size)
{
	assert((me != NULL) && (map != NULL));

	addrspace_t *const s = atomic_load_consume(&map->addrspace);
	assert((s != NULL) && !s->vm_read_only);

	spinlock_acquire(&s->pgtable_lock);

	assert((size > 0U) && (size <= me->size));
	assert(!util_add_overflows(map->vbase, offset));
	assert(!util_add_overflows(map->vbase + offset, size - 1U));

	pgtable_vm_start(&s->vm_pgtable);

	// Unmap only matching physical addresses
	pgtable_vm_unmap_matching(s->header.partition, &s->vm_pgtable,
				  map->vbase + offset, me->phys_base + offset,
				  size);

	pgtable_vm_commit(&s->vm_pgtable);

	spinlock_release(&s->pgtable_lock);
}

static error_t
memextent_map_range(paddr_t base, size_t size, void *arg)
{
	error_t ret = OK;

	if ((size == 0U) && (util_add_overflows(base, size - 1))) {
		ret = ERROR_ARGUMENT_SIZE;
		goto error;
	}

	assert(arg != NULL);

	memextent_arg_t *args = (memextent_arg_t *)arg;

	assert((args->me != NULL) && (args->map[0] != NULL));

	size_t offset = base - args->me->phys_base;
	ret	      = memextent_do_map(args->me, args->map[0], offset, size);

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

	if ((size == 0U) && (util_add_overflows(base, size - 1))) {
		ret = ERROR_ARGUMENT_SIZE;
		goto error;
	}

	assert(arg != NULL);

	memextent_arg_t *args = (memextent_arg_t *)arg;

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

	error_t ret = OK;

	if (util_add_overflows(vm_base, me->size - 1U)) {
		ret = ERROR_ADDR_OVERFLOW;
		goto out;
	}

	spinlock_acquire(&me->lock);

	bool		     mappings_full = true;
	memextent_mapping_t *map	   = NULL;
	for (index_t i = 0; i < util_array_size(me->mappings); i++) {
		map = &me->mappings[i];

		if (atomic_load_relaxed(&map->addrspace) == NULL) {
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

	// Take a reference to the address space to ensure that
	// we don't race with its destruction.
	if (!object_get_addrspace_safe(addrspace)) {
		ret = ERROR_OBJECT_STATE;
		goto out_locked;
	}

	// Add mapping to address space's list
	spinlock_acquire_nopreempt(&addrspace->mapping_list_lock);
	list_insert_at_head(&addrspace->mapping_list, &map->mapping_list_node);
	spinlock_release_nopreempt(&addrspace->mapping_list_lock);

	atomic_store_relaxed(&map->addrspace, addrspace);
	map->vbase = vm_base;

	memextent_mapping_attrs_set_memtype(&map->attrs, memtype);
	memextent_mapping_attrs_set_user_access(&map->attrs,
						access_user & me->access);
	memextent_mapping_attrs_set_kernel_access(&map->attrs,
						  access_kernel & me->access);

	if (list_is_empty(&me->children_list)) {
		ret = memextent_do_map(me, map, 0, me->size);
		goto out_mapping_recorded;
	}

	memextent_arg_t arg = { me, { NULL }, 0 };
	arg.map[0]	    = map;

	// Walk through the memory extent physical range and map the contiguous
	// ranges it owns.
	ret = memdb_range_walk((uintptr_t)me, MEMDB_TYPE_EXTENT, me->phys_base,
			       me->phys_base + (me->size - 1U),
			       memextent_map_range, (void *)&arg);

	// If a range failed to be mapped, we need to rollback and unmap the
	// ranges that have already been mapped
	if ((ret != OK) && (arg.failed_address != me->phys_base)) {
		memdb_range_walk((uintptr_t)me, MEMDB_TYPE_EXTENT,
				 me->phys_base, arg.failed_address - 1U,
				 memextent_unmap_range, (void *)&arg);
	}

out_mapping_recorded:
	// If mapping failed, clear the map structure.
	if (ret != OK) {
		spinlock_acquire_nopreempt(&addrspace->mapping_list_lock);
		list_delete_node(&addrspace->mapping_list,
				 &map->mapping_list_node);
		spinlock_release_nopreempt(&addrspace->mapping_list_lock);
		memset((void *)map, 0U, sizeof(map));
	}
	object_put_addrspace(addrspace);

out_locked:
	spinlock_release(&me->lock);
out:
	return ret;
}

// Needs to be called holding a reference to the addrspace to be used
static void
memextent_remove_map_from_addrspace_list(memextent_mapping_t **mapping)
{
	assert(*mapping != NULL);

	memextent_mapping_t *map = *mapping;
	addrspace_t	    *as	 = atomic_load_consume(&map->addrspace);

	assert(as != NULL);

	spinlock_acquire(&as->mapping_list_lock);
	list_delete_node(&as->mapping_list, &map->mapping_list_node);
	spinlock_release(&as->mapping_list_lock);

	memset((void *)map, 0U, sizeof(map));

	*mapping = map;
}

error_t
memextent_unmap_basic(memextent_t *me, addrspace_t *addrspace, vmaddr_t vm_base)
{
	assert((me != NULL) && (addrspace != NULL));

	error_t		     ret		  = OK;
	bool		     addrspace_not_mapped = true;
	memextent_mapping_t *map		  = NULL;

	spinlock_acquire(&me->lock);

	for (index_t i = 0; i < util_array_size(me->mappings); i++) {
		map = &me->mappings[i];

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

	// Take a reference to the address space to ensure that
	// we don't race with its destruction.
	if (!object_get_addrspace_safe(addrspace)) {
		ret = ERROR_OBJECT_STATE;
		goto out;
	}

	if (list_is_empty(&me->children_list)) {
		memextent_do_unmap(me, map, 0, me->size);
	} else {
		memextent_arg_t arg = { me, { NULL }, 0 };
		arg.map[0]	    = map;

		// Walk through the memory extent physical range and unmap the
		// contiguous ranges it owns.
		ret = memdb_range_walk((uintptr_t)me, MEMDB_TYPE_EXTENT,
				       me->phys_base,
				       me->phys_base + (me->size - 1U),
				       memextent_unmap_range, (void *)&arg);
	}

	assert(ret == OK);
	memextent_remove_map_from_addrspace_list(&map);
	object_put_addrspace(addrspace);

out:
	spinlock_release(&me->lock);
	return ret;
}

bool
memextent_unmap_all_basic(memextent_t *me)
{
	assert(me != NULL);

	memextent_arg_t arg   = { NULL, { NULL }, 0 };
	index_t		index = 0;

	spinlock_acquire(&me->lock);

	// RCU protects ->addrspace
	rcu_read_start();
	for (index_t j = 0; j < util_array_size(me->mappings); j++) {
		addrspace_t *addrspace =
			atomic_load_consume(&me->mappings[j].addrspace);
		if (addrspace != NULL) {
			// Take a reference to the address space to ensure that
			// we don't race with its destruction.
			if (!object_get_addrspace_safe(addrspace)) {
				continue;
			}

			if (list_is_empty(&me->children_list)) {
				memextent_do_unmap(me, &me->mappings[j], 0,
						   me->size);
				object_put_addrspace(addrspace);
				continue;
			} else {
				arg.map[index] = &me->mappings[j];
				index++;
			}
		}
	}
	rcu_read_finish();

	if (list_is_empty(&me->children_list)) {
		goto out;
	}

	if (index != 0U) {
		arg.me = me;

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
			memextent_mapping_t *map = arg.map[j];
			memextent_remove_map_from_addrspace_list(&map);

			object_put_addrspace(
				atomic_load_consume(&map->addrspace));
		}
	}

out:
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

	memextent_mapping_t *map = NULL;

	spinlock_acquire(&me->lock);

	for (index_t j = 0; j < util_array_size(me->mappings); j++) {
		map = &me->mappings[j];

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

	// Take a reference to the address space to ensure that
	// we don't race with its destruction.
	if (!object_get_addrspace_safe(addrspace)) {
		ret = ERROR_OBJECT_STATE;
		goto out;
	}

	pgtable_access_t access_user =
		memextent_access_attrs_get_user_access(&access_attrs);
	pgtable_access_t access_kernel =
		memextent_access_attrs_get_kernel_access(&access_attrs);

	memextent_mapping_attrs_set_user_access(&map->attrs,
						access_user & me->access);
	memextent_mapping_attrs_set_kernel_access(&map->attrs,
						  access_kernel & me->access);

	ret = memextent_do_map(me, map, 0, me->size);

	object_put_addrspace(addrspace);

out:
	spinlock_release(&me->lock);

	return ret;
}

// Revert mappings of extent to the parent, assuming that the extent has no
// children.
static void
memextent_revert_mappings(memextent_t *me)
{
	assert((me != NULL) && (me->parent != NULL));

	memextent_t *parent = me->parent;

	size_t offset = me->phys_base - parent->phys_base;

	spinlock_acquire(&parent->lock);

	BITMAP_DECLARE(util_array_size(parent->mappings),
		       parent_matched) = { 0 };

	for (index_t j = 0; j < util_array_size(me->mappings); j++) {
		// RCU protects ->addrspace
		rcu_read_start();
		memextent_mapping_t *map = &me->mappings[j];
		addrspace_t	    *s	 = atomic_load_consume(&map->addrspace);

		// Take a reference to the address space to ensure that
		// we don't race with its destruction.
		if ((s == NULL) || !object_get_addrspace_safe(s)) {
			// Nothing to do.
			rcu_read_finish();
			continue;
		}
		rcu_read_finish();

		bool matched = false;

		for (index_t i = 0; i < util_array_size(parent->mappings);
		     i++) {
			memextent_mapping_t *p_map = &parent->mappings[i];

			if (atomic_load_relaxed(&p_map->addrspace) != s) {
				continue;
			}

			paddr_t p_vbase = p_map->vbase + offset;
			if (p_vbase == map->vbase) {
				bitmap_set(parent_matched, i);
				matched = true;

				// Revert attributes if they have changed.
				if (memextent_mapping_attrs_raw(p_map->attrs) !=
				    memextent_mapping_attrs_raw(map->attrs)) {
					memextent_do_map(parent, p_map, offset,
							 me->size);
				}

				memextent_remove_map_from_addrspace_list(&map);
			}
		}

		if (!matched) {
			// The parent does not have this mapping; remove it.
			memextent_do_unmap(me, map, 0, me->size);
			memextent_remove_map_from_addrspace_list(&map);
		}

		object_put_addrspace(s);
	}

	BITMAP_FOREACH_CLEAR_BEGIN(i, parent_matched,
				   util_array_size(parent->mappings))
		// RCU protects ->addrspace
		rcu_read_start();
		memextent_mapping_t *p_map = &parent->mappings[i];
		addrspace_t *addrspace = atomic_load_consume(&p_map->addrspace);
		if ((addrspace == NULL) ||
		    (!object_get_addrspace_safe(addrspace))) {
			rcu_read_finish();
			continue;
		}
		rcu_read_finish();

		// Revert the mapping
		memextent_do_map(parent, p_map, offset, me->size);

		object_put_addrspace(addrspace);
	BITMAP_FOREACH_CLEAR_END

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
		memextent_unmap_all_basic(me);
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
	void	     *new_owner;
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
		list_delete_node(&parent->children_list,
				 &me->children_list_node);
		spinlock_release(&parent->lock);
	}

out:
	return true;
}
