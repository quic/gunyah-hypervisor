// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// How the memory database works:
//
// - Finding the common level:
// We first calculate the common bits between the start and end address passed
// as arguments. To know which are the common bits in the address, we use
// 'shifts' so that we can do (addr >> shifts) and get the common bits.
// With these common bits we either create (in case of insertion) or search for
// the level where the paths of the start and end address separate, this level
// is what we call 'common level'.
// In an insertion, we will use the common bits and shifts to set 'guard' and
// 'guard shifts' to entries. We use guards so that we can skip levels where all
// their entries are empty except the entry that points to the next level. We
// will only (for now) use guards, when possible, between the root and the
// common level. In no other operations guards should be set.
//
// - Adding start and end address:
// Once we found the common level, then we use the start/end address to go down
// levels (or create them if needed) until we reach to the levels where all bits
// of the address have been covered.
//
// - Going down levels:
// As we go down the levels we will do it by jumping from one entry of a level
// to a next one in the next level. We need to check if the entry contains a
// guard, and if so we need to check if the guard matches to our address (addr
// >> guard_shifts) and act accordingly. In insertion, there are several corner
// cases we will need to take care of and do some adjustments if the guard
// matches or not, in the rest of the operations if a guard does not match with
// the address then we will return error as probably the address we are
// searching for is not in the database.
//
// - Synchronization:
// 1. Atomic operations: we will read and write entries atomically using
// atomic_load_explicit and atomic_store_explicit, mostly with
// memory_order_relaxed. We will only use memory_order_release when we create a
// level and update parent to point to the new level.
// 2. RCUs: we use rcu_read_start() and rcu_read_finish() in lookups and when
// checking the contiguousness of an address range. We use rcu_enqueue() when we
// want to remove a level from the database, we will trigger the RCU update
// event handler that takes care of deallocating a level.
// 3. Spinlocks: We will only use spinlocks in insertion and updates. Above the
// common level, we will initially always be holding 2 locks. We will be going
// down levels and checking if the current level needs the lock. If the current
// level needs the locks (because its values are going to be modified or it
// might be collapsed since all entries except the current one point to the same
// object we are going to insert in the database) then we will keep the lock of
// the previous level, the current one and all consecutive levels. If the
// current level, does NOT need the lock then we will remove the lock to
// previous level and continue to hold the current one for now.

#include <assert.h>
#include <hyptypes.h>
#include <limits.h>

#include <hypcontainers.h>

#include <allocator.h>
#include <atomic.h>
#include <bootmem.h>
#include <compiler.h>
#include <log.h>
#include <memdb.h>
#include <panic.h>
#include <partition.h>
#include <rcu.h>
#include <spinlock.h>
#include <trace.h>
#include <trace_helpers.h>
#include <util.h>

#include "event_handlers.h"

// Set to 1 to boot enable the MEMDB tracepoints
#if defined(VERBOSE_TRACE) && VERBOSE_TRACE
#define DEBUG_MEMDB_TRACES 1
#else
#define DEBUG_MEMDB_TRACES 0
#endif

#define MEMDB_BITS_PER_ENTRY_MASK util_mask(MEMDB_BITS_PER_ENTRY)
#define ADDR_SIZE		  (sizeof(paddr_t) * CHAR_BIT)
// levels + 1 for root
#define MAX_LEVELS (ADDR_SIZE / MEMDB_BITS_PER_ENTRY) + 1

static memdb_t memdb;

extern const char image_phys_start;
extern const char image_phys_last;

static const paddr_t phys_start = (paddr_t)&image_phys_start;
static const paddr_t phys_end	= (paddr_t)&image_phys_last;

typedef struct start_path {
	memdb_level_t *levels[MAX_LEVELS];
	index_t	       indexes[MAX_LEVELS];
	count_t	       count;
} start_path_t;

typedef struct locked_levels {
	spinlock_t *	       locks[MAX_LEVELS];
	_Atomic memdb_entry_t *entries[MAX_LEVELS];
	count_t		       count;
	uint8_t		       pad_end_[4];
} locked_levels_t;

static count_t
lowest_unmatching_bits(paddr_t start_addr, paddr_t end_addr)
{
	assert(start_addr != end_addr);

	paddr_t ret = ADDR_SIZE - compiler_clz(start_addr ^ end_addr);

	assert(ret <= ADDR_SIZE);

	return (count_t)ret;
}

static count_t
calculate_common_bits(paddr_t start_addr, paddr_t end_addr)
{
	count_t shifts;

	shifts = lowest_unmatching_bits(start_addr, end_addr);
	shifts = util_balign_up(shifts, MEMDB_BITS_PER_ENTRY);

	return shifts;
}

static index_t
get_next_index(paddr_t addr, count_t *shifts)
{
	assert(*shifts != 0U);
	assert(*shifts <= ADDR_SIZE);

	*shifts -= MEMDB_BITS_PER_ENTRY;

	return ((addr >> *shifts) & MEMDB_BITS_PER_ENTRY_MASK);
}

static void
atomic_entry_write(_Atomic memdb_entry_t *entry_dst, memory_order order,
		   paddr_t guard, count_t guard_shifts, memdb_type_t type,
		   uintptr_t object)
{
	memdb_entry_t entry_src = { { 0 }, (uintptr_t)0U };

	memdb_entry_info_set_guard(&entry_src.info, guard);
	memdb_entry_info_set_shifts(&entry_src.info, guard_shifts);
	memdb_entry_info_set_type(&entry_src.info, type);
	entry_src.next = object;
	atomic_store_explicit(entry_dst, entry_src, order);
}

static memdb_entry_t
atomic_entry_read(_Atomic memdb_entry_t *entry_src, paddr_t *guard,
		  count_t *guard_shifts, memdb_type_t *type, uintptr_t *next)
{
	memdb_entry_t entry_dst = atomic_load_consume(entry_src);

	*guard	      = memdb_entry_info_get_guard(&entry_dst.info);
	*guard_shifts = memdb_entry_info_get_shifts(&entry_dst.info);
	*type	      = memdb_entry_info_get_type(&entry_dst.info);
	*next	      = entry_dst.next;

	return entry_dst;
}

static void
init_level(memdb_level_t *level, allocator_t *allocator, memdb_type_t type,
	   uintptr_t obj)
{
	spinlock_init(&level->lock);
	level->allocator = allocator;

	for (index_t i = 0; i < MEMDB_NUM_ENTRIES; i++) {
		// Guard shifts of 64 (ADDR_SIZE) means there is no guard.
		atomic_entry_write(&level->level[i], memory_order_relaxed,
				   MEMDB_TYPE_LEVEL, ADDR_SIZE, type, obj);
	}
}

static memdb_level_t *
create_level(allocator_t *allocator, memdb_type_t type, uintptr_t obj)
{
	void_ptr_result_t ret = allocator_allocate_object(
		allocator, sizeof(memdb_level_t), alignof(memdb_level_t));

	if (ret.e != OK) {
		LOG(ERROR, WARN, "memdb allocate err: {:d}", (register_t)ret.e);
		panic("memdb allocation failure");
	}

	memdb_level_t *level = ret.r;
	init_level(level, allocator, type, obj);

	return level;
}

// Check if the level entries point to the same object. If we pass an index
// different from MEMDB_NUM_ENTRIES, it will check all entries except that index
static bool
are_all_entries_same(memdb_level_t *level, uintptr_t object, index_t index,
		     memdb_type_t type, index_t start, index_t end)
{
	bool ret = true;

	for (index_t i = start; i < end; i++) {
		memdb_entry_t level_entry = atomic_load_explicit(
			&level->level[i], memory_order_relaxed);
		if ((i != index) &&
		    ((memdb_entry_info_get_type(&level_entry.info) != type) ||
		     (level_entry.next != object))) {
			ret = false;
			break;
		}
	}

	return ret;
}

rcu_update_status_t
memdb_deallocate_level(rcu_entry_t *entry)
{
	rcu_update_status_t ret = rcu_update_status_default();
	error_t		    err;

	memdb_level_t *level = memdb_level_container_of_rcu_entry(entry);

	allocator_t *allocator = level->allocator;

	err = allocator_deallocate_object(allocator, level,
					  sizeof(memdb_level_t));
	if (err != OK) {
		panic("Error deallocating level");
	}

	return ret;
}

