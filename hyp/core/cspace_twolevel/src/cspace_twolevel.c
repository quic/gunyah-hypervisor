// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <stdatomic.h>
#include <string.h>

#include <hypcontainers.h>
#include <hyprights.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <cspace.h>
#include <list.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <prng.h>
#include <rcu.h>
#include <refcount.h>
#include <spinlock.h>
#include <thread.h>
#include <util.h>

#include "cspace_object.h"
#include "event_handlers.h"

#define CSPACE_MAX_CAP_COUNT_SUPPORTED                                         \
	(CAP_TABLE_NUM_CAP_SLOTS * CSPACE_NUM_CAP_TABLES)

static_assert(sizeof(cap_data_t) == 16U, "Cap data must be 16 bytes");
static_assert(atomic_is_lock_free((cap_data_t *)NULL),
	      "Cap data is not lock free");
static_assert(sizeof(cap_t) == 32U, "Cap must be 32 bytes");
static_assert(sizeof(cap_table_t) == CAP_TABLE_ALLOC_SIZE,
	      "Cap table not sized correctly");
static_assert(_Alignof(cap_table_t) == CAP_TABLE_ALLOC_SIZE,
	      "Cap table not aligned correctly");
static_assert(sizeof(cspace_t) == CSPACE_ALLOC_SIZE,
	      "Cspace not sized correctly");

cspace_t *
cspace_get_self(void)
{
	return thread_get_self()->cspace_cspace;
}

static cap_table_t *
cspace_get_cap_table(const cap_t *cap)
{
	return (cap_table_t *)util_balign_down((uintptr_t)cap,
					       sizeof(cap_table_t));
}

static index_t
cspace_get_cap_slot_index(const cap_table_t *table, const cap_t *cap)
{
	ptrdiff_t index = cap - &table->cap_slots[0];

	assert((index >= 0U) && (index < (ptrdiff_t)CAP_TABLE_NUM_CAP_SLOTS));
	assert(cap == &table->cap_slots[index]);

	return (index_t)index;
}

// VM visible cap-IDs are randomized. The encode and decode operations turn an
// internally linear cspace index and apply a random base and index multiply.
// This ensures that for each cspace the cap-IDs are unique and randomized on
// each boot.
//
// Currently only a 16-bit random multiplier is used. A larger 64-bit
// multiplier would be better, however that will require 128-bit multiplies and
// a more complex algorithm to find the inverse.

static error_t
cspace_init_id_encoder(cspace_t *cspace)
{
	error_t err;
#if !defined(DISABLE_CSPACE_RAND) || !DISABLE_CSPACE_RAND
	uint64_result_t rand_base, rand_mult;

	// Generate randomized ID space

	// We need to preserve the Cap-Id space of 0xffffffff.xxxxxxxx
	// for special capability values.  Invalid cap is -1 for example.
	// We pick a rand_base that won't allow such id ranges to be generated.
	do {
		rand_base = prng_get64();
		err	  = rand_base.e;
		if (err != OK) {
			goto out;
		}
	} while ((rand_base.r >> 32) >= 0xffffff00U);

	rand_mult = prng_get64();
	err	  = rand_mult.e;
	if (err == OK) {
		cspace->id_rand_base = rand_base.r;
		// Pick a non-zero random 16-bit number
		while ((rand_mult.r & 0xffffU) == 0U) {
			rand_mult.r = ((uint64_t)0x5555U << 48U) |
				      (rand_mult.r >> 16U);
		}
		// Calculate a 16-bit random multiplier and its inverse
		cspace->id_mult = rand_mult.r & 0xffffU;
		cspace->id_inv = (((uint64_t)1U << 32U) / cspace->id_mult) + 1U;
	}

out:
#else
	cspace->id_rand_base = 0U;
	cspace->id_mult	     = 1U;
	cspace->id_inv	     = ((uint64_t)1U << 32U) + 1U;
	err		     = OK;
#endif
	return err;
}

static cap_id_t
cspace_encode_cap_id(const cspace_t *cspace, cap_value_t val)
{
	uint64_t v = (uint64_t)cap_value_raw(val);

	return (cap_id_t)((v * cspace->id_mult) ^ cspace->id_rand_base);
}

