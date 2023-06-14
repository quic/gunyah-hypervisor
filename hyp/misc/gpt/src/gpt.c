// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <limits.h>
#include <string.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <gpt.h>
#include <log.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <rcu.h>
#include <trace.h>
#include <util.h>

#include <events/gpt.h>

#include "event_handlers.h"

#define SIZE_T_BITS (sizeof(size_t) * (size_t)CHAR_BIT)

static_assert(sizeof(gpt_value_t) <= sizeof(uint64_t),
	      "GPT value must not be larger than 64-bits!");
static_assert(GPT_TYPE__MAX < util_bit(GPT_TYPE_BITS),
	      "GPT type does not fit in PTE bitfield!");

bool
gpt_handle_empty_values_equal(void)
{
	return true;
}

void
gpt_handle_reserved_callback(void)
{
	panic("gpt: Reserved callback used");
}

static gpt_pte_t
gpt_pte_empty(void)
{
	return (gpt_pte_t){
		.info  = gpt_pte_info_default(),
		.value = { .raw = 0U },
	};
}

static size_t
get_max_size(gpt_config_t config)
{
	return util_bit(gpt_config_get_max_bits(&config));
}

static size_t
get_pte_addr(gpt_pte_t pte)
{
	size_t	guard  = gpt_pte_info_get_guard(&pte.info);
	count_t shifts = gpt_pte_info_get_shifts(&pte.info);

	return guard << shifts;
}

static size_t
get_pte_size(gpt_pte_t pte)
{
	return util_bit(gpt_pte_info_get_shifts(&pte.info));
}

static bool
guard_matching(gpt_pte_t pte, size_t addr)
{
	assert(gpt_pte_info_get_type(&pte.info) != GPT_TYPE_EMPTY);

	size_t	guard  = gpt_pte_info_get_guard(&pte.info);
	count_t shifts = gpt_pte_info_get_shifts(&pte.info);

	return (addr >> shifts) == guard;
}

static bool
entries_equal(gpt_entry_t a, gpt_entry_t b)
{
	return (a.type == b.type) &&
	       trigger_gpt_values_equal_event(a.type, a.value, b.value);
}

static gpt_pte_t
load_atomic_pte(_Atomic gpt_pte_t *p)
{
	return atomic_load_consume(p);
}

static void
store_atomic_pte(_Atomic gpt_pte_t *p, gpt_pte_t pte, bool init)
{
	if (init) {
		atomic_init(p, pte);
	} else {
		atomic_store_release(p, pte);
	}
}

static gpt_pte_t
load_root_pte(gpt_root_t *root, gpt_config_t config)
{
	gpt_pte_t pte;

	if (gpt_config_get_rcu_read(&config)) {
		pte = load_atomic_pte(&root->atomic);
	} else {
		pte = root->non_atomic;
	}

	return pte;
}

static gpt_pte_t
load_level_pte(gpt_config_t config, gpt_level_t level, index_t i)
{
	gpt_pte_t pte;

	if (gpt_config_get_rcu_read(&config)) {
		pte = load_atomic_pte(&level.atomic->entries[i]);
	} else {
		pte = level.non_atomic->entries[i];
	}

	return pte;
}

static void
store_root_pte(gpt_root_t *root, gpt_config_t config, gpt_pte_t pte, bool init)
{
	if (gpt_config_get_rcu_read(&config)) {
		store_atomic_pte(&root->atomic, pte, init);
	} else {
		root->non_atomic = pte;
	}
}

static void
store_level_pte(gpt_config_t config, gpt_level_t level, index_t i,
		gpt_pte_t pte, bool init)
{
	if (gpt_config_get_rcu_read(&config)) {
		store_atomic_pte(&level.atomic->entries[i], pte, init);
	} else {
		level.non_atomic->entries[i] = pte;
	}
}

static bool
entry_is_valid(gpt_t *gpt, gpt_entry_t entry)
{
	return (entry.type <= GPT_TYPE__MAX) &&
	       bitmap_isset(&gpt->allowed_types, (index_t)entry.type);
}

static bool
entry_is_valid_or_empty(gpt_t *gpt, gpt_entry_t entry)
{
	return entry_is_valid(gpt, entry) || (entry.type == GPT_TYPE_EMPTY);
}

static bool
pte_and_entry_equal(gpt_pte_t pte, size_t curr, gpt_entry_t entry)
{
	assert(guard_matching(pte, curr));

	size_t	    pte_addr  = get_pte_addr(pte);
	gpt_type_t  pte_type  = gpt_pte_info_get_type(&pte.info);
	gpt_value_t pte_value = pte.value;

	trigger_gpt_value_add_offset_event(pte_type, &pte_value,
					   curr - pte_addr);

	gpt_entry_t other = {
		.type  = pte_type,
		.value = pte_value,
	};

	return entries_equal(entry, other);
}