// Unlock levels, but check before if all entries of level are the same. If so,
// update parent with pointer to object, unlock level and deallocate level using
// RCU.
//
// The entry parent of the level will always be in the previous index.
// Lock level[x], parent entry[x -1]
static void
unlock_levels(locked_levels_t *locked_levels)
{
	bool	     optimize = true;
	bool	     res      = false;
	memdb_type_t type;
	paddr_t	     guard;
	count_t	     guard_shifts;
	uintptr_t    next;

	assert(locked_levels->count != 0U);

	for (count_t i = (locked_levels->count - 1); i > 0; i--) {
		memdb_entry_t entry = atomic_load_explicit(
			locked_levels->entries[i - 1], memory_order_relaxed);
		memdb_level_t *level = (memdb_level_t *)entry.next;

		if (optimize) {
			atomic_entry_read(&level->level[0], &guard,
					  &guard_shifts, &type, &next);

			res = are_all_entries_same(level, next,
						   MEMDB_NUM_ENTRIES, type, 0,
						   MEMDB_NUM_ENTRIES);
			if (res) {
				// Update parent and deallocate level.
				atomic_entry_write(
					locked_levels->entries[i - 1],
					memory_order_relaxed, guard,
					guard_shifts, type, next);

				spinlock_release(locked_levels->locks[i]);

				rcu_enqueue(
					&level->rcu_entry,
					RCU_UPDATE_CLASS_MEMDB_RELEASE_LEVEL);

				continue;
			} else {
				optimize = false;
			}
		}

		spinlock_release(locked_levels->locks[i]);
	}

	if (locked_levels->locks[0] != NULL) {
		spinlock_release(locked_levels->locks[0]);
	}
}

static paddr_t
calculate_address(paddr_t addr, count_t shifts, index_t index)
{
	paddr_t result = util_p2align_down(addr, shifts + MEMDB_BITS_PER_ENTRY);

	assert(index < util_bit(MEMDB_BITS_PER_ENTRY));

	result |= (uint64_t)index << shifts;
	result |= util_mask(shifts);

	return result;
}

static error_t
fill_level_entries(memdb_level_t *level, uintptr_t object, memdb_type_t type,
		   uintptr_t prev_object, memdb_type_t prev_type,
		   index_t start_index, index_t end_index, paddr_t addr,
		   paddr_t *last_success_addr, count_t shifts, memdb_op_t op)
{
	error_t ret	     = OK;
	index_t failed_index = 0;

	if (start_index == end_index) {
		goto out;
	}

	for (index_t i = start_index; i < end_index; i++) {
		memdb_entry_t level_entry = atomic_load_explicit(
			&level->level[i], memory_order_relaxed);

		if ((memdb_entry_info_get_type(&level_entry.info) !=
		     prev_type) ||
		    (level_entry.next != prev_object)) {
			failed_index = i;
			ret	     = ERROR_MEMDB_NOT_OWNER;
			goto end_function;
		}
		atomic_entry_write(&level->level[i], memory_order_relaxed, 0,
				   ADDR_SIZE, type, object);
	}

end_function:
	if (ret != OK) {
		if (failed_index > start_index) {
			*last_success_addr = calculate_address(
				addr, shifts, failed_index - 1);
		}
	} else {
		if ((op != MEMDB_OP_ROLLBACK) && (start_index != end_index)) {
			*last_success_addr =
				calculate_address(addr, shifts, end_index - 1);
		}
	}
out:
	return ret;
}

static void
lock_level(memdb_level_t *level, index_t index, locked_levels_t *locked_levels)
{
	assert(locked_levels->count < MAX_LEVELS);

	spinlock_acquire(&level->lock);
	locked_levels->locks[locked_levels->count]   = &level->lock;
	locked_levels->entries[locked_levels->count] = &level->level[index];
	locked_levels->count++;
}

static error_t
check_guard(count_t guard_shifts, paddr_t guard, paddr_t addr, count_t *shifts)
{
	error_t ret = OK;

	if (guard_shifts != ADDR_SIZE) {
		if (guard != (addr >> guard_shifts)) {
			ret = ERROR_ADDR_INVALID;
		} else {
			if (shifts != NULL) {
				*shifts = guard_shifts;
			}
		}
	}

	return ret;
}

static error_t
create_n_levels(allocator_t *allocator, memdb_level_t **level, bool start,
		count_t *shifts, index_t *index, paddr_t addr, uintptr_t object,
		memdb_type_t type, uintptr_t prev_object,
		memdb_type_t prev_type, start_path_t *start_path,
		locked_levels_t *locked_levels, memdb_level_t *first_level,
		memdb_level_t **common_level, count_t *common_level_shifts,
		memdb_op_t op, paddr_t *last_success_addr, count_t limit)
{
	error_t	     ret	= OK;
	paddr_t	     comparison = 0;
	paddr_t	     level_guard;
	count_t	     level_guard_shifts;
	memdb_type_t level_type;
	uintptr_t    level_next;

	if (!start) {
		// Compare remaining end address bits with all ones
		comparison = util_mask(*shifts);
	}

	atomic_entry_read(&(*level)->level[*index], &level_guard,
			  &level_guard_shifts, &level_type, &level_next);

	// Create levels and update parent entry to point to new level.
	while ((*shifts != limit) &&
	       ((util_mask(*shifts) & addr) != comparison)) {
		memdb_level_t *next_level =
			create_level(allocator, prev_type, prev_object);

		if ((op != MEMDB_OP_ROLLBACK) && (*level != first_level)) {
			lock_level(*level, *index, locked_levels);
		}

		level_guard	   = 0;
		level_guard_shifts = ADDR_SIZE;
		level_type	   = MEMDB_TYPE_LEVEL;
		level_next	   = (uintptr_t)next_level;

		atomic_entry_write(&(*level)->level[*index],
				   memory_order_release, level_guard,
				   level_guard_shifts, level_type, level_next);
		if (start) {
			count_t aux_shifts = *shifts + MEMDB_BITS_PER_ENTRY;

			if ((op == MEMDB_OP_ROLLBACK) &&
			    (aux_shifts != ADDR_SIZE) &&
			    ((*last_success_addr >> aux_shifts) ==
			     (addr >> aux_shifts))) {
				*common_level	     = *level;
				*common_level_shifts = *shifts;
			}

			if ((start_path->count == 0) ||
			    (start_path->levels[start_path->count - 1] !=
			     *level)) {
				start_path->levels[start_path->count]  = *level;
				start_path->indexes[start_path->count] = *index;
				start_path->count++;
			}
		}

		if ((!start) && (*level != first_level)) {
			ret = fill_level_entries(
				*level, object, type, prev_object, prev_type, 0,
				*index, addr, last_success_addr, *shifts, op);
			if (ret != OK) {
				// We add it to list of levels so that it can
				// get optimized. Dummy index.
				lock_level(next_level, 0, locked_levels);
				goto error;
			}
		}

		*index = get_next_index(addr, shifts);

		if (!start) {
			comparison = util_mask(*shifts);
		}

		*level = next_level;
	}

error:
	return ret;
}