static cap_value_result_t
cspace_decode_cap_id(const cspace_t *cspace, cap_id_t id)
{
	cap_value_result_t ret;
	uint64_t	   r = id ^ cspace->id_rand_base;
	uint64_t	   v = (r * cspace->id_inv) >> 32U;

	if (compiler_expected((r == (uint64_t)(uint32_t)r) &&
			      (v == (uint64_t)(uint16_t)v))) {
		ret = cap_value_result_ok(cap_value_cast((uint16_t)v));
	} else {
		ret = cap_value_result_error(ERROR_ARGUMENT_INVALID);
	}

	return ret;
}

static error_t
cspace_cap_id_to_indices(const cspace_t *cspace, cap_id_t cap_id,
			 index_t *upper, index_t *lower)
{
	error_t		   err;
	cap_value_result_t ret = cspace_decode_cap_id(cspace, cap_id);

	if (compiler_expected(ret.e == OK)) {
		*lower = cap_value_get_lower_index(&ret.r);
		*upper = cap_value_get_upper_index(&ret.r);
		if (compiler_expected((*upper < CSPACE_NUM_CAP_TABLES) &&
				      (*lower < CAP_TABLE_NUM_CAP_SLOTS))) {
			err = OK;
		} else {
			err = ERROR_ARGUMENT_INVALID;
		}
	} else {
		err = ret.e;
	}

	return err;
}

static cap_id_t
cspace_indices_to_cap_id(const cspace_t *cspace, index_t upper, index_t lower)
{
	cap_value_t val = cap_value_default();

	cap_value_set_lower_index(&val, lower);
	cap_value_set_upper_index(&val, upper);

	return cspace_encode_cap_id(cspace, val);
}

static error_t
cspace_check_cap_data(cap_data_t data, object_type_t type, cap_rights_t rights)
{
	error_t	      err;
	object_type_t obj_type	    = cap_info_get_type(&data.info);
	cap_state_t   state	    = cap_info_get_state(&data.info);
	cap_rights_t  masked_rights = data.rights & rights;

	if (compiler_expected(state == CAP_STATE_VALID)) {
		if (compiler_expected(obj_type == type) ||
		    (type == OBJECT_TYPE_ANY)) {
			err = OK;
		} else {
			err = ERROR_CSPACE_WRONG_OBJECT_TYPE;
			goto out;
		}
	} else if (state == CAP_STATE_NULL) {
		err = ERROR_CSPACE_CAP_NULL;
		goto out;
	} else if (state == CAP_STATE_REVOKED) {
		err = ERROR_CSPACE_CAP_REVOKED;
		goto out;
	} else {
		panic("invalid cap state");
	}

	if (compiler_unexpected(masked_rights != rights)) {
		err = ERROR_CSPACE_INSUFFICIENT_RIGHTS;
	}
out:
	return err;
}

// Update the cap data for the given cap. Will only succeed if cap hasn't been
// modified since it was last read. As such, this function can also be used to
// check if a cap is unchanged after a previous read.
static error_t
cspace_update_cap_slot(cap_t *cap, cap_data_t *expected_data,
		       cap_data_t new_data)
{
	bool success = atomic_compare_exchange_strong_explicit(
		&cap->data, expected_data, new_data, memory_order_relaxed,
		memory_order_relaxed);

	return success ? OK : ERROR_BUSY;
}

static error_t
cspace_lookup_cap_slot(const cspace_t *cspace, cap_id_t cap_id, cap_t **cap)
{
	error_t	     err;
	index_t	     upper_index, lower_index;
	cap_table_t *table;

	err = cspace_cap_id_to_indices(cspace, cap_id, &upper_index,
				       &lower_index);
	if (compiler_expected(err == OK)) {
		table = atomic_load_consume(&cspace->tables[upper_index]);
		if (compiler_expected(table != NULL)) {
			*cap = &table->cap_slots[lower_index];
			err  = OK;
		} else {
			err = ERROR_CSPACE_CAP_NULL;
		}
	}

	return err;
}