static bool
can_replace_pte(size_t curr, size_t rem, gpt_pte_t pte)
{
	size_t pte_addr = get_pte_addr(pte);
	size_t pte_size = get_pte_size(pte);

	assert(!guard_matching(pte, curr));

	// If the remaining region completely covers the range of the PTE, it is
	// safe to replace the PTE.
	return (curr <= pte_addr) && ((curr + rem) >= (pte_addr + pte_size));
}

static bool
pte_will_conflict(size_t curr, size_t rem, gpt_entry_t old, gpt_pte_t pte)
{
	bool ret = true;

	assert(!guard_matching(pte, curr));

	if (old.type != GPT_TYPE_EMPTY) {
		// We expected the GPT to not be empty at this point.
		goto out;
	}

	if (gpt_pte_info_get_type(&pte.info) == GPT_TYPE_LEVEL) {
		// We can only be sure of a conflict with a level if its entire
		// range is going to be replaced. Otherwise, we must traverse
		// the level first to be sure of a conflict.
		ret = can_replace_pte(curr, rem, pte);
		goto out;
	}

	size_t pte_addr = get_pte_addr(pte);

	// If the range to update overlaps with the PTE, we have a conflict.
	ret = (pte_addr >= curr) && (pte_addr < (curr + rem));

out:
	return ret;
}

static count_t
get_common_shifts(gpt_pte_t pte, size_t addr)
{
	count_t clz = compiler_clz(addr ^ get_pte_addr(pte));

	return (count_t)SIZE_T_BITS - util_balign_down(clz, GPT_LEVEL_BITS);
}

static index_t
get_level_index(count_t shifts, size_t addr)
{
	assert(shifts >= GPT_LEVEL_BITS);

	return (index_t)((addr >> (shifts - GPT_LEVEL_BITS)) &
			 util_mask(GPT_LEVEL_BITS));
}

static gpt_stack_frame_t *
get_curr_stack_frame(gpt_stack_t *stack)
{
	assert(stack->depth != 0U);

	index_t i = stack->depth - 1U;
	assert(i < GPT_MAX_LEVELS);

	return &stack->frame[i];
}

static count_t
get_max_entry_shifts(gpt_stack_t *stack)
{
	count_t shifts = GPT_MAX_SIZE_BITS;

	if (stack->depth != 0U) {
		gpt_stack_frame_t *frame = get_curr_stack_frame(stack);
		assert(frame != NULL);
		shifts = gpt_frame_info_get_shifts(&frame->info);
	}

	return shifts;
}

static count_t
get_max_possible_shifts(gpt_stack_t *stack, size_t curr, size_t rem)
{
	count_t shifts = get_max_entry_shifts(stack);

	assert(rem > 0U);

	if (curr != 0U) {
		count_t align_bits =
			util_balign_down(compiler_ctz(curr), GPT_LEVEL_BITS);
		shifts = util_min(shifts, align_bits);
	}

	if (util_bit(shifts) > rem) {
		shifts = util_balign_down(compiler_ctz(rem), GPT_LEVEL_BITS);
	}

	return shifts;
}

static gpt_level_t
get_level_from_pte(gpt_pte_t pte)
{
	gpt_level_t level = pte.value.level;
	assert(level.raw != 0U);

	return level;
}

static void
go_down_level(gpt_config_t config, gpt_stack_t *stack, size_t curr,
	      gpt_pte_t pte)
{
	gpt_level_t level = get_level_from_pte(pte);

	assert(guard_matching(pte, curr));

	stack->depth++;

	gpt_stack_frame_t *frame = get_curr_stack_frame(stack);
	assert(frame != NULL);

	count_t shifts = gpt_pte_info_get_shifts(&pte.info);
	assert(shifts >= GPT_LEVEL_BITS);

	size_t addr = get_pte_addr(pte);
	assert(addr < get_max_size(config));

	gpt_frame_info_t info = gpt_frame_info_default();
	gpt_frame_info_set_addr(&info, addr);
	gpt_frame_info_set_shifts(&info, shifts - GPT_LEVEL_BITS);

	frame->level = level;
	frame->info  = info;
}