static error_t
go_down_levels(memdb_level_t *first_level, memdb_level_t **level, paddr_t addr,
	       index_t *index, count_t *shifts, memdb_op_t op, bool start,
	       start_path_t *start_path, locked_levels_t *locked_levels,
	       uintptr_t object, memdb_type_t type, uintptr_t prev_object,
	       memdb_type_t prev_type, memdb_level_t **common_level,
	       count_t *common_level_shifts, paddr_t *last_success_addr,
	       allocator_t *allocator)
{
	paddr_t	     level_guard;
	count_t	     level_guard_shifts;
	memdb_type_t level_type;
	uintptr_t    level_next;
	error_t	     ret = OK;

	atomic_entry_read(&(*level)->level[*index], &level_guard,
			  &level_guard_shifts, &level_type, &level_next);

	// We need to go down the levels until we find an empty entry or we run
	// out of remaining bits. In the former case, return error since the
	// address already has an owner.
	while ((level_type == MEMDB_TYPE_LEVEL) && (*shifts != 0U)) {
		if (start) {
			paddr_t level_shifts = *shifts + MEMDB_BITS_PER_ENTRY;

			if ((start) && (op == MEMDB_OP_ROLLBACK) &&
			    (level_shifts != ADDR_SIZE) &&
			    ((*last_success_addr >> level_shifts) ==
			     (addr >> level_shifts))) {
				*common_level	     = *level;
				*common_level_shifts = *shifts;
			}

			if ((start_path->count == 0) ||
			    (start_path->levels[start_path->count - 1] !=
			     *level)) {
				start_path->levels[start_path->count]  = *level;
				start_path->indexes[start_path->count] = *index;
				start_path->count++;
			}
		}

		if ((op == MEMDB_OP_INSERT) &&
		    (level_guard_shifts != ADDR_SIZE)) {
			memdb_level_t *last_level = (memdb_level_t *)level_next;
			count_t	       last_shifts;

			ret = check_guard(level_guard_shifts, level_guard, addr,
					  NULL);
			if (ret == OK) {
				// Guard matches: remove guard and create
				// intermediate levels covering the guard bits.

				last_shifts		 = level_guard_shifts;
				level_guard		 = 0;
				level_guard_shifts	 = ADDR_SIZE;
				memdb_level_t *level_aux = *level;

				ret = create_n_levels(
					allocator, level, start, shifts, index,
					addr, object, type, prev_object,
					prev_type, start_path, locked_levels,
					first_level, common_level,
					common_level_shifts, op,
					last_success_addr, last_shifts);
				if (ret != OK) {
					goto end_function;
				}

				atomic_entry_write(&(*level)->level[*index],
						   memory_order_release,
						   level_guard,
						   level_guard_shifts,
						   level_type,
						   (uintptr_t)last_level);

				if (start && (*level != level_aux)) {
					paddr_t level_shifts =
						*shifts + MEMDB_BITS_PER_ENTRY;

					if ((start) &&
					    (op == MEMDB_OP_ROLLBACK) &&
					    (level_shifts != ADDR_SIZE) &&
					    ((*last_success_addr >>
					      level_shifts) ==
					     (addr >> level_shifts))) {
						*common_level	     = *level;
						*common_level_shifts = *shifts;
					}
					start_path->levels[start_path->count] =
						*level;
					start_path->indexes[start_path->count] =
						*index;
					start_path->count++;
				}

			} else {
				// Guard does not match: create intermediate
				// levels that cover only matching bits.
				// There are always some matching bits, at
				// least the one of the entry index.

				count_t aux_shifts = 0;
				paddr_t tmp_cmn	   = addr >> level_guard_shifts;

				// We update guard to common bits between them.
				aux_shifts = calculate_common_bits(level_guard,
								   tmp_cmn);

				if ((aux_shifts + level_guard_shifts) !=
				    ADDR_SIZE) {
					index_t new_index;

					last_shifts = level_guard_shifts +
						      aux_shifts -
						      MEMDB_BITS_PER_ENTRY;
					memdb_level_t *level_aux = *level;

					ret = create_n_levels(
						allocator, level, start, shifts,
						index, addr, object, type,
						prev_object, prev_type,
						start_path, locked_levels,
						first_level, common_level,
						common_level_shifts, op,
						last_success_addr, last_shifts);
					if (ret != OK) {
						goto end_function;
					}

					new_index = get_next_index(level_guard,
								   &aux_shifts);

					// Add old guard in index
					atomic_entry_write(
						&(*level)->level[new_index],
						memory_order_release,
						level_guard, level_guard_shifts,
						level_type,
						(uintptr_t)last_level);

					if (start && (*level != level_aux)) {
						paddr_t level_shifts =
							*shifts +
							MEMDB_BITS_PER_ENTRY;

						if ((start) &&
						    (op == MEMDB_OP_ROLLBACK) &&
						    (level_shifts !=
						     ADDR_SIZE) &&
						    ((*last_success_addr >>
						      level_shifts) ==
						     (addr >> level_shifts))) {
							*common_level = *level;
							*common_level_shifts =
								*shifts;
						}
						start_path->levels
							[start_path->count] =
							*level;
						start_path->indexes
							[start_path->count] =
							*index;
						start_path->count++;
					}
				}
				goto end_function;
			}

		} else {
			// If entry has guard, it must match with common bits.
			ret = check_guard(level_guard_shifts, level_guard, addr,
					  shifts);
			if (ret != OK) {
				assert(op != MEMDB_OP_ROLLBACK);
				goto end_function;
			}
		}

		if (*level != first_level) {
			if ((op == MEMDB_OP_INSERT) ||
			    (op == MEMDB_OP_UPDATE)) {
				lock_level(*level, *index, locked_levels);
			}

			if (!start) {
				ret = fill_level_entries(*level, object, type,
							 prev_object, prev_type,
							 0, *index, addr,
							 last_success_addr,
							 *shifts, op);
				if (ret != OK) {
					goto end_function;
				}
			}
		}

		*level = (memdb_level_t *)level_next;
		*index = get_next_index(addr, shifts);

		atomic_entry_read(&(*level)->level[*index], &level_guard,
				  &level_guard_shifts, &level_type,
				  &level_next);
	}

	if ((level_type != prev_type) || (level_next != prev_object) ||
	    ((*shifts == 0U) && (prev_type == MEMDB_TYPE_NOTYPE))) {
		ret = ERROR_MEMDB_NOT_OWNER;
		goto end_function;
	}

end_function:
	return ret;
}

static error_t
add_address(allocator_t *allocator, uintptr_t object, memdb_type_t type,
	    memdb_level_t *first_level, paddr_t addr,
	    count_t first_level_shifts, bool start, uintptr_t prev_object,
	    memdb_type_t prev_type, paddr_t *last_success_addr,
	    locked_levels_t *locked_levels, memdb_op_t op)
{
	memdb_level_t *level  = first_level;
	count_t	       shifts = first_level_shifts;
	index_t	       index  = get_next_index(addr, &shifts);
	paddr_t	       level_guard;
	count_t	       level_guard_shifts;
	memdb_type_t   level_type;
	uintptr_t      level_next;
	start_path_t   start_path	   = { { NULL }, { 0 }, 0 };
	count_t	       common_level_shifts = 0;
	memdb_level_t *common_level	   = NULL;
	count_t	       count		   = 0;
	error_t	       ret		   = OK;

	ret = go_down_levels(first_level, &level, addr, &index, &shifts, op,
			     start, &start_path, locked_levels, object, type,
			     prev_object, prev_type, &common_level,
			     &common_level_shifts, last_success_addr,
			     allocator);
	if (ret != OK) {
		goto end_function;
	}

	assert(shifts != ADDR_SIZE);

	ret = create_n_levels(allocator, &level, start, &shifts, &index, addr,
			      object, type, prev_object, prev_type, &start_path,
			      locked_levels, first_level, &common_level,
			      &common_level_shifts, op, last_success_addr, 0);
	if (ret != OK) {
		goto end_function;
	}

	if ((op != MEMDB_OP_ROLLBACK) && (level != first_level)) {
		lock_level(level, index, locked_levels);
	}

	// If we are in the last MEMDB_BITS_PER_ENTRY bits or if the remaining
	// bits of start address are all zeroes or the remaining bits of end
	// address are all ones, then we can directly point to the object.
	if ((!start) && (level != first_level)) {
		ret = fill_level_entries(level, object, type, prev_object,
					 prev_type, 0, index, addr,
					 last_success_addr, shifts, op);
		if (ret != OK) {
			goto end_function;
		}
	}

	level_guard	   = 0;
	level_guard_shifts = ADDR_SIZE;
	level_type	   = type;
	level_next	   = object;

	atomic_entry_write(&level->level[index], memory_order_relaxed,
			   level_guard, level_guard_shifts, level_type,
			   level_next);

	if (op != MEMDB_OP_ROLLBACK) {
		*last_success_addr = calculate_address(addr, shifts, index);
	}

	if (!start) {
		goto end_function;
	}

	count_t aux_shifts = shifts + MEMDB_BITS_PER_ENTRY;

	// Rest of function only applicable for start path
	if ((op == MEMDB_OP_ROLLBACK) && (aux_shifts != ADDR_SIZE) &&
	    ((*last_success_addr >> aux_shifts) == (addr >> aux_shifts))) {
		common_level	    = level;
		common_level_shifts = shifts;
	}

	if ((start_path.count == 0U) ||
	    (start_path.levels[start_path.count - 1] != level)) {
		start_path.levels[start_path.count]  = level;
		start_path.indexes[start_path.count] = index;
		start_path.count++;
	}

	if (common_level == NULL) {
		common_level	    = first_level;
		common_level_shifts = first_level_shifts - MEMDB_BITS_PER_ENTRY;
	}

	count = start_path.count - 1;

	// Fill entries from start_index+1 to MEMDB_NUM_ENTRIES in start path
	// levels
	while (start_path.levels[count] != common_level) {
		index_t start_index = start_path.indexes[count] + 1;

		level = start_path.levels[count];

		ret = fill_level_entries(level, object, type, prev_object,
					 prev_type, start_index,
					 MEMDB_NUM_ENTRIES, addr,
					 last_success_addr, shifts, op);
		if (ret != OK) {
			goto end_function;
		}
		count--;
	}

	if ((op == MEMDB_OP_ROLLBACK) && (count != 0U)) {
		level		    = start_path.levels[count];
		index_t start_index = start_path.indexes[count] + 1;
		index_t end_index =
			((*last_success_addr >> common_level_shifts) &
			 MEMDB_BITS_PER_ENTRY_MASK) +
			1;

		// Fill intermediate entries of new common level
		ret = fill_level_entries(level, object, type, prev_object,
					 prev_type, start_index, end_index,
					 addr, last_success_addr, shifts, op);
		if (ret != OK) {
			goto end_function;
		}

		*last_success_addr = (paddr_t)-1;
	}

end_function:
	return ret;
}