static error_t
cspace_allocate_cap_table(cspace_t *cspace, cap_table_t **table,
			  index_t *upper_index)
{
	error_t		  err;
	void_ptr_result_t ret;
	index_t		  index;
	cap_table_t	    *new_table;
	partition_t	    *partition = cspace->header.partition;

	do {
		if (!bitmap_atomic_ffc(cspace->allocated_tables,
				       CSPACE_NUM_CAP_TABLES, &index)) {
			err = ERROR_CSPACE_FULL;
			goto allocate_cap_table_error;
		}
		// Loop until we successfully change bit state.
	} while (bitmap_atomic_test_and_set(cspace->allocated_tables, index,
					    memory_order_relaxed));

	ret = partition_alloc(partition, sizeof(cap_table_t),
			      alignof(cap_table_t));
	if (ret.e != OK) {
		(void)bitmap_atomic_test_and_clear(cspace->allocated_tables,
						   index, memory_order_relaxed);
		err = ERROR_NOMEM;
		goto allocate_cap_table_error;
	}

	memset(ret.r, 0, sizeof(cap_table_t));
	new_table = (cap_table_t *)ret.r;

	new_table->partition = object_get_partition_additional(partition);
	new_table->cspace    = cspace;
	new_table->index     = index;

	*table	     = new_table;
	*upper_index = index;
	err	     = OK;

allocate_cap_table_error:
	return err;
}

rcu_update_status_t
cspace_destroy_cap_table(rcu_entry_t *entry)
{
	index_t		    index;
	cap_table_t	    *table     = cap_table_container_of_rcu_entry(entry);
	partition_t	    *partition = table->partition;
	rcu_update_status_t ret	      = rcu_update_status_default();

	// If called via cspace destroy, there may still
	// be valid caps which also require destruction.
	for (; table->cap_count > 0U; table->cap_count--) {
		cap_t	      *cap;
		cap_data_t	 data;
		object_type_t	 type;
		object_header_t *header;
		bool		 cap_list_empty;

		if (compiler_unexpected(!bitmap_atomic_ffs(
			    table->used_slots, CAP_TABLE_NUM_CAP_SLOTS,
			    &index))) {
			panic("cap table has incorrect cap_count on delete");
		}

		cap  = &table->cap_slots[index];
		data = atomic_load_relaxed(&cap->data);

		bitmap_atomic_clear(table->used_slots, index,
				    memory_order_relaxed);

		if (cap_info_get_state(&data.info) != CAP_STATE_VALID) {
			continue;
		}

		type   = cap_info_get_type(&data.info);
		header = object_get_header(type, data.object);
		spinlock_acquire(&header->cap_list_lock);
		list_delete_node(&header->cap_list, &cap->cap_list_node);
		cap_list_empty = list_is_empty(&header->cap_list);
		spinlock_release(&header->cap_list_lock);

		if (cap_list_empty) {
			object_put(type, data.object);
		}
	}

	partition_free(partition, table, sizeof(cap_table_t));
	object_put_partition(partition);

	return ret;
}

static error_t
cspace_allocate_cap_slot(cspace_t *cspace, cap_t **cap, cap_id_t *cap_id)
{
	error_t	     err;
	cap_table_t *table;
	index_t	     upper_index, lower_index;

	spinlock_acquire(&cspace->cap_allocation_lock);

	if (cspace->cap_count == cspace->max_caps) {
		spinlock_release(&cspace->cap_allocation_lock);
		err = ERROR_CSPACE_FULL;
		goto allocate_cap_slot_error;
	}

	if (bitmap_ffs(cspace->available_tables, CSPACE_NUM_CAP_TABLES,
		       &upper_index)) {
		table = atomic_load_relaxed(&cspace->tables[upper_index]);
	} else {
		// Allocation may require preemption, so release the lock.
		spinlock_release(&cspace->cap_allocation_lock);
		rcu_read_finish();
		err = cspace_allocate_cap_table(cspace, &table, &upper_index);
		rcu_read_start();
		if (err != OK) {
			goto allocate_cap_slot_error;
		}
		// Re-acquire lock and attach table.
		spinlock_acquire(&cspace->cap_allocation_lock);
		// Store with release, as table initialisation
		// must be ordered before table attachment.
		atomic_store_release(&cspace->tables[upper_index], table);
		bitmap_set(cspace->available_tables, upper_index);
	}

	table->cap_count++;
	cspace->cap_count++;

	if (table->cap_count == CAP_TABLE_NUM_CAP_SLOTS) {
		bitmap_clear(cspace->available_tables, upper_index);
	}

	spinlock_release(&cspace->cap_allocation_lock);

	do {
		if (compiler_unexpected(!bitmap_atomic_ffc(
			    table->used_slots, CAP_TABLE_NUM_CAP_SLOTS,
			    &lower_index))) {
			panic("cap table has incorrect cap_count on allocate");
		}
		// Loop until we successfully change bit state.
	} while (bitmap_atomic_test_and_set(table->used_slots, lower_index,
					    memory_order_relaxed));

	*cap	= &table->cap_slots[lower_index];
	*cap_id = cspace_indices_to_cap_id(cspace, upper_index, lower_index);
	err	= OK;

allocate_cap_slot_error:
	return err;
}

