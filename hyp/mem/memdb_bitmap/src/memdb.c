// © 2023 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <bitmap.h>
#include <bootmem.h>
#include <compiler.h>
#include <log.h>
#include <memdb.h>
#include <panic.h>
#include <partition.h>
#include <rcu.h>
#include <spinlock.h>
#include <trace.h>
#include <util.h>

#include "event_handlers.h"

static spinlock_t memdb_lock;

static_assert((uint64_t)MEMDB_TYPE_NOTYPE == 0U,
	      "Zero-initialised memdb entries must be empty");
static memdb_level_table_t memdb_root;

extern const char image_phys_start;
extern const char image_phys_last;

// We rely on the bitmap extract and insert operations being atomic, which is
// only possible if the field never spans two machine words. This is the case if
// the field's size is a power of two, or if the whole bitmap fits in one word.
//
// The AArch64 LDP & CASP instructions could be used to atomically access two
// adjacent words if FEAT_LSE2 is implemented, but there is no easy way to make
// use of that from platform-independent C code, and it doesn't work on older
// ARMv8 hardware without FEAT_LSE2.
static_assert(util_is_p2(MEMDB_BITMAP_ID_BITS) ||
		      (MEMDB_BITMAP_SIZE < BITMAP_WORD_BITS),
	      "Bitmap extract & insert must be atomic");

void
memdb_bitmap_handle_boot_cold_init(void)
{
	partition_t *hyp_partition = partition_get_private();
	assert(hyp_partition != NULL);

	spinlock_init(&memdb_lock);

	// Assign the hypervisor's ELF image to the private partition.
	error_t err = memdb_insert(hyp_partition, (paddr_t)&image_phys_start,
				   (paddr_t)&image_phys_last,
				   (uintptr_t)hyp_partition,
				   MEMDB_TYPE_PARTITION);
	if (err != OK) {
		panic("Error adding boot memory to hyp_partition");
	}

	// Obtain the initial bootmem range and change its ownership to the
	// hypervisor's allocator. We assume here that no other memory has been
	// assigned to any allocators yet.
	size_t bootmem_size	 = 0U;
	void  *bootmem_virt_base = bootmem_get_region(&bootmem_size);
	assert((bootmem_size != 0U) && (bootmem_virt_base != NULL));
	paddr_t bootmem_phys_base = partition_virt_to_phys(
		hyp_partition, (uintptr_t)bootmem_virt_base);
	assert(!util_add_overflows(bootmem_phys_base, bootmem_size - 1U));

	// Update ownership of the hypervisor partition's allocator memory
	err = memdb_update(hyp_partition, bootmem_phys_base,
			   bootmem_phys_base + (bootmem_size - 1U),
			   (uintptr_t)&hyp_partition->allocator,
			   MEMDB_TYPE_ALLOCATOR, (uintptr_t)hyp_partition,
			   MEMDB_TYPE_PARTITION);
	if (err != OK) {
		panic("Error updating bootmem allocator memory");
	}
}

static error_t
memdb_range_check(paddr_t start_addr, paddr_t end_addr)
{
	error_t err;

	if (start_addr >= end_addr) {
		err = ERROR_ARGUMENT_INVALID;
	} else if (end_addr >= util_bit(MEMDB_MAX_BITS)) {
		err = ERROR_ARGUMENT_SIZE;
	} else if (!util_is_p2aligned(start_addr, MEMDB_MIN_BITS) ||
		   !util_is_p2aligned(end_addr + 1U, MEMDB_MIN_BITS)) {
		err = ERROR_ARGUMENT_ALIGNMENT;
	} else {
		err = OK;
	}

	return err;
}

static void
memdb_release_level_table(memdb_level_table_t *table)
{
	rcu_enqueue(&table->rcu_entry,
		    RCU_UPDATE_CLASS_MEMDB_RELEASE_LEVEL_TABLE);
}

rcu_update_status_t
memdb_bitmap_free_level_table(rcu_entry_t *entry)
{
	rcu_update_status_t ret		  = rcu_update_status_default();
	partition_t	   *hyp_partition = partition_get_private();

	memdb_level_table_t *table =
		memdb_level_table_container_of_rcu_entry(entry);

	error_t err = partition_free(hyp_partition, table, sizeof(*table));
	if (err != OK) {
		panic(__func__);
	}

	return ret;
}

static void
memdb_release_level_bitmap(memdb_level_bitmap_t *bitmap)
{
	rcu_enqueue(&bitmap->rcu_entry,
		    RCU_UPDATE_CLASS_MEMDB_RELEASE_LEVEL_BITMAP);
}

rcu_update_status_t
memdb_bitmap_free_level_bitmap(rcu_entry_t *entry)
{
	rcu_update_status_t ret		  = rcu_update_status_default();
	partition_t	   *hyp_partition = partition_get_private();

	memdb_level_bitmap_t *bitmap =
		memdb_level_bitmap_container_of_rcu_entry(entry);

	error_t err = partition_free(hyp_partition, bitmap, sizeof(*bitmap));
	if (err != OK) {
		panic(__func__);
	}

	return ret;
}

static memdb_entry_t
memdb_entry_for_object(uintptr_t object, memdb_type_t obj_type)
{
	memdb_entry_t entry = memdb_entry_default();
	memdb_entry_set_entry_ptr(&entry, object);
	memdb_entry_set_entry_type(&entry, obj_type);
	return entry;
}

static memdb_level_bitmap_ptr_result_t
memdb_create_bitmap(memdb_entry_t initial_entry)
{
	memdb_level_bitmap_ptr_result_t ret;
	partition_t		       *hyp_partition = partition_get_private();

	void_ptr_result_t alloc_ret =
		partition_alloc(hyp_partition, sizeof(memdb_level_bitmap_t),
				alignof(memdb_level_bitmap_t));
	if (alloc_ret.e != OK) {
		ret = memdb_level_bitmap_ptr_result_error(alloc_ret.e);
		goto out;
	}
	ret = memdb_level_bitmap_ptr_result_ok(
		(memdb_level_bitmap_t *)alloc_ret.r);

	static_assert(MEMDB_NUM_ENTRIES <
			      util_bit(8U * sizeof(memdb_bitmap_count_t)),
		      "memdb_bitmap_count_t is too small");
	*ret.r = (memdb_level_bitmap_t){
		.objects = { [0] = initial_entry, },
		.counts = { [0] = (memdb_bitmap_count_t)MEMDB_NUM_ENTRIES, },
	};

out:
	return ret;
}