// Adds start and end address entries and intermediate entries between them.
// First go down to the level where the start address is located, then go up
// to the common levels adding all entries between start_index+1 to
// MEMDB_NUM_ENTRIES in each level, then add entries from start_index+1 to
// end_index-1 in the common level, and finally go done to the level where the
// end address is, adding all the entries from 0 to end_index-1 in each level.
//
// If there an entry points to an object different from prev_object, it means
// the address already has an owner. If so, return error and rollback to
// initial state by calling this function again but now the end_addr will be the
// last_success_addr.
static error_t
add_address_range(allocator_t *allocator, paddr_t start_addr, paddr_t end_addr,
		  memdb_level_t *common_level, count_t shifts, uintptr_t object,
		  memdb_type_t type, uintptr_t prev_object,
		  memdb_type_t prev_type, locked_levels_t *end_locked_levels,
		  locked_levels_t *start_locked_levels,
		  paddr_t *last_success_addr, memdb_op_t op)
{
	count_t start_shifts = shifts;
	count_t end_shifts   = shifts;
	index_t start_index  = get_next_index(start_addr, &start_shifts);
	index_t end_index    = get_next_index(end_addr, &end_shifts);
	bool	rollback     = false;
	paddr_t mask	     = util_mask(start_shifts);
	error_t ret	     = OK;

	if (op == MEMDB_OP_ROLLBACK) {
		rollback = true;
	}

	// Add entry already if range is covered by only one entry
	if ((start_index == end_index) && ((mask & start_addr) == 0U) &&
	    ((mask & end_addr) == mask)) {
		count_t	     level_guard_shifts;
		paddr_t	     level_guard;
		memdb_type_t level_type;
		uintptr_t    level_next;

		atomic_entry_read(&common_level->level[start_index],
				  &level_guard, &level_guard_shifts,
				  &level_type, &level_next);
		if ((level_type == prev_type) && (level_next == prev_object)) {
			atomic_entry_write(&common_level->level[start_index],
					   memory_order_relaxed, 0, ADDR_SIZE,
					   type, object);
		} else {
			ret = ERROR_MEMDB_NOT_OWNER;
		}
		goto end_function;
	}

	if (!rollback) {
		// For the start entries, I add the entry from the common level
		// since it might be updated if level below is collapsed. I do
		// not add the lock since it is already in the end locks array.
		start_locked_levels->entries[0] =
			&common_level->level[start_index];
		start_locked_levels->locks[0] = NULL;
		start_locked_levels->count++;
	}

	// Find START address entry and point it to object
	ret = add_address(allocator, object, type, common_level, start_addr,
			  shifts, true, prev_object, prev_type,
			  last_success_addr, start_locked_levels, op);

	if (ret != OK) {
		goto end_function;
	}
	if (rollback && (*last_success_addr == 0U)) {
		goto end_function;
	}

	// Fill first level intermediate entries between start end end
	ret = fill_level_entries(common_level, object, type, prev_object,
				 prev_type, start_index + 1, end_index,
				 start_addr, last_success_addr, end_shifts, op);
	if (ret != OK) {
		goto end_function;
	}

	// Find END address entry and point it to object
	ret = add_address(allocator, object, type, common_level, end_addr,
			  shifts, false, prev_object, prev_type,
			  last_success_addr, end_locked_levels, op);
end_function:
	return ret;
}

static error_t
compare_adjust_bits(count_t guard_shifts, count_t shifts,
		    count_t *extra_guard_shifts, count_t *extra_shifts,
		    paddr_t guard, paddr_t addr, bool insert)
{
	paddr_t tmp_guard = guard;
	paddr_t tmp_cmn;
	error_t ret = OK;

	if (guard_shifts > shifts) {
		*extra_shifts = guard_shifts - shifts;
	} else if (guard_shifts < shifts) {
		if (insert) {
			*extra_guard_shifts = shifts - guard_shifts;
		} else {
			ret = ERROR_ADDR_INVALID;
			goto end_function;
		}
	}

	if (insert) {
		if ((guard_shifts + *extra_guard_shifts) != ADDR_SIZE) {
			tmp_guard = guard >> *extra_guard_shifts;
		} else {
			tmp_guard = 0;
		}
	}

	if ((shifts + *extra_shifts) != ADDR_SIZE) {
		tmp_cmn = addr >> (shifts + *extra_shifts);
	} else {
		tmp_cmn = 0;
	}

	assert((shifts + *extra_shifts) <= ADDR_SIZE);
	assert((guard_shifts + *extra_guard_shifts) <= ADDR_SIZE);

	// If guard & common shifts differ, we calculate the highest common
	// bits between them and keep track of the remaining bits.
	if ((tmp_guard ^ tmp_cmn) != 0U) {
		count_t aux_shifts = 0;

		if (!insert) {
			ret = ERROR_ADDR_INVALID;
			goto end_function;
		}

		aux_shifts = calculate_common_bits(tmp_guard, tmp_cmn);

		// If there are no common bits between them, the guard
		// will not act as a shortcut.
		*extra_guard_shifts += aux_shifts;
		*extra_shifts += aux_shifts;

		assert((shifts + *extra_shifts) <= ADDR_SIZE);
		assert((guard_shifts + *extra_guard_shifts) <= ADDR_SIZE);

		if (*extra_guard_shifts != ADDR_SIZE) {
			tmp_guard = guard >> *extra_guard_shifts;
		} else {
			tmp_guard = 0;
		}
		if ((shifts + *extra_shifts) != ADDR_SIZE) {
			tmp_cmn = addr >> (shifts + *extra_shifts);
		} else {
			tmp_cmn = 0;
		}
	}
	assert((tmp_guard ^ tmp_cmn) == 0U);

end_function:
	return ret;
}

static error_t
add_extra_shifts_update(allocator_t *allocator, count_t *shifts,
			count_t extra_shifts, uintptr_t next,
			paddr_t start_addr, paddr_t end_addr, uintptr_t object,
			memdb_type_t obj_type, uintptr_t prev_object,
			memdb_type_t prev_type, memdb_level_t **common_level,
			bool locking, bool lock_taken,
			locked_levels_t *locked_levels)
{
	count_t	       rem_cmn_shifts = *shifts + extra_shifts;
	memdb_level_t *level	      = (memdb_level_t *)next;
	bool	       new_level      = false;
	count_t	       level_guard_shifts;
	paddr_t	       level_guard;
	memdb_type_t   level_type;
	uintptr_t      level_next;
	error_t	       ret = OK;

	// If !locking it means that we are in the middle of a
	// MEMDB_OP_CONTIGUOUSNESS op

	while (rem_cmn_shifts != *shifts) {
		index_t index = get_next_index(start_addr, &rem_cmn_shifts);

		if (locking) {
			// Lock level and check if it is needed. If so,
			// we keep lock to previous and current level
			// and lock all next levels. If not, we remove
			// lock from previous level.
			lock_level(level, index, locked_levels);
		}

		atomic_entry_read(&level->level[index], &level_guard,
				  &level_guard_shifts, &level_type,
				  &level_next);

		// If entry has guard, it must match with common bits.
		ret = check_guard(level_guard_shifts, level_guard, start_addr,
				  &rem_cmn_shifts);
		if (ret != OK) {
			goto end_function;
		}

		if (level_type == MEMDB_TYPE_LEVEL) {
			// Go down levels until common level or we
			// reach an entry pointing to previous object

			if ((locking) && (!lock_taken) &&
			    !are_all_entries_same(level, object, index,
						  obj_type, 0,
						  MEMDB_NUM_ENTRIES)) {
				// Current level does not need lock,
				// remove previous level lock and reset
				// locked levels count.
				spinlock_t *lock = locked_levels->locks[0];

				spinlock_release(lock);

				assert(locked_levels->count == 2U);

				locked_levels->entries[0] =
					locked_levels->entries[1];
				locked_levels->locks[0] =
					locked_levels->locks[1];
				locked_levels->entries[1] = 0;
				locked_levels->locks[1]	  = 0;
				locked_levels->count	  = 1;
			} else if (locking) {
				// Current level needs to be locked, so all
				// next levels also need to be.
				lock_taken = true;
			}

			level = (memdb_level_t *)level_next;

			if (rem_cmn_shifts == *shifts) {
				*common_level = level;

				if (locking) {
					count_t tmp_shifts = *shifts;

					index = get_next_index(end_addr,
							       &tmp_shifts);
					lock_level(*common_level, index,
						   locked_levels);
				}
			}
		} else if (locking &&
			   ((new_level) || ((level_type == prev_type) &&
					    (level_next == prev_object)))) {
			// Create new level with all entries
			// pointing to prev owner
			memdb_level_t *next_level =
				create_level(allocator, prev_type, prev_object);

			// Keep current and next levels lock since
			// current level will be modified.
			lock_taken = true;

			level_type = MEMDB_TYPE_LEVEL;
			level_next = (uintptr_t)next_level;

			atomic_entry_write(&level->level[index],
					   memory_order_release, level_guard,
					   level_guard_shifts, level_type,
					   level_next);

			if (rem_cmn_shifts == *shifts) {
				count_t tmp_shifts = *shifts;

				*common_level = next_level;

				index = get_next_index(end_addr, &tmp_shifts);
				lock_level(*common_level, index, locked_levels);
			} else {
				new_level = true;
				level	  = next_level;
			}
		} else if (!locking && (level_type == obj_type) &&
			   (level_next == object)) {
			*common_level = level;
			*shifts	      = rem_cmn_shifts + MEMDB_BITS_PER_ENTRY;
			goto end_function;
		} else if (level_type == MEMDB_TYPE_NOTYPE) {
			ret = ERROR_ADDR_INVALID;
			goto end_function;
		} else {
			ret = ERROR_MEMDB_NOT_OWNER;
			goto end_function;
		}
	}

end_function:
	return ret;
}