// Assumes cap data is already set to null
static void
cspace_free_cap_slot(cspace_t *cspace, cap_t *cap)
{
	cap_table_t *table;
	index_t	     upper_index, lower_index;

	table	    = cspace_get_cap_table(cap);
	lower_index = cspace_get_cap_slot_index(table, cap);
	upper_index = table->index;

	(void)bitmap_atomic_test_and_clear(table->used_slots, lower_index,
					   memory_order_relaxed);

	spinlock_acquire(&cspace->cap_allocation_lock);

	if (table->cap_count == CAP_TABLE_NUM_CAP_SLOTS) {
		bitmap_set(cspace->available_tables, upper_index);
	}

	table->cap_count--;
	cspace->cap_count--;

	if (table->cap_count == 0U) {
		(void)bitmap_atomic_test_and_clear(cspace->allocated_tables,
						   upper_index,
						   memory_order_relaxed);
		bitmap_clear(cspace->available_tables, upper_index);
		atomic_store_relaxed(&cspace->tables[upper_index], NULL);
		rcu_enqueue(&table->rcu_entry,
			    RCU_UPDATE_CLASS_CSPACE_RELEASE_LEVEL);
	}

	spinlock_release(&cspace->cap_allocation_lock);
}

object_ptr_result_t
cspace_lookup_object(cspace_t *cspace, cap_id_t cap_id, object_type_t type,
		     cap_rights_t rights, bool active_only)
{
	error_t		    err;
	cap_t	      *cap;
	cap_data_t	    cap_data;
	object_ptr_result_t ret;

	assert(type != OBJECT_TYPE_ANY);

	rcu_read_start();

	err = cspace_lookup_cap_slot(cspace, cap_id, &cap);
	if (compiler_unexpected(err != OK)) {
		ret = object_ptr_result_error(err);
		goto lookup_object_error;
	}

	cap_data = atomic_load_consume(&cap->data);
	err	 = cspace_check_cap_data(cap_data, type, rights);
	if (compiler_unexpected(err != OK)) {
		ret = object_ptr_result_error(err);
		goto lookup_object_error;
	}
	if (active_only) {
		object_state_t obj_state = atomic_load_acquire(
			&object_get_header(type, cap_data.object)->state);
		if (compiler_unexpected(obj_state != OBJECT_STATE_ACTIVE)) {
			ret = object_ptr_result_error(ERROR_OBJECT_STATE);
			goto lookup_object_error;
		}
	}
	if (compiler_unexpected(!object_get_safe(type, cap_data.object))) {
		ret = object_ptr_result_error(ERROR_CSPACE_CAP_NULL);
		goto lookup_object_error;
	}
	ret = object_ptr_result_ok(cap_data.object);

lookup_object_error:
	rcu_read_finish();

	return ret;
}