static memdb_level_table_ptr_result_t
memdb_create_table(memdb_entry_t initial_entry)
{
	memdb_level_table_ptr_result_t ret;
	partition_t		      *hyp_partition = partition_get_private();

	void_ptr_result_t alloc_ret =
		partition_alloc(hyp_partition, sizeof(memdb_level_table_t),
				alignof(memdb_level_table_t));
	if (alloc_ret.e != OK) {
		ret = memdb_level_table_ptr_result_error(alloc_ret.e);
		goto out;
	}
	ret = memdb_level_table_ptr_result_ok(
		(memdb_level_table_t *)alloc_ret.r);

	// Here we encounter an ugly quirk of C11 atomics: The compiler will not
	// let us use a plain { 0 } to zero-initialise this structure because
	// its first member is an array of atomic structs, and 0 is not a legal
	// initialiser for an atomic struct.
	*ret.r = (memdb_level_table_t){
		.rcu_entry = 0,
	};

	// Fill all of the entries with the initial entry value.
	for (index_t i = 0U; i < util_array_size(ret.r->entries); i++) {
		atomic_init(&ret.r->entries[i], initial_entry);
	}

out:
	return ret;
}

static memdb_level_table_ptr_result_t
memdb_convert_bitmap(memdb_level_bitmap_t *bitmap) REQUIRE_LOCK(memdb_lock)
{
	memdb_level_table_ptr_result_t ret =
		memdb_create_table(memdb_entry_default());
	if (ret.e != OK) {
		goto out;
	}

	memdb_entry_t objects[MEMDB_BITMAP_OBJECTS] = { 0 };
	for (index_t i = 0U; i < util_array_size(objects); i++) {
		objects[i] = atomic_load_relaxed(&bitmap->objects[i]);
	}

	for (index_t i = 0U; i < MEMDB_NUM_ENTRIES; i++) {
		const index_t cur_id = (index_t)bitmap_atomic_extract(
			bitmap->bitmap, i * MEMDB_BITMAP_ID_BITS,
			MEMDB_BITMAP_ID_BITS, memory_order_relaxed);
		atomic_init(&ret.r->entries[i], objects[cur_id]);
	}

out:
	return ret;
}

static memdb_level_bitmap_ptr_result_t
memdb_duplicate_bitmap(memdb_level_bitmap_t *bitmap) REQUIRE_LOCK(memdb_lock)
{
	memdb_level_bitmap_ptr_result_t ret;
	partition_t		       *hyp_partition = partition_get_private();

	void_ptr_result_t alloc_ret =
		partition_alloc(hyp_partition, sizeof(memdb_level_bitmap_t),
				alignof(memdb_level_bitmap_t));
	if (alloc_ret.e != OK) {
		ret = memdb_level_bitmap_ptr_result_error(alloc_ret.e);
		goto out;
	}
	ret = memdb_level_bitmap_ptr_result_ok(
		(memdb_level_bitmap_t *)alloc_ret.r);

	*ret.r = (memdb_level_bitmap_t){ 0U };

	for (index_t i = 0U; i < util_array_size(bitmap->objects); i++) {
		// Copy only the objects with nonzero counts, so that the other
		// IDs can be allocated to new objects.
		if (bitmap->counts[i] != 0U) {
			atomic_init(&ret.r->objects[i],
				    atomic_load_relaxed(&bitmap->objects[i]));
			ret.r->counts[i] = bitmap->counts[i];
		}
	}

	for (index_t i = 0U; i < util_array_size(bitmap->bitmap); i++) {
		atomic_init(&ret.r->bitmap[i],
			    atomic_load_relaxed(&bitmap->bitmap[i]));
	}

out:
	return ret;
}

static index_t
memdb_entry_index(paddr_t addr, index_t entry_bits)
{
	return (index_t)((addr >> entry_bits) &
			 util_mask(MEMDB_BITS_PER_LEVEL));
}

static index_result_t
memdb_update_bitmap_check_owner(index_t start_index, index_t end_index,
				memdb_entry_t		    old_entry,
				const memdb_level_bitmap_t *bitmap)
{
	index_result_t ret;

	index_t old_id;
	for (old_id = 0U; old_id < MEMDB_BITMAP_OBJECTS; old_id++) {
		if ((bitmap->counts[old_id] != 0U) &&
		    memdb_entry_is_equal(
			    atomic_load_relaxed(&bitmap->objects[old_id]),
			    old_entry)) {
			break;
		}
	}
	if (old_id == MEMDB_BITMAP_OBJECTS) {
		// Old entry isn't present anywhere in this bitmap
		ret = index_result_error(ERROR_MEMDB_NOT_OWNER);
		goto out;
	}

	// TODO: This could be optimised by using splat, xor and CLZ to find
	// contiguous ranges.
	for (index_t i = start_index; i <= end_index; i++) {
		const index_t cur_id = (index_t)bitmap_atomic_extract(
			bitmap->bitmap, i * MEMDB_BITMAP_ID_BITS,
			MEMDB_BITMAP_ID_BITS, memory_order_relaxed);

		if (cur_id != old_id) {
			ret = index_result_error(ERROR_MEMDB_NOT_OWNER);
			goto out;
		}
	}

	ret = index_result_ok(old_id);
out:
	return ret;
}