static void
add_extra_guard_shifts(allocator_t *allocator, count_t guard_shifts,
		       paddr_t guard, uintptr_t *next, memdb_type_t root_type,
		       count_t		extra_guard_shifts,
		       locked_levels_t *locked_levels, paddr_t end_addr)
{
	paddr_t	       new_guard;
	count_t	       level_guard_shifts;
	paddr_t	       level_guard;
	memdb_level_t *level = create_level(allocator, MEMDB_TYPE_NOTYPE, 0);

	assert(extra_guard_shifts != 0U);

	count_t new_guard_shifts = guard_shifts + extra_guard_shifts;

	assert(new_guard_shifts <= ADDR_SIZE);

	if (new_guard_shifts != ADDR_SIZE) {
		new_guard = guard >> extra_guard_shifts;
	} else {
		new_guard = 0;
	}

	index_t index = get_next_index(guard, &extra_guard_shifts);

	count_t tmp_shifts = new_guard_shifts;
	lock_level(level, get_next_index(end_addr, &tmp_shifts), locked_levels);

	level_guard	   = guard;
	level_guard_shifts = guard_shifts;

	atomic_entry_write(&level->level[index], memory_order_relaxed,
			   level_guard, level_guard_shifts, root_type, *next);

	root_type = MEMDB_TYPE_LEVEL;
	*next	  = (uintptr_t)level;

	atomic_entry_write(&memdb.root, memory_order_release, new_guard,
			   new_guard_shifts, root_type, *next);
}

static void
create_intermediate_level(allocator_t *allocator, paddr_t start_addr,
			  memdb_level_t *level, index_t index)
{
	paddr_t	     level_guard;
	count_t	     level_guard_shifts;
	memdb_type_t level_type;
	uintptr_t    level_next;
	paddr_t	     tmp_cmn;

	// Set guard equal to common bits and create level.

	atomic_entry_read(&level->level[index], &level_guard,
			  &level_guard_shifts, &level_type, &level_next);

	paddr_t	     new_guard	= level_guard;
	count_t	     new_shifts = level_guard_shifts;
	memdb_type_t new_type	= level_type;
	uintptr_t    new_next	= level_next;
	paddr_t	     tmp_guard	= level_guard;

	paddr_t level_addr = level_guard << level_guard_shifts;

	if (level_guard_shifts != (count_t)ADDR_SIZE) {
		tmp_cmn = start_addr >> level_guard_shifts;
	} else {
		tmp_cmn = 0;
	}

	memdb_level_t *next_level =
		create_level(allocator, MEMDB_TYPE_NOTYPE, 0);

	// We update guard to common bits between them.
	count_t aux_shifts = calculate_common_bits(tmp_guard, tmp_cmn);

	count_t tmp_shifts = level_guard_shifts + aux_shifts;

	// If there are no common bits, there is no guard
	if ((level_guard_shifts + aux_shifts) == ADDR_SIZE) {
		tmp_guard = 0;
	} else {
		tmp_guard = tmp_guard >> aux_shifts;
	}

	level_guard	   = tmp_guard;
	level_guard_shifts = tmp_shifts;
	level_type	   = MEMDB_TYPE_LEVEL;
	level_next	   = (uintptr_t)next_level;

	atomic_entry_write(&level->level[index], memory_order_release,
			   level_guard, level_guard_shifts, level_type,
			   level_next);

	// Add old entry to new level
	index = get_next_index(level_addr, &tmp_shifts);

	atomic_entry_write(&next_level->level[index], memory_order_relaxed,
			   new_guard, new_shifts, new_type, new_next);
}

static error_t
add_extra_shifts(allocator_t *allocator, count_t shifts, count_t extra_shifts,
		 uintptr_t next, paddr_t start_addr, paddr_t end_addr,
		 uintptr_t object, memdb_type_t obj_type,
		 memdb_level_t **common_level, bool lock_taken,
		 locked_levels_t *locked_levels)
{
	count_t	       level_guard_shifts;
	paddr_t	       level_guard;
	memdb_type_t   level_type;
	uintptr_t      level_next;
	count_t	       rem_cmn_shifts = shifts + extra_shifts;
	memdb_level_t *level	      = (memdb_level_t *)next;
	error_t	       ret	      = OK;

	while (rem_cmn_shifts != shifts) {
		index_t llevel_index = locked_levels->count - 1;
		count_t level_shifts = rem_cmn_shifts;

		index_t index = get_next_index(start_addr, &rem_cmn_shifts);

		// Lock level and check if it is needed. If so, we keep lock to
		// prev and current level and lock all next levels. If not, we
		// remove lock from previous level.
		if (locked_levels->locks[llevel_index] != &level->lock) {
			lock_level(level, index, locked_levels);
		}

		atomic_entry_read(&level->level[index], &level_guard,
				  &level_guard_shifts, &level_type,
				  &level_next);

		if (level_type != MEMDB_TYPE_NOTYPE) {
			if ((!lock_taken) &&
			    !are_all_entries_same(level, object, index,
						  obj_type, 0,
						  MEMDB_NUM_ENTRIES)) {
				// Current level doesn't need lock, remove prev
				// level lock and reset locked levels count.
				spinlock_t *lock = locked_levels->locks[0];

				spinlock_release(lock);

				assert(locked_levels->count == 2U);

				locked_levels->entries[0] =
					locked_levels->entries[1];
				locked_levels->locks[0] =
					locked_levels->locks[1];
				locked_levels->entries[1] = 0;
				locked_levels->locks[1]	  = 0;
				locked_levels->count	  = 1;
			} else {
				// Current level needs to hold lock, so all
				// next levels also.
				lock_taken = true;
			}

			// If guard, does it match with common bits?
			// 1. No  -> create an intermediate level.
			// 2. Yes -> (type == level) ?
			//	a. Yes -> go down to next level.
			//	b. No  -> error (already has owner).
			ret = check_guard(level_guard_shifts, level_guard,
					  start_addr, &rem_cmn_shifts);
			if (ret != OK) {
				ret = OK;
				create_intermediate_level(allocator, start_addr,
							  level, index);
				// Retry this level
				rem_cmn_shifts = level_shifts;

				lock_taken = true;
			} else {
				// Go down levels until common level
				if (level_type == MEMDB_TYPE_LEVEL) {
					level = (memdb_level_t *)level_next;
				} else {
					ret = ERROR_MEMDB_NOT_OWNER;
					goto end_function;
				}
			}

			if (rem_cmn_shifts == shifts) {
				*common_level = level;

				count_t tmp_shifts = shifts;
				index = get_next_index(end_addr, &tmp_shifts);
				lock_level(*common_level, index, locked_levels);
			}
		} else {
			// Set guard equal to common bits and create level.
			count_t tmp_shifts = shifts;

			memdb_level_t *next_level =
				create_level(allocator, MEMDB_TYPE_NOTYPE, 0);

			// Keep current and next levels lock since current level
			// values will be modified.
			lock_taken = true;

			if (shifts != ADDR_SIZE) {
				level_guard = start_addr >> shifts;
			} else {
				level_guard = 0;
			}
			level_guard_shifts = shifts;
			level_type	   = MEMDB_TYPE_LEVEL;
			level_next	   = (uintptr_t)next_level;

			atomic_entry_write(&level->level[index],
					   memory_order_release, level_guard,
					   level_guard_shifts, level_type,
					   level_next);

			index = get_next_index(end_addr, &tmp_shifts);
			lock_level(next_level, index, locked_levels);

			rem_cmn_shifts = shifts;
			*common_level  = next_level;
		}
	}

end_function:
	return ret;
}