object_ptr_result_t
cspace_lookup_object_any(cspace_t *cspace, cap_id_t cap_id,
			 cap_rights_generic_t rights, object_type_t *type)
{
	error_t		    err;
	cap_t	      *cap;
	cap_data_t	    cap_data;
	object_ptr_result_t ret;
	object_type_t	    obj_type = OBJECT_TYPE_ANY;

	assert(type != NULL);
	// Only valid generic object rights may be specified
	assert((~cap_rights_generic_raw(CAP_RIGHTS_GENERIC_ALL) &
		cap_rights_generic_raw(rights)) == 0U);

	rcu_read_start();

	err = cspace_lookup_cap_slot(cspace, cap_id, &cap);
	if (compiler_unexpected(err != OK)) {
		ret = object_ptr_result_error(err);
		goto lookup_object_error;
	}

	cap_data = atomic_load_consume(&cap->data);
	obj_type = cap_info_get_type(&cap_data.info);
	err	 = cspace_check_cap_data(cap_data, OBJECT_TYPE_ANY,
					 cap_rights_generic_raw(rights));
	if (compiler_unexpected(err != OK)) {
		ret = object_ptr_result_error(err);
		goto lookup_object_error;
	}
	if (compiler_unexpected(!object_get_safe(obj_type, cap_data.object))) {
		ret = object_ptr_result_error(ERROR_CSPACE_CAP_NULL);
		goto lookup_object_error;
	}
	ret = object_ptr_result_ok(cap_data.object);

lookup_object_error:
	*type = obj_type;
	rcu_read_finish();

	return ret;
}

error_t
cspace_twolevel_handle_object_create_cspace(cspace_create_t cspace_create)
{
	error_t	  err;
	cspace_t *cspace = cspace_create.cspace;

	// The cspace has been zeroed on allocation,
	// so just initialise non-zero fields.
	spinlock_init(&cspace->cap_allocation_lock);
	spinlock_init(&cspace->revoked_cap_list_lock);
	list_init(&cspace->revoked_cap_list);
	err = cspace_init_id_encoder(cspace);

	return err;
}

error_t
cspace_configure(cspace_t *cspace, count_t max_caps)
{
	error_t err;

	assert(atomic_load_relaxed(&cspace->header.state) == OBJECT_STATE_INIT);

	if (max_caps <= CSPACE_MAX_CAP_COUNT_SUPPORTED) {
		cspace->max_caps = max_caps;
		err		 = OK;
	} else {
		err = ERROR_ARGUMENT_INVALID;
	}

	return err;
}

error_t
cspace_twolevel_handle_object_activate_cspace(cspace_t *cspace)
{
	if (cspace->max_caps != 0) {
		return OK;
	} else {
		return ERROR_OBJECT_CONFIG;
	}
}

void
cspace_twolevel_handle_object_cleanup_cspace(cspace_t *cspace)
{
	// Ensure all lower levels destroyed
	for (index_t i = 0U; i < CSPACE_NUM_CAP_TABLES; i++) {
		cap_table_t *table = atomic_load_relaxed(&cspace->tables[i]);
		if (table != NULL) {
			(void)cspace_destroy_cap_table(&table->rcu_entry);
		}
	}
}

cap_id_result_t
cspace_create_master_cap(cspace_t *cspace, object_ptr_t object,
			 object_type_t type)
{
	error_t		err;
	cap_t	      *new_cap;
	cap_data_t	cap_data;
	cap_id_t	new_cap_id;
	cap_id_result_t ret;

	assert(type != OBJECT_TYPE_ANY);

	// Objects are initialized with a refcount of 1 which is for the master
	// cap reference here.
	cap_data.object = object;
	cap_data.rights = cspace_get_rights_all(type);

	cap_info_init(&cap_data.info);
	cap_info_set_master_cap(&cap_data.info, true);
	cap_info_set_type(&cap_data.info, type);
	cap_info_set_state(&cap_data.info, CAP_STATE_VALID);

	rcu_read_start();

	err = cspace_allocate_cap_slot(cspace, &new_cap, &new_cap_id);
	if (err == OK) {
		object_header_t *header = object_get_header(type, object);
		// No need to hold cap list lock prior to cap being available
		// in the cspace. Instead, store cap data with release to
		// ensure object & cap list initialisation is ordered-before.
		list_insert_at_head(&header->cap_list, &new_cap->cap_list_node);
		atomic_store_release(&new_cap->data, cap_data);
		ret = cap_id_result_ok(new_cap_id);
	} else {
		ret = cap_id_result_error(err);
	}

	rcu_read_finish();

	return ret;
}