static bool
check_ptes_consistent(gpt_pte_t a, gpt_pte_t b, size_t offset)
{
	bool ret = false;

	gpt_type_t type = gpt_pte_info_get_type(&a.info);
	if (type != gpt_pte_info_get_type(&b.info)) {
		goto out;
	}

	gpt_value_t x = a.value;
	gpt_value_t y = b.value;

	trigger_gpt_value_add_offset_event(type, &x, offset);

	ret = trigger_gpt_values_equal_event(type, x, y);

out:
	return ret;
}

static void
write_pte_to_level(gpt_root_t *root, gpt_config_t config, gpt_stack_t *stack,
		   gpt_pte_t pte)
{
	if (stack->depth == 0U) {
		store_root_pte(root, config, pte, false);
	} else {
		gpt_stack_frame_t *frame = get_curr_stack_frame(stack);
		assert(frame != NULL);

		index_t i = gpt_frame_info_get_index(&frame->info);
		assert(i < GPT_LEVEL_ENTRIES);

		store_level_pte(config, frame->level, i, pte, false);
		gpt_frame_info_set_dirty(&frame->info, true);
	}
}

rcu_update_status_t
gpt_handle_rcu_free_level(rcu_entry_t *entry)
{
	gpt_level_atomic_t *level =
		gpt_level_atomic_container_of_rcu_entry(entry);
	assert(level != NULL);
	assert(level->partition != NULL);

	(void)partition_free(level->partition, level, sizeof(*level));

	return rcu_update_status_default();
}

static void
free_level(gpt_config_t config, partition_t *partition, gpt_level_t level)
{
	if (gpt_config_get_rcu_read(&config)) {
		rcu_enqueue(&level.atomic->rcu_entry,
			    RCU_UPDATE_CLASS_GPT_FREE_LEVEL);
	} else {
		(void)partition_free(partition, level.non_atomic,
				     sizeof(gpt_level_non_atomic_t));
	}
}

static void
try_clean(gpt_root_t *root, gpt_config_t config, partition_t *partition,
	  gpt_stack_t *stack, gpt_level_t level, count_t entry_shifts)
{
	count_t	  filled_count	  = 0U;
	gpt_pte_t first_pte	  = gpt_pte_empty();
	gpt_pte_t last_filled_pte = gpt_pte_empty();
	bool	  can_merge	  = true;

	assert(partition != NULL);

	for (index_t i = 0U; i < GPT_LEVEL_ENTRIES; i++) {
		gpt_pte_t curr_pte = load_level_pte(config, level, i);

		if (gpt_pte_info_get_type(&curr_pte.info) == GPT_TYPE_EMPTY) {
			can_merge = false;
		} else {
			filled_count++;
			last_filled_pte = curr_pte;
			// Merging entries is only possible if they fill up
			// the entire level.
			if (gpt_pte_info_get_shifts(&curr_pte.info) !=
			    entry_shifts) {
				can_merge = false;
			}
		}

		if (can_merge) {
			if (i == 0U) {
				first_pte = curr_pte;
			} else {
				size_t offset = (size_t)i << entry_shifts;
				if (!check_ptes_consistent(first_pte, curr_pte,
							   offset)) {
					can_merge = false;
				}
			}
		} else if (filled_count > 1U) {
			break;
		} else {
			// We may still be able to clean, continue iterating.
		}
	}

	if (filled_count <= 1U) {
		// Either the level is empty, or the last filled
		// PTE is the only one in the level.
		write_pte_to_level(root, config, stack, last_filled_pte);
		free_level(config, partition, level);
	} else if (can_merge) {
		// All entries consistent, we can merge into one PTE.
		assert(filled_count == GPT_LEVEL_ENTRIES);
		count_t new_shifts = entry_shifts + GPT_LEVEL_BITS;
		size_t	new_guard  = get_pte_addr(first_pte) >> new_shifts;
		gpt_pte_info_set_guard(&first_pte.info, new_guard);
		gpt_pte_info_set_shifts(&first_pte.info, new_shifts);
		write_pte_to_level(root, config, stack, first_pte);
		free_level(config, partition, level);
	} else {
		// No cases where we can free the level, do nothing.
	}
}

static void
go_up_level(gpt_root_t *root, gpt_config_t config, partition_t *partition,
	    gpt_stack_t *stack, bool write)
{
	assert(stack->depth > 0U);

	gpt_stack_frame_t *frame = get_curr_stack_frame(stack);
	assert(frame != NULL);

	stack->depth--;

	if (write && gpt_frame_info_get_dirty(&frame->info)) {
		// FIXME: Do we need a better heuristic to determine if a
		// clean is required? We could maintain a count of filled
		// entries in each level, but do we want this additional
		// memory consumption?
		count_t shifts = gpt_frame_info_get_shifts(&frame->info);
		try_clean(root, config, partition, stack, frame->level, shifts);
	} else {
		assert(!gpt_frame_info_get_dirty(&frame->info));
	}
}