static error_t
find_common_level(paddr_t start_addr, paddr_t end_addr,
		  memdb_level_t **common_level, count_t *shifts,
		  allocator_t *allocator, uintptr_t object,
		  memdb_type_t obj_type, uintptr_t prev_object,
		  memdb_type_t prev_type, locked_levels_t *locked_levels,
		  bool insert, bool first)
{
	error_t	     ret	= OK;
	bool	     lock_taken = false;
	count_t	     guard_shifts;
	paddr_t	     guard;
	memdb_type_t root_type;
	uintptr_t    next;
	count_t	     extra_shifts	= 0;
	count_t	     extra_guard_shifts = 0;
	bool	     locking		= false;

	if (locked_levels != NULL) {
		locking = true;
	}

	// We calculate the first common bits between start and end address
	// and save shifts (must be multiple of MEMDB_BITS_PER_ENTRY).
	*shifts = calculate_common_bits(start_addr, end_addr);

	// FIXME: check how to remove this restriction.
	// To simplify the code, we do not allow the root to point directly to
	// the object. If the remaining bits of start address are all zeroes
	// and all ones for end address, instead of making the root point to
	// the object, we will set the guard to be MEMDB_BITS_PER_ENTRY shorter
	// and add a level just after the root.
	if (*shifts != ADDR_SIZE) {
		paddr_t mask = util_mask(*shifts);

		if (((mask & start_addr) == 0U) &&
		    ((mask & end_addr) == mask)) {
			*shifts += MEMDB_BITS_PER_ENTRY;
		}
	}

	atomic_entry_read(&memdb.root, &guard, &guard_shifts, &root_type,
			  &next);

	if ((!first) && (root_type == MEMDB_TYPE_NOTYPE)) {
		ret = ERROR_MEMDB_EMPTY;
		goto end_function;
	}

	if (locking) {
		// Lock root until we know it is not need.
		spinlock_acquire(&memdb.lock);
		locked_levels->entries[0] = &memdb.root;
		locked_levels->locks[0]	  = &memdb.lock;
		locked_levels->count	  = 1;
	}

	if (first) {
		goto end_function;
	}

	// To compare guard & common bits, their length must be equal.
	ret = compare_adjust_bits(guard_shifts, *shifts, &extra_guard_shifts,
				  &extra_shifts, guard, start_addr, insert);
	if (ret != OK) {
		goto end_function;
	}

	assert(root_type == MEMDB_TYPE_LEVEL);
	assert((*shifts + extra_shifts) <= ADDR_SIZE);
	assert((guard_shifts + extra_guard_shifts) <= ADDR_SIZE);
	assert(insert || (extra_guard_shifts == 0U));
	assert(!insert || (locked_levels != NULL));
	assert((allocator != NULL) || (locked_levels == NULL));

	// If there are extra guard shifts, the guard needs to be updated and a
	// new level created to add the remaining guard.
	if (extra_guard_shifts != 0U) {
		// Root must keep lock since we need to modify its values.
		// Therefore, all consecutive levels should hold locks.
		lock_taken = true;

		add_extra_guard_shifts(allocator, guard_shifts, guard, &next,
				       root_type, extra_guard_shifts,
				       locked_levels, end_addr);
	}

	// If there are extra common shifts, we need to find the common level.
	if (extra_shifts != 0U) {
		if (!insert) {
			ret = add_extra_shifts_update(
				allocator, shifts, extra_shifts, next,
				start_addr, end_addr, object, obj_type,
				prev_object, prev_type, common_level, locking,
				lock_taken, locked_levels);
		} else {
			ret = add_extra_shifts(allocator, *shifts, extra_shifts,
					       next, start_addr, end_addr,
					       object, obj_type, common_level,
					       lock_taken, locked_levels);
		}
	} else {
		*common_level = (memdb_level_t *)next;

		// Lock common level if it is not already locked
		if (locking && !lock_taken) {
			count_t aux_shifts = *shifts;
			index_t index = get_next_index(end_addr, &aux_shifts);
			lock_level(*common_level, index, locked_levels);
		}
	}

end_function:
	return ret;
}

// - start_locked_levels : refer to locks held from levels after the common
// level to the level where the start address is.
//
// - end_locked_levels : refer to the locks held from the root to the common
// level to the level where the end address is located.
static error_t
add_range(allocator_t *allocator, paddr_t start_addr, paddr_t end_addr,
	  memdb_level_t *common_level, count_t shifts, uintptr_t object,
	  memdb_type_t obj_type, uintptr_t prev_object, memdb_type_t prev_type,
	  locked_levels_t *end_locked_levels, error_t init_error, memdb_op_t op)

{
	paddr_t		last_success_addr   = (paddr_t)-1;
	error_t		ret		    = OK;
	locked_levels_t start_locked_levels = { { NULL }, { NULL }, 0, { 0 } };

	if (init_error != OK) {
		ret = init_error;
		goto end_function;
	}

	ret = add_address_range(allocator, start_addr, end_addr, common_level,
				shifts, object, obj_type, prev_object,
				prev_type, end_locked_levels,
				&start_locked_levels, &last_success_addr, op);

	if ((ret != OK) && (start_addr <= last_success_addr) &&
	    (last_success_addr != (paddr_t)-1)) {
		// Rolling back the entries to old owner.
		add_address_range(allocator, start_addr, last_success_addr,
				  common_level, shifts, prev_object, prev_type,
				  object, obj_type, end_locked_levels,
				  &start_locked_levels, &last_success_addr,
				  MEMDB_OP_ROLLBACK);
	}

end_function:
	if (start_locked_levels.count != 0U) {
		unlock_levels(&start_locked_levels);
	}

	if (end_locked_levels->count != 0U) {
		unlock_levels(end_locked_levels);
	}

	return ret;
}

static error_t
check_address(memdb_level_t *first_level, memdb_level_t **level, paddr_t addr,
	      index_t *index, count_t *shifts, memdb_op_t op, bool start,
	      uintptr_t object, memdb_type_t type)
{
	paddr_t	     level_guard;
	count_t	     level_guard_shifts;
	memdb_type_t level_type;
	uintptr_t    level_next;
	error_t	     ret = OK;

	atomic_entry_read(&(*level)->level[*index], &level_guard,
			  &level_guard_shifts, &level_type, &level_next);

	// We need to go down the levels until we find an empty entry or we run
	// out of remaining bits. In the former case, return error since the
	// address already has an owner.
	while ((level_type == MEMDB_TYPE_LEVEL) && (*shifts != 0U)) {
		// If entry has guard, it must match with common bits.
		ret = check_guard(level_guard_shifts, level_guard, addr,
				  shifts);

		if (ret != OK) {
			goto error;
		}

		if ((*level != first_level) &&
		    (op == MEMDB_OP_CONTIGUOUSNESS)) {
			index_t start_index = 0;
			index_t end_index   = 0;
			bool	res	    = false;

			if (start) {
				start_index = *index + 1;
				end_index   = MEMDB_NUM_ENTRIES;
			} else {
				start_index = 0;
				end_index   = *index;
			}

			res = are_all_entries_same(*level, object,
						   MEMDB_NUM_ENTRIES, type,
						   start_index, end_index);
			if (!res) {
				ret = ERROR_MEMDB_NOT_OWNER;
				goto error;
			}
		}

		*level = (memdb_level_t *)level_next;
		*index = get_next_index(addr, shifts);

		atomic_entry_read(&(*level)->level[*index], &level_guard,
				  &level_guard_shifts, &level_type,
				  &level_next);
	}

	if ((op == MEMDB_OP_CONTIGUOUSNESS) &&
	    ((level_type != type) || (level_next != object))) {
		ret = ERROR_MEMDB_NOT_OWNER;
		goto error;
	}

error:
	return ret;
}