// Returns true if the bitmap's entries are all identical, so it can be
// collapsed into a single entry. Returns:
// - ERROR_MEMDB_NOT_OWNER if the update is invalid. In this case, the update
//   must be rolled back and the error returned to the caller.
// - ERROR_BUSY if the bitmap's object IDs are all in use, so it would need to
//   be converted to a table to perform the requested update.
// - ERROR_RETRY if the bitmap's object IDs have all been allocated, but one
//   of them has a usage count of 0, so the update needs RCU synchronisation.
// - ERROR_ARGUMENT_ALIGNMENT if the start or end address is within the range
//   represented by a field in the bitmap, so the bitmap must be converted to a
//   table to allow a next-level table to be created.
static bool_result_t
memdb_update_bitmap(paddr_t start, paddr_t end, memdb_entry_t old_entry,
		    memdb_entry_t new_entry, memdb_level_bitmap_t *bitmap,
		    index_t entry_bits) REQUIRE_LOCK(memdb_lock)
{
	bool_result_t ret;

	assert((entry_bits <= MEMDB_ROOT_ENTRY_BITS) &&
	       ((start >> (entry_bits + MEMDB_BITS_PER_LEVEL)) ==
		(end >> (entry_bits + MEMDB_BITS_PER_LEVEL))));

	const index_t start_index  = memdb_entry_index(start, entry_bits);
	const index_t end_index	   = memdb_entry_index(end, entry_bits);
	const count_t changed_bits = end_index - start_index + 1U;

	// We do all of the ownership checks before making any changes. There
	// are two reasons for this: first, we avoid having to implement
	// rollback; second, it prevents triggering bitmap to table conversion
	// by returning ERROR_BUSY for an update that would fail anyway.
	const index_result_t old_id_r = memdb_update_bitmap_check_owner(
		start_index, end_index, old_entry, bitmap);
	if (old_id_r.e != OK) {
		ret = bool_result_error(old_id_r.e);
		goto out;
	}
	const index_t old_id = old_id_r.r;

	// At this point we know that the update will succeed; now we need to
	// determine whether it can be represented by the bitmap. First, check
	// that the address range is exactly equal to the bit range.
	if (!util_is_p2aligned(start, entry_bits) ||
	    !util_is_p2aligned(end + 1U, entry_bits)) {
		ret = bool_result_error(ERROR_ARGUMENT_ALIGNMENT);
		goto out;
	}

	// Try to find an existing ID for the new entry.
	index_t new_id;
	for (new_id = 0U; new_id < MEMDB_BITMAP_OBJECTS; new_id++) {
		if (memdb_entry_is_equal(
			    atomic_load_relaxed(&bitmap->objects[new_id]),
			    new_entry)) {
			break;
		}
	}

	if (new_id == MEMDB_BITMAP_OBJECTS) {
		bool should_retry = false;

		// No existing ID. Try to find an ID that can be claimed.
		for (new_id = 0U; new_id < MEMDB_BITMAP_OBJECTS; new_id++) {
			if (bitmap->counts[new_id] != 0U) {
				// ID is in use already.
				continue;
			}

			if (memdb_entry_is_equal(
				    atomic_load_relaxed(
					    &bitmap->objects[new_id]),
				    memdb_entry_default())) {
				// Entry has never been used; we can claim it.
				break;
			}

			// Found an ID that was previously used, but we can't
			// safely recycle it in place because concurrent RCU
			// readers might have seen its ID in the bitmap. If
			// this is the only free ID, we can ask the caller to
			// update a clone of the bitmap.
			should_retry = true;
		}

		if (new_id == MEMDB_BITMAP_OBJECTS) {
			// No available IDs.
			ret = bool_result_error(should_retry ? ERROR_RETRY
							     : ERROR_BUSY);
			goto out;
		}
	}

	// Update the bitmap and the entry table.
	assert((new_id != old_id) && (new_id < MEMDB_BITMAP_OBJECTS));
	for (index_t i = start_index; i <= end_index; i++) {
		bitmap_atomic_insert(bitmap->bitmap, i * MEMDB_BITMAP_ID_BITS,
				     MEMDB_BITMAP_ID_BITS, new_id,
				     memory_order_relaxed);
	}

	atomic_store_release(&bitmap->objects[new_id], new_entry);
	bitmap->counts[new_id] =
		(memdb_bitmap_count_t)(bitmap->counts[new_id] + changed_bits);
	bitmap->counts[old_id] =
		(memdb_bitmap_count_t)(bitmap->counts[old_id] - changed_bits);

	// If the new ID's count is now equal to the total number of entries,
	// the bitmap has become contiguous and can be pruned.
	// TODO: We could also do this by splatting the ID and comparing it
	// to the whole bitmap; it would be a bit slower, but then we wouldn't
	// need the counts and could save some more space.
	ret = bool_result_ok(bitmap->counts[new_id] == MEMDB_NUM_ENTRIES);

out:
	return ret;
}

static error_t
memdb_update_table_entry(paddr_t start, paddr_t end, memdb_entry_t old_entry,
			 memdb_entry_t new_entry, memdb_level_table_t *table,
			 index_t entry_bits, index_t entry_index)
	REQUIRE_LOCK(memdb_lock);

static bool
memdb_update_table_check_contig(index_t start_index, index_t end_index,
				memdb_entry_t		   new_entry,
				const memdb_level_table_t *table)
{
	// Determine whether new_entry is now completely filling the table. We
	// need to check the start and end slots because the update might have
	// put next-level entries there, but anything between them is
	// necessarily already equal to new_entry so we can skip those.
	bool is_contig = true;

	for (index_t i = 0; is_contig && (i <= start_index); i++) {
		const memdb_entry_t cur_entry =
			atomic_load_consume(&table->entries[i]);
		if (!memdb_entry_is_equal(cur_entry, new_entry)) {
			is_contig = false;
		}
	}

	for (index_t i = end_index; is_contig && (i < MEMDB_NUM_ENTRIES); i++) {
		const memdb_entry_t cur_entry =
			atomic_load_consume(&table->entries[i]);
		if (!memdb_entry_is_equal(cur_entry, new_entry)) {
			is_contig = false;
		}
	}

	return is_contig;
}