static gpt_pte_t
get_curr_pte(gpt_root_t *root, gpt_config_t config, partition_t *partition,
	     gpt_stack_t *stack, size_t curr, bool write)
{
	gpt_pte_t pte;

	while (stack->depth > 0U) {
		gpt_stack_frame_t *frame = get_curr_stack_frame(stack);
		assert(frame != NULL);

		count_t shifts = gpt_frame_info_get_shifts(&frame->info);
		size_t	addr   = gpt_frame_info_get_addr(&frame->info);
		assert(curr >= addr);

		index_t idx = (index_t)((curr - addr) >> shifts);
		if (idx < GPT_LEVEL_ENTRIES) {
			gpt_frame_info_set_index(&frame->info, idx);
			pte = load_level_pte(config, frame->level, idx);
			goto out;
		}

		go_up_level(root, config, partition, stack, write);
	}

	assert(stack->depth == 0U);

	pte = load_root_pte(root, config);

out:
	return pte;
}

static void
update_curr_pte(gpt_root_t *root, gpt_config_t config, gpt_stack_t *stack,
		size_t addr, count_t shifts, gpt_type_t type, gpt_value_t value)
{
	gpt_pte_t new_pte = gpt_pte_empty();

	if (type != GPT_TYPE_EMPTY) {
		gpt_pte_info_set_guard(&new_pte.info, addr >> shifts);
		gpt_pte_info_set_shifts(&new_pte.info, shifts);
		gpt_pte_info_set_type(&new_pte.info, type);
		new_pte.value = value;
	}

	write_pte_to_level(root, config, stack, new_pte);
}

static void
split_pte_and_fill_level(gpt_config_t config, gpt_level_t level,
			 gpt_pte_t old_pte, count_t shifts)
{
	size_t	    pte_addr = get_pte_addr(old_pte);
	size_t	    pte_size = util_bit(shifts);
	gpt_type_t  type     = gpt_pte_info_get_type(&old_pte.info);
	gpt_value_t value    = old_pte.value;

	gpt_pte_t new_pte = old_pte;
	gpt_pte_info_set_shifts(&new_pte.info, shifts);

	for (index_t i = 0U; i < GPT_LEVEL_ENTRIES; i++) {
		gpt_pte_info_set_guard(&new_pte.info, pte_addr >> shifts);
		new_pte.value = value;

		store_level_pte(config, level, i, new_pte, true);

		pte_addr += pte_size;

		trigger_gpt_value_add_offset_event(type, &value, pte_size);
	}
}

static error_t
allocate_level(gpt_root_t *root, gpt_config_t config, partition_t *partition,
	       gpt_stack_t *stack, gpt_pte_t old_pte, count_t new_shifts,
	       bool fill)
{
	error_t	    err = OK;
	gpt_level_t level;
	size_t	    alloc_size;
	size_t	    alloc_align;

	if (gpt_config_get_rcu_read(&config)) {
		alloc_size  = sizeof(gpt_level_atomic_t);
		alloc_align = alignof(gpt_level_atomic_t);
	} else {
		alloc_size  = sizeof(gpt_level_non_atomic_t);
		alloc_align = alignof(gpt_level_non_atomic_t);
	}

	void_ptr_result_t alloc_ret =
		partition_alloc(partition, alloc_size, alloc_align);
	if (alloc_ret.e != OK) {
		err = alloc_ret.e;
		goto out;
	}

	if (gpt_config_get_rcu_read(&config)) {
		level.atomic  = (gpt_level_atomic_t *)alloc_ret.r;
		*level.atomic = (gpt_level_atomic_t){ .partition = partition };
	} else {
		level.non_atomic  = (gpt_level_non_atomic_t *)alloc_ret.r;
		*level.non_atomic = (gpt_level_non_atomic_t){ 0 };
	}

	size_t	    addr       = get_pte_addr(old_pte);
	count_t	    old_shifts = gpt_pte_info_get_shifts(&old_pte.info);
	gpt_value_t value      = { .level = level };

	if (fill) {
		assert(old_shifts == new_shifts);
		split_pte_and_fill_level(config, level, old_pte,
					 new_shifts - GPT_LEVEL_BITS);
	} else {
		assert(old_shifts < new_shifts);
		index_t i = get_level_index(new_shifts, addr);
		store_level_pte(config, level, i, old_pte, true);
	}

	update_curr_pte(root, config, stack, addr, new_shifts, GPT_TYPE_LEVEL,
			value);

out:
	return err;
}