// Populate the memory database. If any entry from the range already has an
// owner, return error and do not update the database.
error_t
memdb_insert(partition_t *partition, paddr_t start_addr, paddr_t end_addr,
	     uintptr_t object, memdb_type_t obj_type)
{
	error_t		ret	      = OK;
	locked_levels_t locked_levels = { { NULL }, { NULL }, 0, { 0 } };
	paddr_t		guard;
	count_t		guard_shifts;
	memdb_type_t	root_type;
	uintptr_t	next;
	memdb_level_t * common_level = NULL;
	count_t		shifts;
	bool		insert	    = true;
	bool		first_entry = false;

	// Overlapping addresses and the entire address space will not be passed
	// as an argument to the function
	assert((start_addr != end_addr) && (start_addr < end_addr));
	assert((start_addr != 0U) || (~end_addr != 0U));
	assert(partition != NULL);

	allocator_t *allocator = &partition->allocator;

	atomic_entry_read(&memdb.root, &guard, &guard_shifts, &root_type,
			  &next);

	if (root_type == MEMDB_TYPE_NOTYPE) {
		first_entry = true;
	}

	ret = find_common_level(start_addr, end_addr, &common_level, &shifts,
				allocator, object, obj_type, 0,
				MEMDB_TYPE_NOTYPE, &locked_levels, insert,
				first_entry);
	if (ret != OK) {
		goto end_function;
	}

	// FIXME: remove this case and handle as any other new level.
	if (first_entry) {
		// Empty database. The root guard will be equal to the common
		// bits between start and end address.
		guard_shifts = shifts;

		if (shifts != ADDR_SIZE) {
			guard = start_addr >> shifts;
		} else {
			guard = 0;
		}

		// Create a new level and add address range entries.
		memdb_level_t *first_level =
			create_level(allocator, MEMDB_TYPE_NOTYPE, 0);

		count_t aux_shifts = shifts;
		index_t index	   = get_next_index(start_addr, &aux_shifts);

		lock_level(first_level, index, &locked_levels);

		root_type = MEMDB_TYPE_LEVEL;
		next	  = (uintptr_t)first_level;

		atomic_entry_write(&memdb.root, memory_order_release, guard,
				   guard_shifts, root_type, next);

		common_level = first_level;
	}

end_function:

	// Add range from level after the common bits on
	ret = add_range(allocator, start_addr, end_addr, common_level, shifts,
			object, obj_type, 0, MEMDB_TYPE_NOTYPE, &locked_levels,
			ret, MEMDB_OP_INSERT);

	if (ret == OK) {
		TRACE(MEMDB, INFO,
		      "memdb_insert: {:#x}..{:#x} - obj({:#x}) - type({:d})",
		      start_addr, end_addr, object, obj_type);
	} else {
		TRACE(MEMDB, INFO,
		      "memdb: Error inserting {:#x}..{:#x} - obj({:#x}) - type({:d}), err = {:d}",
		      start_addr, end_addr, object, obj_type, (register_t)ret);
	}

	return ret;
}

// Change the ownership of the input address range. Checks if all entries of
// range were pointing to previous object. If so, update all entries to point to
// the new object. If not, return error.
error_t
memdb_update(partition_t *partition, paddr_t start_addr, paddr_t end_addr,
	     uintptr_t object, memdb_type_t obj_type, uintptr_t prev_object,
	     memdb_type_t prev_type)
{
	error_t		ret = OK;
	count_t		shifts;
	locked_levels_t locked_levels = { { NULL }, { NULL }, 0, { 0 } };
	memdb_level_t * common_level  = NULL;

	// We need to find the common level, the level where all the first
	// common bits between start and end address are covered. Then, add
	// entries from the address range from that level on.

	// Overlapping addresses and the entire address space will not be passed
	// as an argument to the function
	assert((start_addr != end_addr) && (start_addr < end_addr));
	assert((start_addr != 0U) || (~end_addr != 0U));
	assert(partition != NULL);

	allocator_t *allocator = &partition->allocator;

	ret = find_common_level(start_addr, end_addr, &common_level, &shifts,
				allocator, object, obj_type, prev_object,
				prev_type, &locked_levels, false, false);

	ret = add_range(allocator, start_addr, end_addr, common_level, shifts,
			object, obj_type, prev_object, prev_type,
			&locked_levels, ret, MEMDB_OP_UPDATE);

	if (ret == OK) {
		TRACE(MEMDB, INFO,
		      "memdb_update: {:#x}..{:#x} - obj({:#x}) - type({:d})",
		      start_addr, end_addr, object, obj_type);
	} else {
		TRACE(MEMDB, INFO,
		      "memdb: Error updating {:#x}..{:#x} - obj({:#x}) - type({:d}), err = {:d}",
		      start_addr, end_addr, object, obj_type, (register_t)ret);
	}

	return ret;
}

// Check if all the entries from the input address range point to the object
// passed as an argument
bool
memdb_is_ownership_contiguous(paddr_t start_addr, paddr_t end_addr,
			      uintptr_t object, memdb_type_t type)
{
	memdb_level_t *common_level = NULL;
	count_t	       shifts;
	error_t	       ret	= OK;
	bool	       ret_bool = true;
	bool	       start	= true;

	rcu_read_start();

	memdb_entry_t root_entry =
		atomic_load_explicit(&memdb.root, memory_order_relaxed);
	if ((memdb_entry_info_get_type(&root_entry.info)) ==
	    MEMDB_TYPE_NOTYPE) {
		ret_bool = false;
		goto end_function;
	}

	assert((start_addr != end_addr) && (start_addr < end_addr));
	assert((start_addr != 0U) || (~end_addr != 0U));

	ret = find_common_level(start_addr, end_addr, &common_level, &shifts,
				NULL, object, type, 0, 0, NULL, false, false);
	if (ret != OK) {
		ret_bool = false;
		goto end_function;
	}

	count_t start_shifts = shifts;
	count_t end_shifts   = shifts;
	index_t start_index  = get_next_index(start_addr, &start_shifts);
	index_t end_index    = get_next_index(end_addr, &end_shifts);

	// Go down levels until START entry and check if it is equal to object.
	index_t	       index = start_index;
	memdb_level_t *level = common_level;

	ret = check_address(common_level, &level, start_addr, &index,
			    &start_shifts, MEMDB_OP_CONTIGUOUSNESS, start,
			    object, type);
	if (ret != OK) {
		ret_bool = false;
		goto end_function;
	}

	// Check first level intermediate entries between start end end
	ret_bool = are_all_entries_same(common_level, object, MEMDB_NUM_ENTRIES,
					type, start_index + 1, end_index);
	if (!ret_bool) {
		goto end_function;
	}

	// Go down levels until END entry and check if it is equal to object.
	index = end_index;
	level = common_level;

	ret = check_address(common_level, &level, end_addr, &index, &end_shifts,
			    MEMDB_OP_CONTIGUOUSNESS, !start, object, type);
	if (ret != OK) {
		ret_bool = false;
		goto end_function;
	}

end_function:
	rcu_read_finish();

	return ret_bool;
}

// Find the entry corresponding to the input address and return the object and
// type the entry is pointing to
memdb_obj_type_result_t
memdb_lookup(paddr_t addr)
{
	memdb_obj_type_result_t ret = { 0 };
	paddr_t			guard;
	count_t			guard_shifts;
	memdb_type_t		root_type;
	uintptr_t		next;
	memdb_level_t *		level;
	index_t			index;
	bool			start = true;

	rcu_read_start();

	atomic_entry_read(&memdb.root, &guard, &guard_shifts, &root_type,
			  &next);

	if (root_type == MEMDB_TYPE_NOTYPE) {
		ret.e = ERROR_MEMDB_EMPTY;
		goto end_function;
	}

	// If entry has guard, it must match with common bits.
	ret.e = check_guard(guard_shifts, guard, addr, NULL);
	if (ret.e != OK) {
		goto end_function;
	}

	level = (memdb_level_t *)next;
	index = get_next_index(addr, &guard_shifts);

	// Go down levels until we get to input address
	// Dummy start argument, does not affect lookup.
	ret.e = check_address((memdb_level_t *)next, &level, addr, &index,
			      &guard_shifts, MEMDB_OP_LOOKUP, start, 0, 0);
	if (ret.e != OK) {
		ret.r.type   = MEMDB_TYPE_NOTYPE;
		ret.r.object = 0;
	} else {
		memdb_entry_t entry = atomic_load_explicit(
			&level->level[index], memory_order_relaxed);

		ret.r.type   = memdb_entry_info_get_type(&entry.info);
		ret.r.object = entry.next;
	}

end_function:
	rcu_read_finish();

	return ret;
}