// Returns true if the table's entries are all identical, so it can be collapsed
// into a single entry.
static bool_result_t
memdb_update_table(paddr_t start, paddr_t end, memdb_entry_t old_entry,
		   memdb_entry_t new_entry, memdb_level_table_t *table,
		   index_t entry_bits) REQUIRE_LOCK(memdb_lock)
{
	assert((entry_bits <= MEMDB_ROOT_ENTRY_BITS) &&
	       ((start >> (entry_bits + MEMDB_BITS_PER_LEVEL)) ==
		(end >> (entry_bits + MEMDB_BITS_PER_LEVEL))));

	// Work on one entry at a time. If a failure occurs, we start rolling
	// back the changes.
	const index_t start_index = memdb_entry_index(start, entry_bits);
	const index_t end_index	  = memdb_entry_index(end, entry_bits);
	paddr_t	      table_start = start &
			      ~util_mask(entry_bits + MEMDB_BITS_PER_LEVEL);

	error_t err = OK;
	index_t update_index;
	for (update_index = start_index; update_index <= end_index;
	     update_index++) {
		paddr_t entry_start =
			util_max(start, table_start + ((paddr_t)update_index *
						       util_bit(entry_bits)));
		paddr_t entry_end = util_min(
			end, table_start + ((((paddr_t)update_index + 1U) *
					     util_bit(entry_bits)) -
					    1U));
		err = memdb_update_table_entry(entry_start, entry_end,
					       old_entry, new_entry, table,
					       entry_bits, update_index);
		if (err != OK) {
			break;
		}
	}

	if (err != OK) {
		index_t rollback_index;
		for (rollback_index = start_index;
		     rollback_index < update_index; rollback_index++) {
			paddr_t entry_start = util_max(
				start, table_start + ((paddr_t)rollback_index *
						      util_bit(entry_bits)));
			paddr_t entry_end = util_min(
				end,
				table_start + ((((paddr_t)rollback_index + 1U) *
						util_bit(entry_bits)) -
					       1U));
			error_t rollback_err = memdb_update_table_entry(
				entry_start, entry_end, new_entry, old_entry,
				table, entry_bits, rollback_index);
			if (rollback_err != OK) {
				panic("memdb_update_table: rollback failure");
			}
		}
	}

	bool_result_t ret;
	if (err == OK) {
		ret = bool_result_ok(memdb_update_table_check_contig(
			start_index, end_index, new_entry, table));
	} else {
		ret = bool_result_error(err);
	}
	return ret;
}

static error_t
memdb_update_table_entry_level_table(
	paddr_t start, paddr_t end, memdb_entry_t old_entry,
	memdb_entry_t new_entry, memdb_level_table_t *table, index_t entry_bits,
	memdb_entry_t cur_entry, index_t entry_index) REQUIRE_LOCK(memdb_lock)
{
	uintptr_t	     cur_ptr	= memdb_entry_get_entry_ptr(&cur_entry);
	memdb_level_table_t *next_table = (memdb_level_table_t *)cur_ptr;
	bool_result_t	     is_contig_ret =
		memdb_update_table(start, end, old_entry, new_entry, next_table,
				   entry_bits - MEMDB_BITS_PER_LEVEL);
	error_t err = is_contig_ret.e;

	if ((err == OK) && is_contig_ret.r) {
		// Next level has become contiguous and is no longer
		// needed. Replace the entry with the new entry and
		// release the table.
		atomic_store_release(&table->entries[entry_index], new_entry);
		memdb_release_level_table(next_table);
	}

	return err;
}

static error_t
memdb_update_table_entry_level_bitmap(
	paddr_t start, paddr_t end, memdb_entry_t old_entry,
	memdb_entry_t new_entry, memdb_level_table_t *table, index_t entry_bits,
	memdb_entry_t cur_entry, index_t entry_index) REQUIRE_LOCK(memdb_lock)
{
	uintptr_t	      cur_ptr = memdb_entry_get_entry_ptr(&cur_entry);
	memdb_level_bitmap_t *next_bitmap   = (memdb_level_bitmap_t *)cur_ptr;
	bool_result_t	      is_contig_ret = memdb_update_bitmap(
		start, end, old_entry, new_entry, next_bitmap,
		entry_bits - MEMDB_BITS_PER_LEVEL);
	error_t err = is_contig_ret.e;

	if ((err == ERROR_BUSY) || (err == ERROR_ARGUMENT_ALIGNMENT)) {
		// Requested update is not possible with a bitmap. We
		// must convert it to a table and try again.
		memdb_level_table_ptr_result_t table_ret =
			memdb_convert_bitmap(next_bitmap);
		err = table_ret.e;
		if (err == OK) {
			is_contig_ret = memdb_update_table(
				start, end, old_entry, new_entry, table_ret.r,
				entry_bits - MEMDB_BITS_PER_LEVEL);
			if ((err == OK) && !is_contig_ret.r) {
				memdb_entry_t table_entry =
					memdb_entry_default();
				memdb_entry_set_entry_type(
					&table_entry, MEMDB_TYPE_LEVEL_TABLE);
				memdb_entry_set_entry_ptr(
					&table_entry, (uintptr_t)table_ret.r);
				atomic_store_release(
					&table->entries[entry_index],
					table_entry);
				memdb_release_level_bitmap(next_bitmap);
			} else {
				// Update failed, or succeeded and made
				// the table contiguous. The table is
				// no longer needed.
				memdb_release_level_table(table_ret.r);
			}
		}
	} else if (err == ERROR_RETRY) {
		// Requested update needs to be done on a copy of the bitmap
		// to avoid recycling object IDs.
		memdb_level_bitmap_ptr_result_t new_bitmap_ret =
			memdb_duplicate_bitmap(next_bitmap);
		err = new_bitmap_ret.e;
		if (err == OK) {
			is_contig_ret = memdb_update_bitmap(
				start, end, old_entry, new_entry,
				new_bitmap_ret.r,
				entry_bits - MEMDB_BITS_PER_LEVEL);
			if ((err == OK) && !is_contig_ret.r) {
				memdb_entry_t bitmap_entry =
					memdb_entry_default();
				memdb_entry_set_entry_type(
					&bitmap_entry, MEMDB_TYPE_LEVEL_BITMAP);
				memdb_entry_set_entry_ptr(
					&bitmap_entry,
					(uintptr_t)new_bitmap_ret.r);
				atomic_store_release(
					&table->entries[entry_index],
					bitmap_entry);
				memdb_release_level_bitmap(next_bitmap);
			} else {
				// Update failed, or succeeded and made
				// the table contiguous. The new bitmap is
				// no longer needed.
				memdb_release_level_bitmap(new_bitmap_ret.r);
			}
		}
	} else {
		// No special action needed for other errors.
	}

	if ((err == OK) && is_contig_ret.r) {
		// Next level has become contiguous and is no longer
		// needed. Replace the entry with the new entry and
		// release the bitmap.
		atomic_store_release(&table->entries[entry_index], new_entry);
		memdb_release_level_bitmap(next_bitmap);
	}

	return err;
}