static void
free_all_levels(gpt_config_t config, partition_t *partition, gpt_pte_t pte)
{
	gpt_level_t levels[GPT_MAX_LEVELS]    = { get_level_from_pte(pte) };
	index_t	    level_idx[GPT_MAX_LEVELS] = { 0 };

	count_t depth = 1U;
	while (depth > 0U) {
		index_t i = depth - 1U;
		assert(i < GPT_MAX_LEVELS);

		gpt_level_t level = levels[i];
		assert(level.raw != 0U);

		index_t j = level_idx[i];
		if (j == GPT_LEVEL_ENTRIES) {
			free_level(config, partition, level);
			levels[i].raw = 0U;
			level_idx[i]  = 0U;
			depth--;
			continue;
		}

		gpt_pte_t curr_pte = load_level_pte(config, level, j);
		if (gpt_pte_info_get_type(&curr_pte.info) == GPT_TYPE_LEVEL) {
			assert(i < (GPT_MAX_LEVELS - 1U));
			levels[i + 1U] = get_level_from_pte(curr_pte);
			depth++;
		}

		level_idx[i]++;
	}
}

static size_t
update_curr_pte_and_get_size(gpt_root_t *root, gpt_config_t config,
			     gpt_stack_t *stack, size_t curr, size_t rem,
			     gpt_entry_t new)
{
	count_t shifts = get_max_possible_shifts(stack, curr, rem);

	update_curr_pte(root, config, stack, curr, shifts, new.type, new.value);

	return util_bit(shifts);
}

static size_t
get_next_pte_base(gpt_stack_t *stack, size_t curr)
{
	count_t shifts = get_max_entry_shifts(stack);

	return util_p2align_down(curr, shifts) + util_bit(shifts);
}

static size_result_t
handle_write(gpt_root_t *root, gpt_config_t config, partition_t *partition,
	     gpt_stack_t *stack, size_t curr, size_t rem, gpt_entry_t old,
	     gpt_entry_t new, bool match)
{
	size_result_t ret = size_result_ok(0U);
	gpt_pte_t     pte =
		get_curr_pte(root, config, partition, stack, curr, true);
	gpt_type_t type = gpt_pte_info_get_type(&pte.info);

	if (type == GPT_TYPE_EMPTY) {
		// Empty PTEs don't have a valid guard, which is why we can't
		// perform a guard check here.
		if (match && (old.type != GPT_TYPE_EMPTY)) {
			// We expected a non-empty PTE at this point.
			ret.e = ERROR_BUSY;
		} else if (new.type == GPT_TYPE_EMPTY) {
			// No need to update an already empty entry, skip to the
			// next entry.
			ret.r = get_next_pte_base(stack, curr) - curr;
		} else {
			// We can safely update the PTE.
			ret.r = update_curr_pte_and_get_size(
				root, config, stack, curr, rem, new);
		}
	} else if (!guard_matching(pte, curr)) {
		// The current address isn't mapped in the GPT.
		if (!match && can_replace_pte(curr, rem, pte)) {
			// It is safe to overwrite this PTE.
			ret.r = update_curr_pte_and_get_size(
				root, config, stack, curr, rem, new);
			// If the old PTE was a level, we need to ensure it
			// and all levels below it are freed.
			if (type == GPT_TYPE_LEVEL) {
				free_all_levels(config, partition, pte);
			}
		} else if (match && pte_will_conflict(curr, rem, old, pte)) {
			// We either expected the GPT to be filled at the
			// current offset, or to be empty at the PTE's offset.
			ret.e = ERROR_BUSY;
		} else {
			// Allocate a new common level and retry.
			count_t shifts = get_common_shifts(pte, curr);
			ret.e = allocate_level(root, config, partition, stack,
					       pte, shifts, false);
		}
	} else if (type == GPT_TYPE_LEVEL) {
		// Guard matches for this level, traverse down it.
		go_down_level(config, stack, curr, pte);
	} else if (match && !pte_and_entry_equal(pte, curr, old)) {
		// We aren't matching the expected old value.
		ret.e = ERROR_BUSY;
	} else {
		// The PTE has an old value, which we may not be fully
		// replacing. Determine if we need to split the PTE into a new
		// level or not.
		count_t old_shifts = gpt_pte_info_get_shifts(&pte.info);
		count_t new_shifts = get_max_possible_shifts(stack, curr, rem);
		if (old_shifts > new_shifts) {
			assert(old_shifts >= GPT_LEVEL_BITS);
			// Split entry into a new level and retry.
			ret.e = allocate_level(root, config, partition, stack,
					       pte, old_shifts, true);
		} else if ((old_shifts < new_shifts) && match) {
			// The old PTE doesn't cover the entire region that we
			// want to update, which means there is a mismatch.
			ret.e = ERROR_BUSY;
		} else {
			// Either the shifts are matching or we don't care about
			// the old entry, we can safely update the PTE.
			ret.r = update_curr_pte_and_get_size(
				root, config, stack, curr, rem, new);
		}
	}

	return ret;
}