static error_t
memdb_do_walk(uintptr_t object, memdb_type_t type, memdb_fnptr fn, void *arg,
	      memdb_level_t *level, paddr_t covered_bits, count_t shifts,
	      paddr_t start_addr, paddr_t end_addr, bool all_memdb)
{
	count_t	       count	    = 0;
	index_t	       index	    = 0;
	paddr_t	       pending_base = 0;
	size_t	       pending_size = 0;
	error_t	       ret	    = OK;
	count_t	       guard_shifts;
	paddr_t	       guard;
	memdb_type_t   next_type;
	uintptr_t      next;
	index_t	       index_stack[MAX_LEVELS]	 = { 0 };
	count_t	       shifts_stack[MAX_LEVELS]	 = { 0 };
	paddr_t	       covered_stack[MAX_LEVELS] = { 0 };
	memdb_level_t *levels[MAX_LEVELS]	 = { NULL };

	if (!all_memdb) {
		index = get_next_index(start_addr, &shifts);
	}

	do {
		if (count > 0U) {
			count--;
			level	     = levels[count];
			index	     = index_stack[count];
			covered_bits = covered_stack[count];
			shifts	     = shifts_stack[count];
		}

		while (index != MEMDB_NUM_ENTRIES) {
			paddr_t base = (covered_bits << MEMDB_BITS_PER_ENTRY) |
				       index;
			base = base << shifts;

			// Stop iteration if we have reached to the end address,
			// when we are not walking through the entire database
			if (!all_memdb && (base > end_addr)) {
				count = 0U;
				break;
			}

			atomic_entry_read(&level->level[index], &guard,
					  &guard_shifts, &next_type, &next);

			if (guard_shifts != ADDR_SIZE) {
				if (next_type == MEMDB_TYPE_NOTYPE) {
					// FIXME: handle bad entry.
				} else {
					assert(next_type == MEMDB_TYPE_LEVEL);
				}
			}

			if ((next_type == type) && (next == object)) {
				// If entry points to the object, meaning this
				// address is owned by the object, we add it to
				// the pending address and size to be added to
				// the range. The range will be added when the
				// ownership stops being contiguous.

				size_t size = util_bit(shifts);

				if (!all_memdb) {
					if (base < start_addr) {
						size -= start_addr - base;
						base = start_addr;
					}

					if ((base + size - 1) > end_addr) {
						size -= (base + size - 1) -
							end_addr;
					}
				}

				if (pending_size != 0U) {
					assert((pending_base + pending_size) ==
					       base);
					pending_size += size;
				} else {
					pending_base = base;
					pending_size = size;
				}
				index++;

			} else if (next_type == MEMDB_TYPE_LEVEL) {
				// We move down to the next level and iterate
				// through all its entries. We save current
				// level so that we can eventually return to it
				// and continue iterating through its entries,
				// starting from the next index on.

				covered_stack[count] = covered_bits;
				shifts_stack[count]  = shifts;
				levels[count]	     = level;
				index_stack[count]   = index + 1;
				count++;

				if (guard_shifts == ADDR_SIZE) {
					covered_bits =
						(covered_bits
						 << MEMDB_BITS_PER_ENTRY) |
						index;
					shifts -= MEMDB_BITS_PER_ENTRY;
				} else {
					covered_bits = guard;
					shifts	     = guard_shifts -
						 MEMDB_BITS_PER_ENTRY;
				}

				level = (memdb_level_t *)next;
				index = 0;

			} else {
				// Entry does not point to object. Add range if
				// it is pending to be added.
				if (pending_size != 0U) {
					ret = fn(pending_base, pending_size,
						 arg);
					if (ret != OK) {
						goto error;
					}
					pending_base = 0;
					pending_size = 0;
				}
				index++;
			}
		}
	} while (count > 0U);

	if (pending_size != 0U) {
		ret = fn(pending_base, pending_size, arg);
		if (ret != OK) {
			goto error;
		}
	}
error:
	return ret;
}

// Walk through a range of the database and add the address ranges that are
// owned by the object passed as argument.
// FIXME: replace function pointer with a selector event
error_t
memdb_range_walk(uintptr_t object, memdb_type_t type, paddr_t start_addr,
		 paddr_t end_addr, memdb_fnptr fn, void *arg)
{
	error_t	       ret	    = OK;
	paddr_t	       covered_bits = 0;
	count_t	       shifts;
	memdb_level_t *common_level = NULL;

	rcu_read_start();

	memdb_entry_t root_entry =
		atomic_load_explicit(&memdb.root, memory_order_relaxed);
	if ((memdb_entry_info_get_type(&root_entry.info)) ==
	    MEMDB_TYPE_NOTYPE) {
		ret = ERROR_MEMDB_EMPTY;
		goto error;
	}

	assert((start_addr != end_addr) && (start_addr < end_addr));
	assert((start_addr != 0U) || (~end_addr != 0U));

	ret = find_common_level(start_addr, end_addr, &common_level, &shifts,
				NULL, object, type, 0, 0, NULL, false, false);
	if (ret != OK) {
		goto error;
	}

	memdb_level_t *level = common_level;
	if (shifts == ADDR_SIZE) {
		covered_bits = 0;
	} else {
		covered_bits = start_addr >> shifts;
	}

	ret = memdb_do_walk(object, type, fn, arg, level, covered_bits, shifts,
			    start_addr, end_addr, false);
error:
	rcu_read_finish();

	return ret;
}

// Walk through the entire database and add the address ranges that are owned
// by the object passed as argument.
// FIXME: replace function pointer with a selector event
error_t
memdb_walk(uintptr_t object, memdb_type_t type, memdb_fnptr fn, void *arg)
{
	error_t	     ret	  = OK;
	paddr_t	     covered_bits = 0;
	count_t	     guard_shifts;
	paddr_t	     guard;
	memdb_type_t next_type;
	uintptr_t    next;
	count_t	     shifts = ADDR_SIZE - MEMDB_BITS_PER_ENTRY;

	rcu_read_start();

	atomic_entry_read(&memdb.root, &guard, &guard_shifts, &next_type,
			  &next);

	if (next_type == MEMDB_TYPE_NOTYPE) {
		ret = ERROR_MEMDB_EMPTY;
		goto error;
	}

	assert(next_type == MEMDB_TYPE_LEVEL);

	memdb_level_t *level = (memdb_level_t *)next;

	if (guard_shifts != ADDR_SIZE) {
		covered_bits = guard;
	}

	shifts = guard_shifts - MEMDB_BITS_PER_ENTRY;

	ret = memdb_do_walk(object, type, fn, arg, level, covered_bits, shifts,
			    0, 0, true);
error:
	rcu_read_finish();

	return ret;
}

error_t
memdb_init(void)
{
	atomic_entry_write(&memdb.root, memory_order_relaxed, 0, ADDR_SIZE,
			   MEMDB_TYPE_NOTYPE, 0);
	spinlock_init(&memdb.lock);

	return OK;
}

void
memdb_handle_boot_cold_init(void)
{
#if !defined(NDEBUG) && DEBUG_MEMDB_TRACES
	register_t flags = 0U;
	TRACE_SET_CLASS(flags, MEMDB);
	trace_set_class_flags(flags);
#endif

	partition_t *hyp_partition = partition_get_private();
	assert(hyp_partition != NULL);

	// Initialize memory ownership database
	memdb_init();

	// Assign the hypervisor's ELF image to the private partition.
	error_t err = memdb_insert(hyp_partition, phys_start, phys_end,
				   (uintptr_t)hyp_partition,
				   MEMDB_TYPE_PARTITION);
	if (err != OK) {
		panic("Error adding boot memory to hyp_partition");
	}

	// Obtain the initial bootmem range and change its ownership to the
	// hypervisor's allocator. We assume here that no other memory has been
	// assigned to any allocators yet.
	size_t bootmem_size	 = 0U;
	void * bootmem_virt_base = bootmem_get_region(&bootmem_size);
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

error_t
memdb_handle_partition_add_ram_range(partition_t *owner, paddr_t phys_base,
				     size_t size)
{
	partition_t *hyp_partition = partition_get_private();

	assert(size > 0U);
	assert(!util_add_overflows(phys_base, size - 1U));

	// We should use memdb_insert() once this is safe to do so.
	error_t err = memdb_update(hyp_partition, phys_base,
				   phys_base + (size - 1U), (uintptr_t)owner,
				   MEMDB_TYPE_PARTITION, (uintptr_t)owner,
				   MEMDB_TYPE_PARTITION_NOMAP);
	if (err != OK) {
		LOG(ERROR, WARN,
		    "memdb: Error adding ram {:#x}..{:#x} to partition {:x}, err = {:d}",
		    phys_base, phys_base + size - 1U, (register_t)owner,
		    (register_t)err);
	}

	return err;
}

error_t
memdb_handle_partition_remove_ram_range(partition_t *owner, paddr_t phys_base,
					size_t size)
{
	partition_t *hyp_partition = partition_get_private();

	assert(size > 0U);
	assert(!util_add_overflows(phys_base, size - 1U));

	// We should use memdb_insert() once this is safe to do so.
	error_t err = memdb_update(hyp_partition, phys_base,
				   phys_base + (size - 1U), (uintptr_t)owner,
				   MEMDB_TYPE_PARTITION_NOMAP, (uintptr_t)owner,
				   MEMDB_TYPE_PARTITION);
	if (err != OK) {
		LOG(ERROR, WARN,
		    "memdb: Error removing ram {:#x}..{:#x} from partition {:x}, err = {:d}",
		    phys_base, phys_base + size - 1U, (register_t)owner,
		    (register_t)err);
	}

	return err;
}