static error_t
memdb_update_table_entry_split_bitmap(paddr_t start, paddr_t end,
				      memdb_entry_t	   old_entry,
				      memdb_entry_t	   new_entry,
				      memdb_level_table_t *table,
				      index_t entry_bits, index_t entry_index)
	REQUIRE_LOCK(memdb_lock)
{
	memdb_level_bitmap_ptr_result_t bitmap_ret =
		memdb_create_bitmap(old_entry);
	error_t err = bitmap_ret.e;

	if (compiler_expected(err == OK)) {
		bool_result_t is_contig_r = memdb_update_bitmap(
			start, end, old_entry, new_entry, bitmap_ret.r,
			entry_bits - MEMDB_BITS_PER_LEVEL);
		err = is_contig_r.e;

		if (compiler_expected(err == OK)) {
			assert(!is_contig_r.r);
			memdb_entry_t bitmap_entry = memdb_entry_default();
			memdb_entry_set_entry_type(&bitmap_entry,
						   MEMDB_TYPE_LEVEL_BITMAP);
			memdb_entry_set_entry_ptr(&bitmap_entry,
						  (uintptr_t)bitmap_ret.r);
			atomic_store_release(&table->entries[entry_index],
					     bitmap_entry);
		} else {
			// Update failed. The bitmap is no longer needed.
			memdb_release_level_bitmap(bitmap_ret.r);
		}
	}

	return err;
}

static error_t
memdb_update_table_entry_split_table(paddr_t start, paddr_t end,
				     memdb_entry_t	  old_entry,
				     memdb_entry_t	  new_entry,
				     memdb_level_table_t *table,
				     index_t entry_bits, index_t entry_index)
	REQUIRE_LOCK(memdb_lock)
{
	memdb_level_table_ptr_result_t table_ret =
		memdb_create_table(old_entry);
	error_t err = table_ret.e;

	if (compiler_expected(err == OK)) {
		bool_result_t is_contig_r = memdb_update_table(
			start, end, old_entry, new_entry, table_ret.r,
			entry_bits - MEMDB_BITS_PER_LEVEL);
		err = is_contig_r.e;

		if (compiler_expected(err == OK)) {
			assert(!is_contig_r.r);
			memdb_entry_t table_entry = memdb_entry_default();
			memdb_entry_set_entry_type(&table_entry,
						   MEMDB_TYPE_LEVEL_TABLE);
			memdb_entry_set_entry_ptr(&table_entry,
						  (uintptr_t)table_ret.r);
			atomic_store_release(&table->entries[entry_index],
					     table_entry);
		} else {
			// Update failed. The table is no longer needed.
			memdb_release_level_table(table_ret.r);
		}
	}

	return err;
}

static error_t
memdb_update_table_entry(paddr_t start, paddr_t end, memdb_entry_t old_entry,
			 memdb_entry_t new_entry, memdb_level_table_t *table,
			 index_t entry_bits, index_t entry_index)
	REQUIRE_LOCK(memdb_lock)
{
	error_t err;

	assert((entry_bits <= MEMDB_ROOT_ENTRY_BITS) &&
	       ((start >> entry_bits) == (end >> entry_bits)));

	const memdb_entry_t cur_entry =
		atomic_load_consume(&table->entries[entry_index]);
	memdb_type_t cur_type = memdb_entry_get_entry_type(&cur_entry);

	if (cur_type == MEMDB_TYPE_LEVEL_TABLE) {
		err = memdb_update_table_entry_level_table(
			start, end, old_entry, new_entry, table, entry_bits,
			cur_entry, entry_index);

	} else if (cur_type == MEMDB_TYPE_LEVEL_BITMAP) {
		err = memdb_update_table_entry_level_bitmap(
			start, end, old_entry, new_entry, table, entry_bits,
			cur_entry, entry_index);

	} else if (!memdb_entry_is_equal(cur_entry, old_entry)) {
		// The existing entry must be equal to the specified old entry.
		err = ERROR_MEMDB_NOT_OWNER;

	} else if (util_is_p2aligned(start, entry_bits) &&
		   util_is_p2aligned(end + 1U, entry_bits)) {
		// If the existing entry's whole range is covered; replace it.
		atomic_store_release(&table->entries[entry_index], new_entry);
		err = OK;

	} else if (entry_bits <= MEMDB_MIN_BITS) {
		// We can't create any deeper levels, so the alignment check
		// failure is fatal.
		err = ERROR_ARGUMENT_ALIGNMENT;

	} else if ((entry_bits == (MEMDB_PAGE_BITS + MEMDB_BITS_PER_LEVEL)) &&
		   util_is_p2aligned(start, MEMDB_PAGE_BITS) &&
		   util_is_p2aligned(end + 1U, MEMDB_PAGE_BITS)) {
		// If the next level entries are page sized and the range is
		// page aligned, we should create a next-level bitmap.
		err = memdb_update_table_entry_split_bitmap(
			start, end, old_entry, new_entry, table, entry_bits,
			entry_index);
	} else {
		// Create a next-level table.
		err = memdb_update_table_entry_split_table(start, end,
							   old_entry, new_entry,
							   table, entry_bits,
							   entry_index);
	}

	return err;
}

error_t
memdb_insert(partition_t *partition, paddr_t start_addr, paddr_t end_addr,
	     uintptr_t object, memdb_type_t obj_type)
{
	return memdb_update(partition, start_addr, end_addr, object, obj_type,
			    0U, MEMDB_TYPE_NOTYPE);
}