static error_t
do_walk_callback(gpt_read_data_t *data)
{
	error_t err = OK;

	if (data->size > 0U) {
		err = trigger_gpt_walk_callback_event(data->cb, data->entry,
						      data->base, data->size,
						      data->arg);
	}

	return err;
}

static void
log_range(size_t base, size_t size, gpt_entry_t entry)
{
	if ((entry.type != GPT_TYPE_EMPTY) && (size > 0U)) {
		LOG(DEBUG, INFO, "[{:#x}, {:#x}]: type {:d}, value {:#x}", base,
		    size, entry.type, entry.value.raw);
	}
}

static size_result_t
handle_read(gpt_root_t *root, gpt_config_t config, gpt_stack_t *stack,
	    size_t curr, size_t rem, gpt_read_op_t op, gpt_read_data_t *data)
{
	size_result_t ret = size_result_ok(0U);
	gpt_pte_t   pte	 = get_curr_pte(root, config, NULL, stack, curr, false);
	gpt_type_t  type = gpt_pte_info_get_type(&pte.info);
	gpt_value_t value = { .raw = 0U };

	size_t pte_addr = get_pte_addr(pte);
	size_t pte_size = get_pte_size(pte);
	size_t end_addr;

	if (type == GPT_TYPE_EMPTY) {
		// The empty range ends at the next PTE.
		end_addr = get_next_pte_base(stack, curr);
	} else if (!guard_matching(pte, curr)) {
		// The current address isn't mapped in the GPT, so we treat it
		// as empty.
		type = GPT_TYPE_EMPTY;
		if (curr < pte_addr) {
			// The range ends at the start of the PTE.
			end_addr = pte_addr;
		} else {
			// The range ends at the next PTE.
			end_addr = get_next_pte_base(stack, curr);
		}
	} else if (type == GPT_TYPE_LEVEL) {
		go_down_level(config, stack, curr, pte);
		goto out;
	} else {
		end_addr = util_balign_down(curr + pte_size, pte_size);
		value	 = pte.value;

		trigger_gpt_value_add_offset_event(type, &value,
						   curr - pte_addr);
	}

	size_t size = util_min(end_addr - curr, rem);

	gpt_entry_t curr_entry = {
		.type  = type,
		.value = value,
	};

	gpt_entry_t cmp_entry = data->entry;

	trigger_gpt_value_add_offset_event(cmp_entry.type, &cmp_entry.value,
					   data->size);

	bool equal = entries_equal(curr_entry, cmp_entry);
	if (equal) {
		data->size += size;
	} else {
		if (op == GPT_READ_OP_LOOKUP) {
			if (data->base == curr) {
				data->entry = curr_entry;
				data->size  = size;
			} else {
				ret.e = ERROR_FAILURE;
			}
		} else if (op == GPT_READ_OP_IS_CONTIGUOUS) {
			ret.e = ERROR_FAILURE;
		} else if (op == GPT_READ_OP_WALK) {
			ret.e = do_walk_callback(data);
			if (curr_entry.type == cmp_entry.type) {
				data->base  = curr;
				data->size  = size;
				data->entry = curr_entry;
			} else {
				data->base = curr + size;
				data->size = 0U;
			}
		} else if (op == GPT_READ_OP_DUMP_RANGE) {
			log_range(data->base, data->size, data->entry);
			data->entry = curr_entry;
			data->base  = curr;
			data->size  = size;
		} else {
			panic("gpt: Invalid read operation");
		}
	}

	ret.r = size;

out:
	return ret;
}