cap_id_result_t
cspace_copy_cap(cspace_t *target_cspace, cspace_t *parent_cspace,
		cap_id_t parent_id, cap_rights_t rights_mask)
{
	error_t		 err;
	cap_t	      *new_cap, *parent_cap;
	cap_data_t	 cap_data;
	cap_id_t	 new_cap_id;
	object_header_t *header;
	cap_id_result_t	 ret;

	rcu_read_start();

	// We try to allocate the cap slot first, as we may need to
	// be preempted if allocating a cap table is required.
	err = cspace_allocate_cap_slot(target_cspace, &new_cap, &new_cap_id);
	if (err != OK) {
		ret = cap_id_result_error(err);
		goto copy_cap_error;
	}

	err = cspace_lookup_cap_slot(parent_cspace, parent_id, &parent_cap);
	if (err != OK) {
		cspace_free_cap_slot(target_cspace, new_cap);
		ret = cap_id_result_error(err);
		goto copy_cap_error;
	}

	cap_data = atomic_load_consume(&parent_cap->data);

	err = cspace_check_cap_data(cap_data, OBJECT_TYPE_ANY, 0U);
	if (err != OK) {
		cspace_free_cap_slot(target_cspace, new_cap);
		ret = cap_id_result_error(err);
		goto copy_cap_error;
	}
	cap_rights_t masked_rights = cap_data.rights & rights_mask;
	if (masked_rights == 0U) {
		cspace_free_cap_slot(target_cspace, new_cap);
		ret = cap_id_result_error(ERROR_CSPACE_INSUFFICIENT_RIGHTS);
		goto copy_cap_error;
	}

	header = object_get_header(cap_info_get_type(&cap_data.info),
				   cap_data.object);
	spinlock_acquire(&header->cap_list_lock);

	// Reload the parent cap data for the new cap and
	// ensure it has not changed.
	err = cspace_update_cap_slot(parent_cap, &cap_data, cap_data);
	if (err == OK) {
		// Reuse parent cap data with updated rights.
		cap_data.rights = masked_rights;
		// Ensure this is not created as the master cap
		cap_info_set_master_cap(&cap_data.info, false);
		atomic_store_relaxed(&new_cap->data, cap_data);
		list_insert_after_node(&header->cap_list,
				       &parent_cap->cap_list_node,
				       &new_cap->cap_list_node);
	}

	spinlock_release(&header->cap_list_lock);

	if (err == OK) {
		ret = cap_id_result_ok(new_cap_id);
	} else {
		cspace_free_cap_slot(target_cspace, new_cap);
		ret = cap_id_result_error(err);
	}

copy_cap_error:
	rcu_read_finish();
	return ret;
}

error_t
cspace_delete_cap(cspace_t *cspace, cap_id_t cap_id)
{
	error_t	      err;
	cap_t	      *cap;
	cap_data_t    cap_data, null_cap_data = { 0 };
	cap_state_t   state;
	object_type_t type;
	object_ptr_t  object;
	bool	      cap_list_empty = false;

	rcu_read_start();

	err = cspace_lookup_cap_slot(cspace, cap_id, &cap);
	if (err != OK) {
		goto delete_cap_error;
	}

	cap_data = atomic_load_consume(&cap->data);
	state	 = cap_info_get_state(&cap_data.info);
	type	 = cap_info_get_type(&cap_data.info);
	object	 = cap_data.object;

	if (state == CAP_STATE_VALID) {
		object_header_t *header = object_get_header(type, object);
		spinlock_acquire(&header->cap_list_lock);

		err = cspace_update_cap_slot(cap, &cap_data, null_cap_data);
		if (err == OK) {
			list_delete_node(&header->cap_list,
					 &cap->cap_list_node);
			cap_list_empty = list_is_empty(&header->cap_list);
		}

		spinlock_release(&header->cap_list_lock);
	} else if (state == CAP_STATE_REVOKED) {
		spinlock_acquire(&cspace->revoked_cap_list_lock);

		err = cspace_update_cap_slot(cap, &cap_data, null_cap_data);
		if (err == OK) {
			list_delete_node(&cspace->revoked_cap_list,
					 &cap->cap_list_node);
		}

		spinlock_release(&cspace->revoked_cap_list_lock);
	} else {
		err = ERROR_CSPACE_CAP_NULL;
	}

	if (err == OK) {
		cspace_free_cap_slot(cspace, cap);
		if (cap_list_empty) {
			object_put(type, object);
		}
	}

delete_cap_error:
	rcu_read_finish();
	return err;
}