error_t
memdb_update(partition_t *partition, paddr_t start_addr, paddr_t end_addr,
	     uintptr_t object, memdb_type_t obj_type, uintptr_t prev_object,
	     memdb_type_t prev_type)
{
	assert(partition == partition_get_private());

	error_t err = memdb_range_check(start_addr, end_addr);
	if (err != OK) {
		LOG(ERROR, WARN,
		    "memdb: range invalid for update: {:#x} .. {:#x}: {:d}",
		    start_addr, end_addr, (register_t)err);
		goto out;
	}

	const memdb_entry_t new_entry =
		memdb_entry_for_object(object, obj_type);
	const memdb_entry_t old_entry =
		memdb_entry_for_object(prev_object, prev_type);
	spinlock_acquire(&memdb_lock);
	err = memdb_update_table(start_addr, end_addr, old_entry, new_entry,
				 &memdb_root, MEMDB_ROOT_ENTRY_BITS)
		      .e;
	spinlock_release(&memdb_lock);

	if (err == OK) {
		TRACE(MEMDB, INFO,
		      "memdb_update: {:#x}..{:#x} - {:#x} -> {:#x}", start_addr,
		      end_addr, memdb_entry_raw(old_entry),
		      memdb_entry_raw(new_entry));

#if defined(MEMDB_DEBUG)
		// Check that the range was added correctly
		bool cont = memdb_is_ownership_contiguous(start_addr, end_addr,
							  object, obj_type);
		if (!cont) {
			LOG(ERROR, INFO,
			    "<<< memdb_update BUG!! range {:#x}..{:#x} should be contiguous",
			    start_addr, end_addr);
			panic("BUG in memdb_update");
		}
#endif
	} else {
		TRACE(MEMDB, INFO,
		      "memdb: Error updating {:#x}..{:#x} - {:#x} -> {:#x}: {:d}",
		      start_addr, end_addr, memdb_entry_raw(old_entry),
		      memdb_entry_raw(new_entry), (register_t)err);
	}

out:
	return err;
}

static memdb_obj_type_result_t
memdb_lookup_bitmap(paddr_t addr, const memdb_level_bitmap_t *bitmap,
		    index_t entry_bits) REQUIRE_RCU_READ
{
	assert(entry_bits <= MEMDB_ROOT_ENTRY_BITS);
	const index_t index	= memdb_entry_index(addr, entry_bits);
	index_t	      object_id = (index_t)bitmap_atomic_extract(
		      bitmap->bitmap, index * MEMDB_BITMAP_ID_BITS,
		      MEMDB_BITMAP_ID_BITS, memory_order_relaxed);
	const memdb_entry_t entry =
		atomic_load_consume(&bitmap->objects[object_id]);

	memdb_obj_type_result_t ret;
	memdb_type_t		entry_type = memdb_entry_get_entry_type(&entry);
	uintptr_t		entry_ptr  = memdb_entry_get_entry_ptr(&entry);

	// Next level entries would duplicate entire branches of the tree, so
	// they shouldn't be present in a bitmap level.
	assert((entry_type != MEMDB_TYPE_LEVEL_TABLE) &&
	       (entry_type != MEMDB_TYPE_LEVEL_BITMAP));

	if (entry_type == MEMDB_TYPE_NOTYPE) {
		ret = memdb_obj_type_result_error(ERROR_MEMDB_EMPTY);
	} else {
		ret = memdb_obj_type_result_ok((memdb_obj_type_t){
			.object = entry_ptr,
			.type	= entry_type,
		});
	}

	return ret;
}

static memdb_obj_type_result_t
memdb_lookup_table(paddr_t addr, const memdb_level_table_t *table,
		   index_t entry_bits) REQUIRE_RCU_READ
{
	assert(entry_bits <= MEMDB_ROOT_ENTRY_BITS);
	const index_t	    index = memdb_entry_index(addr, entry_bits);
	const memdb_entry_t entry = atomic_load_consume(&table->entries[index]);

	memdb_obj_type_result_t ret;

	memdb_type_t entry_type = memdb_entry_get_entry_type(&entry);
	uintptr_t    entry_ptr	= memdb_entry_get_entry_ptr(&entry);
	if (entry_type == MEMDB_TYPE_NOTYPE) {
		ret = memdb_obj_type_result_error(ERROR_MEMDB_EMPTY);
	} else if (entry_type == MEMDB_TYPE_LEVEL_TABLE) {
		ret = memdb_lookup_table(addr, (memdb_level_table_t *)entry_ptr,
					 entry_bits - MEMDB_BITS_PER_LEVEL);
	} else if (entry_type == MEMDB_TYPE_LEVEL_BITMAP) {
		ret = memdb_lookup_bitmap(addr,
					  (memdb_level_bitmap_t *)entry_ptr,
					  entry_bits - MEMDB_BITS_PER_LEVEL);
	} else {
		ret = memdb_obj_type_result_ok((memdb_obj_type_t){
			.object = entry_ptr,
			.type	= entry_type,
		});
	}

	return ret;
}

memdb_obj_type_result_t
memdb_lookup(paddr_t addr)
{
	memdb_obj_type_result_t ret;

	if (addr >= util_bit(MEMDB_MAX_BITS)) {
		ret = memdb_obj_type_result_error(ERROR_ARGUMENT_INVALID);
	} else {
		ret = memdb_lookup_table(addr, &memdb_root,
					 MEMDB_ROOT_ENTRY_BITS);
	}

	return ret;
}

static bool
memdb_is_contig_entry(paddr_t start, paddr_t end, memdb_entry_t entry,
		      memdb_entry_t cur_entry, index_t entry_bits)
	REQUIRE_RCU_READ;

static bool
memdb_is_contig_bitmap(paddr_t start, paddr_t end, memdb_entry_t entry,
		       const memdb_level_bitmap_t *bitmap, index_t entry_bits)
	REQUIRE_RCU_READ
{
	bool is_contig = true;

	assert((entry_bits <= MEMDB_ROOT_ENTRY_BITS) &&
	       ((start >> (entry_bits + MEMDB_BITS_PER_LEVEL)) ==
		(end >> (entry_bits + MEMDB_BITS_PER_LEVEL))));

	index_t object_id;
	for (object_id = 0U; object_id < MEMDB_BITMAP_OBJECTS; object_id++) {
		if (memdb_entry_is_equal(
			    atomic_load_relaxed(&bitmap->objects[object_id]),
			    entry)) {
			break;
		}
	}

	// Order the object ID search before the bitmap reads (if it succeeded)
	// and anything that is conditional on the result of the contiguous
	// check (if it failed).
	atomic_thread_fence(memory_order_acquire);

	if (object_id == MEMDB_BITMAP_OBJECTS) {
		// The requested entry is not in this bitmap at all.
		is_contig = false;
		goto out;
	}

	const index_t start_index = memdb_entry_index(start, entry_bits);
	const index_t end_index	  = memdb_entry_index(end, entry_bits);

	// TODO: This could be optimised by using splat, xor and CLZ to find
	// contiguous ranges.
	for (index_t i = start_index; i <= end_index; i++) {
		const index_t cur_object_id = (index_t)bitmap_atomic_extract(
			bitmap->bitmap, i * MEMDB_BITMAP_ID_BITS,
			MEMDB_BITMAP_ID_BITS, memory_order_relaxed);

		if (cur_object_id != object_id) {
			is_contig = false;
			break;
		}
	}

out:
	return is_contig;
}