static size_result_t
gpt_do_write(gpt_t *gpt, size_t base, size_t size, gpt_entry_t old,
	     gpt_entry_t new, bool match)
{
	size_result_t ret	= size_result_ok(0U);
	gpt_root_t   *root	= &gpt->root;
	gpt_config_t  config	= gpt->config;
	partition_t  *partition = gpt->partition;

	gpt_stack_t stack;
	stack.depth = 0U;

	gpt_entry_t x = old;
	gpt_entry_t y = new;

	size_t offset = 0U;
	while ((ret.e == OK) && (offset < size)) {
		ret = handle_write(root, config, partition, &stack,
				   base + offset, size - offset, x, y, match);
		if ((ret.e == OK) && (ret.r != 0U)) {
			offset += ret.r;
			trigger_gpt_value_add_offset_event(x.type, &x.value,
							   ret.r);
			trigger_gpt_value_add_offset_event(y.type, &y.value,
							   ret.r);
		}
	}

	ret.r = offset;

	// Unwind the GPT stack to finish any required cleanup.
	while (stack.depth > 0U) {
		go_up_level(root, config, partition, &stack, true);
	}

	return ret;
}

static error_t
gpt_write(gpt_t *gpt, size_t base, size_t size, gpt_entry_t old,
	  gpt_entry_t new, bool match)
{
	size_result_t ret;

	assert(gpt != NULL);

	if ((size == 0U) || util_add_overflows(base, size - 1U)) {
		ret = size_result_error(ERROR_ARGUMENT_INVALID);
		goto out;
	}

	if ((base + size - 1U) > (get_max_size(gpt->config) - 1U)) {
		ret = size_result_error(ERROR_ARGUMENT_SIZE);
		goto out;
	}

	assert(entry_is_valid_or_empty(gpt, old));
	assert(entry_is_valid_or_empty(gpt, new));

	ret = gpt_do_write(gpt, base, size, old, new, match);
	if ((ret.e != OK) && (ret.r != 0U)) {
		size_result_t revert =
			gpt_do_write(gpt, base, ret.r, new, old, true);
		if (revert.e != OK) {
			panic("gpt: Failed to revert write!");
		}
	}

out:
	return ret.e;
}

static gpt_entry_t
gpt_entry_empty(void)
{
	return (gpt_entry_t){
		.type  = GPT_TYPE_EMPTY,
		.value = { .raw = 0U },
	};
}