error_t
cspace_revoke_caps(cspace_t *cspace, cap_id_t master_cap_id)
{
	error_t		 err;
	cap_t	      *master_cap;
	cap_data_t	 master_cap_data;
	object_header_t *header;

	rcu_read_start();

	err = cspace_lookup_cap_slot(cspace, master_cap_id, &master_cap);
	if (err != OK) {
		goto revoke_caps_error;
	}

	master_cap_data = atomic_load_consume(&master_cap->data);
	err = cspace_check_cap_data(master_cap_data, OBJECT_TYPE_ANY, 0U);
	if (err != OK) {
		goto revoke_caps_error;
	}
	if (!cap_info_get_master_cap(&master_cap_data.info)) {
		err = ERROR_CSPACE_INSUFFICIENT_RIGHTS;
		goto revoke_caps_error;
	}

	header = object_get_header(cap_info_get_type(&master_cap_data.info),
				   master_cap_data.object);
	spinlock_acquire(&header->cap_list_lock);

	// Perform a no-op update on the master cap. If this fails,
	// the master cap data has changed.
	err = cspace_update_cap_slot(master_cap, &master_cap_data,
				     master_cap_data);
	if (err != OK) {
		spinlock_release(&header->cap_list_lock);
		goto revoke_caps_error;
	}

	// Child caps are always inserted after the parent, so the
	// master cap will be at the head of the object cap list.
	list_t *list = &header->cap_list;
	assert(list_get_head(list) == &master_cap->cap_list_node);

	cap_t *curr_cap = NULL;

	list_foreach_container_maydelete (curr_cap, list, cap, cap_list_node) {
		if (curr_cap == master_cap) {
			continue;
		}

		cap_data_t curr_cap_data = atomic_load_relaxed(&curr_cap->data);

		cap_info_set_state(&curr_cap_data.info, CAP_STATE_REVOKED);

		// Clear the object this cap points to, since the object could
		// be deleted by deleting the last valid cap, revoked caps
		// pointing to freed memory would make debugging confusing.
		memset(&curr_cap_data.object, 0, sizeof(curr_cap_data.object));

		// It is safe to get the child cap's cspace, as the child
		// cap must be destroyed before the cspace can be, and
		// this cannot happen while we have the cap list lock.
		cspace_t *curr_cspace = cspace_get_cap_table(curr_cap)->cspace;
		spinlock_acquire_nopreempt(&curr_cspace->revoked_cap_list_lock);

		// Child cap data won't change while we hold the locks,
		// so just atomically store the invalid data.
		atomic_store_relaxed(&curr_cap->data, curr_cap_data);
		list_delete_node(&header->cap_list, &curr_cap->cap_list_node);
		list_insert_at_head(&curr_cspace->revoked_cap_list,
				    &curr_cap->cap_list_node);
		spinlock_release_nopreempt(&curr_cspace->revoked_cap_list_lock);
	}

	spinlock_release(&header->cap_list_lock);

revoke_caps_error:
	rcu_read_finish();
	return err;
}

error_t
cspace_attach_thread(cspace_t *cspace, thread_t *thread)
{
	assert(thread != NULL);
	assert(cspace != NULL);
	assert(atomic_load_relaxed(&cspace->header.state) ==
	       OBJECT_STATE_ACTIVE);
	assert(atomic_load_relaxed(&thread->header.state) == OBJECT_STATE_INIT);

	if (thread->cspace_cspace != NULL) {
		object_put_cspace(thread->cspace_cspace);
	}

	thread->cspace_cspace = object_get_cspace_additional(cspace);

	return OK;
}

void
cspace_twolevel_handle_object_deactivate_thread(thread_t *thread)
{
	assert(thread != NULL);

	cspace_t *cspace = thread->cspace_cspace;

	if (cspace != NULL) {
		object_put_cspace(thread->cspace_cspace);
		thread->cspace_cspace = NULL;
	}
}