static bool
memdb_is_contig_table(paddr_t start, paddr_t end, memdb_entry_t entry,
		      const memdb_level_table_t *table, index_t entry_bits)
	REQUIRE_RCU_READ
{
	bool is_contig = true;

	assert((entry_bits <= MEMDB_ROOT_ENTRY_BITS) &&
	       ((start >> (entry_bits + MEMDB_BITS_PER_LEVEL)) ==
		(end >> (entry_bits + MEMDB_BITS_PER_LEVEL))));

	const index_t start_index = memdb_entry_index(start, entry_bits);
	const index_t end_index	  = memdb_entry_index(end, entry_bits);

	paddr_t entry_start = start;
	for (index_t i = start_index; i <= end_index; i++) {
		const memdb_entry_t cur_entry =
			atomic_load_consume(&table->entries[i]);
		paddr_t entry_end =
			util_min(end, entry_start | util_mask(entry_bits));

		is_contig = memdb_is_contig_entry(entry_start, entry_end, entry,
						  cur_entry, entry_bits);
		if (!is_contig) {
			break;
		}

		entry_start = entry_end + 1U;
	}

	return is_contig;
}

static bool
memdb_is_contig_entry(paddr_t start, paddr_t end, memdb_entry_t entry,
		      memdb_entry_t cur_entry, index_t entry_bits)
	REQUIRE_RCU_READ
{
	assert((entry_bits <= MEMDB_ROOT_ENTRY_BITS) &&
	       ((start >> entry_bits) == (end >> entry_bits)));

	bool is_contig;

	memdb_type_t cur_type = memdb_entry_get_entry_type(&cur_entry);
	uintptr_t    cur_ptr  = memdb_entry_get_entry_ptr(&cur_entry);

	if (cur_type == MEMDB_TYPE_LEVEL_TABLE) {
		is_contig = memdb_is_contig_table(
			start, end, entry, (memdb_level_table_t *)cur_ptr,
			entry_bits - MEMDB_BITS_PER_LEVEL);
	} else if (cur_type == MEMDB_TYPE_LEVEL_BITMAP) {
		is_contig = memdb_is_contig_bitmap(
			start, end, entry, (memdb_level_bitmap_t *)cur_ptr,
			entry_bits - MEMDB_BITS_PER_LEVEL);
	} else {
		is_contig = memdb_entry_is_equal(entry, cur_entry);
	}

	return is_contig;
}

bool
memdb_is_ownership_contiguous(paddr_t start_addr, paddr_t end_addr,
			      uintptr_t object, memdb_type_t obj_type)
{
	const memdb_entry_t entry = memdb_entry_for_object(object, obj_type);
	rcu_read_start();
	bool result = memdb_is_contig_table(start_addr, end_addr, entry,
					    &memdb_root, MEMDB_ROOT_ENTRY_BITS);
	rcu_read_finish();
	return result;
}

error_t
memdb_walk(uintptr_t object, memdb_type_t type, memdb_fnptr fn, void *arg)
{
	return memdb_range_walk(object, type, 0U, util_mask(MEMDB_MAX_BITS), fn,
				arg);
}

static size_result_t
memdb_walk_entry(memdb_entry_t entry, paddr_t start, paddr_t end,
		 const memdb_entry_t cur_entry, index_t entry_bits,
		 memdb_fnptr fn, void *arg, size_t pending_size)
	REQUIRE_RCU_READ;

static size_result_t
memdb_walk_table(memdb_entry_t entry, paddr_t start, paddr_t end,
		 const memdb_level_table_t *table, index_t entry_bits,
		 memdb_fnptr fn, void *arg, size_t pending_size)
	REQUIRE_RCU_READ
{
	size_result_t ret = size_result_ok(pending_size);

	assert((entry_bits <= MEMDB_ROOT_ENTRY_BITS) &&
	       ((start >> (entry_bits + MEMDB_BITS_PER_LEVEL)) ==
		(end >> (entry_bits + MEMDB_BITS_PER_LEVEL))));

	const index_t start_index = memdb_entry_index(start, entry_bits);
	const index_t end_index	  = memdb_entry_index(end, entry_bits);

	paddr_t entry_start = start;
	for (index_t i = start_index; i <= end_index; i++) {
		const memdb_entry_t cur_entry =
			atomic_load_consume(&table->entries[i]);
		paddr_t entry_end =
			util_min(end, entry_start | util_mask(entry_bits));

		ret = memdb_walk_entry(entry, entry_start, entry_end, cur_entry,
				       entry_bits, fn, arg, ret.r);
		if (ret.e != OK) {
			break;
		}

		entry_start = entry_end + 1U;
	}

	return ret;
}