static error_t
gpt_read(gpt_t *gpt, size_t base, size_t size, gpt_read_op_t op,
	 gpt_read_data_t *data)
{
	size_result_t ret    = size_result_ok(0U);
	gpt_root_t   *root   = &gpt->root;
	gpt_config_t  config = gpt->config;

	if ((size == 0U) || util_add_overflows(base, size - 1U)) {
		ret.e = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if ((base + size - 1U) > (get_max_size(gpt->config) - 1U)) {
		ret.e = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	assert(entry_is_valid_or_empty(gpt, data->entry));

	gpt_stack_t stack;
	stack.depth = 0U;

	size_t offset = 0U;
	while ((ret.e == OK) && (offset < size)) {
		ret = handle_read(root, config, &stack, base + offset,
				  size - offset, op, data);
		offset += ret.r;
	}

out:
	return ret.e;
}

error_t
gpt_init(gpt_t *gpt, partition_t *partition, gpt_config_t config,
	 register_t allowed_types)
{
	error_t err = OK;

	if (gpt_config_get_max_bits(&config) > GPT_MAX_SIZE_BITS) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (bitmap_isset(&allowed_types, (index_t)GPT_TYPE_EMPTY) ||
	    bitmap_isset(&allowed_types, (index_t)GPT_TYPE_LEVEL) ||
	    ((allowed_types & ~util_mask((index_t)GPT_TYPE__MAX + 1U)) != 0U)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	store_root_pte(&gpt->root, config, gpt_pte_empty(), true);

	gpt->partition	   = object_get_partition_additional(partition);
	gpt->config	   = config;
	gpt->allowed_types = allowed_types;

out:
	return err;
}

void
gpt_destroy(gpt_t *gpt)
{
	gpt_clear_all(gpt);
	object_put_partition(gpt->partition);
}

error_t
gpt_insert(gpt_t *gpt, size_t base, size_t size, gpt_entry_t entry,
	   bool expect_empty)
{
	error_t err;

	if (entry_is_valid(gpt, entry)) {
		err = gpt_write(gpt, base, size, gpt_entry_empty(), entry,
				expect_empty);
	} else {
		err = ERROR_ARGUMENT_INVALID;
	}

	return err;
}

error_t
gpt_update(gpt_t *gpt, size_t base, size_t size, gpt_entry_t old_entry,
	   gpt_entry_t new_entry)
{
	error_t err;

	if (entry_is_valid(gpt, old_entry) && entry_is_valid(gpt, new_entry)) {
		err = gpt_write(gpt, base, size, old_entry, new_entry, true);
	} else {
		err = ERROR_ARGUMENT_INVALID;
	}

	return err;
}

error_t
gpt_remove(gpt_t *gpt, size_t base, size_t size, gpt_entry_t entry)
{
	error_t err;

	if (entry_is_valid(gpt, entry)) {
		err = gpt_write(gpt, base, size, entry, gpt_entry_empty(),
				true);
	} else {
		err = ERROR_ARGUMENT_INVALID;
	}

	return err;
}

error_t
gpt_clear(gpt_t *gpt, size_t base, size_t size)
{
	return gpt_write(gpt, base, size, gpt_entry_empty(), gpt_entry_empty(),
			 false);
}

void
gpt_clear_all(gpt_t *gpt)
{
	error_t err = gpt_clear(gpt, 0U, get_max_size(gpt->config));
	assert(err == OK);
}

bool
gpt_is_empty(gpt_t *gpt)
{
	gpt_pte_t pte = load_root_pte(&gpt->root, gpt->config);

	return gpt_pte_info_get_type(&pte.info) == GPT_TYPE_EMPTY;
}

gpt_lookup_result_t
gpt_lookup(gpt_t *gpt, size_t base, size_t max_size)
{
	gpt_read_data_t read = { .base = base };

	error_t err = gpt_read(gpt, base, max_size, GPT_READ_OP_LOOKUP, &read);
	assert((err == OK) || (err == ERROR_FAILURE));

	return (gpt_lookup_result_t){
		.entry = read.entry,
		.size  = read.size,
	};
}

bool
gpt_is_contiguous(gpt_t *gpt, size_t base, size_t size, gpt_entry_t entry)
{
	bool		ret;
	gpt_read_data_t read = {
		.entry = entry,
	};

	if (entry_is_valid(gpt, entry)) {
		ret = gpt_read(gpt, base, size, GPT_READ_OP_IS_CONTIGUOUS,
			       &read) == OK;
	} else {
		ret = false;
	}

	return ret;
}

#if defined(UNIT_TESTS)
error_t
gpt_walk(gpt_t *gpt, size_t base, size_t size, gpt_type_t type,
	 gpt_callback_t callback, gpt_arg_t arg)
{
	error_t		err;
	gpt_read_data_t read = {
		.entry = { .type = type },
		.cb    = callback,
		.arg   = arg,
	};

	if ((callback < GPT_CALLBACK__MIN) || (callback > GPT_CALLBACK__MAX) ||
	    (callback == GPT_CALLBACK_RESERVED)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (entry_is_valid(gpt, read.entry)) {
		err = gpt_read(gpt, base, size, GPT_READ_OP_WALK, &read);
		if (err == OK) {
			err = do_walk_callback(&read);
		}
	} else {
		err = ERROR_ARGUMENT_INVALID;
	}

out:
	return err;
}
#endif

void
gpt_dump_ranges(gpt_t *gpt)
{
	gpt_read_data_t read = { .base = 0U, .size = 0U };

	LOG(DEBUG, INFO, "Dumping ranges of GPT {:#x}", (register_t)gpt);

	error_t err = gpt_read(gpt, 0U, get_max_size(gpt->config),
			       GPT_READ_OP_DUMP_RANGE, &read);
	assert(err == OK);

	log_range(read.base, read.size, read.entry);
}

void
gpt_dump_levels(gpt_t *gpt)
{
	gpt_root_t  *root   = &gpt->root;
	gpt_config_t config = gpt->config;

	LOG(DEBUG, INFO, "Dumping levels of GPT {:#x}", (register_t)gpt);

	gpt_stack_t stack;
	stack.depth = 0U;

	size_t curr = 0U;
	while (curr < get_max_size(config)) {
		gpt_pte_t pte =
			get_curr_pte(root, config, NULL, &stack, curr, false);
		count_t entry_shifts = get_max_entry_shifts(&stack);

		if (!util_is_p2aligned(curr, entry_shifts)) {
			// We have already logged this PTE, go to the next
			// entry.
			curr = util_p2align_up(curr, entry_shifts);
			continue;
		}

		size_t	    guard  = gpt_pte_info_get_guard(&pte.info);
		count_t	    shifts = gpt_pte_info_get_shifts(&pte.info);
		gpt_type_t  type   = gpt_pte_info_get_type(&pte.info);
		gpt_value_t value  = pte.value;

		LOG(DEBUG, INFO, "{:d} {:#x} {:d} {:d} {:#x}", stack.depth,
		    guard, shifts, type, value.raw);

		if (type == GPT_TYPE_LEVEL) {
			curr = get_pte_addr(pte);
			go_down_level(config, &stack, curr, pte);
		} else {
			curr += util_bit(entry_shifts);
		}
	}
}