static size_result_t
memdb_walk_bitmap(memdb_entry_t entry, paddr_t start, paddr_t end,
		  const memdb_level_bitmap_t *bitmap, index_t entry_bits,
		  memdb_fnptr fn, void *arg, size_t pending_size)
	REQUIRE_RCU_READ
{
	size_result_t ret;

	assert((entry_bits <= MEMDB_ROOT_ENTRY_BITS) &&
	       ((start >> (entry_bits + MEMDB_BITS_PER_LEVEL)) ==
		(end >> (entry_bits + MEMDB_BITS_PER_LEVEL))));

	index_t object_id;
	for (object_id = 0U; object_id < MEMDB_BITMAP_OBJECTS; object_id++) {
		if (memdb_entry_is_equal(
			    atomic_load_relaxed(&bitmap->objects[object_id]),
			    entry)) {
			break;
		}
	}

	// Order the object ID search before the bitmap reads (if it succeeded)
	// and the handler function (if it failed).
	atomic_thread_fence(memory_order_acquire);

	if (object_id == MEMDB_BITMAP_OBJECTS) {
		// The requested entry is not in this bitmap at all. Call the
		// handler function for the pending range if necessary.
		error_t err = OK;
		if (pending_size != 0U) {
			err = fn(start - pending_size, pending_size, arg);
		}
		ret = (size_result_t){ .e = err, .r = 0U };
		goto out;
	}
	ret = size_result_ok(pending_size);

	const index_t start_index = memdb_entry_index(start, entry_bits);
	const index_t end_index	  = memdb_entry_index(end, entry_bits);

	// TODO: This could be optimised by using splat, xor and CLZ to find
	// contiguous ranges.
	paddr_t entry_start = start;
	for (index_t i = start_index; i <= end_index; i++) {
		const index_t cur_object_id = (index_t)bitmap_atomic_extract(
			bitmap->bitmap, i * MEMDB_BITMAP_ID_BITS,
			MEMDB_BITMAP_ID_BITS, memory_order_relaxed);

		paddr_t entry_end =
			util_min(end, entry_start | util_mask(entry_bits));

		if (cur_object_id == object_id) {
			// Matching; increase the pending size
			ret.r += (entry_end - entry_start) + 1U;
		} else if (ret.r != 0U) {
			// Not matching, and pending size is nonzero; call the
			// handler function for the pending range
			error_t err = fn(entry_start - ret.r, ret.r, arg);
			ret	    = (size_result_t){ .e = err, .r = 0U };
			if (err != OK) {
				break;
			}
		} else {
			// Neither matching nor pending, nothing to do.
		}

		entry_start = entry_end + 1U;
	}

out:
	return ret;
}

static size_result_t
memdb_walk_entry(memdb_entry_t entry, paddr_t start, paddr_t end,
		 const memdb_entry_t cur_entry, index_t entry_bits,
		 memdb_fnptr fn, void *arg, size_t pending_size)
	REQUIRE_RCU_READ
{
	assert((entry_bits <= MEMDB_ROOT_ENTRY_BITS) &&
	       ((start >> entry_bits) == (end >> entry_bits)));

	size_result_t ret;

	memdb_type_t cur_type = memdb_entry_get_entry_type(&cur_entry);
	uintptr_t    cur_ptr  = memdb_entry_get_entry_ptr(&cur_entry);

	if (cur_type == MEMDB_TYPE_LEVEL_TABLE) {
		ret = memdb_walk_table(entry, start, end,
				       (memdb_level_table_t *)cur_ptr,
				       entry_bits - MEMDB_BITS_PER_LEVEL, fn,
				       arg, pending_size);
	} else if (cur_type == MEMDB_TYPE_LEVEL_BITMAP) {
		ret = memdb_walk_bitmap(entry, start, end,
					(memdb_level_bitmap_t *)cur_ptr,
					entry_bits - MEMDB_BITS_PER_LEVEL, fn,
					arg, pending_size);
	} else if (memdb_entry_is_equal(entry, cur_entry)) {
		// Matching; increase the pending size
		ret = size_result_ok(pending_size + (end - start) + 1U);
	} else if (pending_size != 0U) {
		error_t err = fn(start - pending_size, pending_size, arg);
		ret	    = (size_result_t){ .e = err, .r = 0U };
	} else {
		ret = size_result_ok(0U);
	}

	return ret;
}

error_t
memdb_range_walk(uintptr_t object, memdb_type_t obj_type, paddr_t start_addr,
		 paddr_t end_addr, memdb_fnptr fn, void *arg)
{
	error_t err;
	if (obj_type == MEMDB_TYPE_NOTYPE) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	const memdb_entry_t entry = memdb_entry_for_object(object, obj_type);

	// Truncate the range at the maximum address.
	paddr_t end = util_min(end_addr, util_mask(MEMDB_MAX_BITS));

	if (start_addr > end) {
		// The range contains no addresses, so there's nothing to do.
		err = OK;
		goto out;
	}

	rcu_read_start();
	size_result_t ret = memdb_walk_table(entry, start_addr, end,
					     &memdb_root, MEMDB_ROOT_ENTRY_BITS,
					     fn, arg, 0U);
	if ((ret.e == OK) && (ret.r != 0U)) {
		err = fn(end - ret.r + 1U, ret.r, arg);
	} else {
		err = ret.e;
	}
	rcu_read_finish();

out:
	return err;
}

error_t
memdb_bitmap_handle_partition_add_ram_range(partition_t *owner,
					    paddr_t phys_base, size_t size)
{
	partition_t *hyp_partition = partition_get_private();

	assert(size > 0U);
	assert(!util_add_overflows(phys_base, size - 1U));

	error_t err = memdb_insert(hyp_partition, phys_base,
				   phys_base + (size - 1U), (uintptr_t)owner,
				   MEMDB_TYPE_PARTITION);
	if (err != OK) {
		LOG(ERROR, WARN,
		    "memdb: Error adding ram {:#x}..{:#x} to partition {:x}, err = {:d}",
		    phys_base, phys_base + size - 1U, (register_t)owner,
		    (register_t)err);
	}

	return err;
}

error_t
memdb_bitmap_handle_partition_remove_ram_range(partition_t *owner,
					       paddr_t phys_base, size_t size)
{
	partition_t *hyp_partition = partition_get_private();

	assert(size > 0U);
	assert(!util_add_overflows(phys_base, size - 1U));

	error_t err = memdb_update(hyp_partition, phys_base,
				   phys_base + (size - 1U), 0U,
				   MEMDB_TYPE_NOTYPE, (uintptr_t)owner,
				   MEMDB_TYPE_PARTITION);
	if (err != OK) {
		LOG(ERROR, WARN,
		    "memdb: Error removing ram {:#x}..{:#x} from partition {:x}, err = {:d}",
		    phys_base, phys_base + size - 1U, (register_t)owner,
		    (register_t)err);
	}

	return err;
}
