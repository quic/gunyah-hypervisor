// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <util.h>

#define BITMAP_SET_BIT(x) ((register_t)1U << (((x) % BITMAP_WORD_BITS)))
#define BITMAP_WORD(x)	  ((x) / BITMAP_WORD_BITS)
#define BITMAP_SIZE_ASSERT(bitmap, bit)                                        \
	assert((index_t)(compiler_sizeof_object(bitmap) /                      \
			 sizeof(register_t)) > BITMAP_WORD(bit))

bool
bitmap_isset(const register_t *bitmap, index_t bit)
{
	BITMAP_SIZE_ASSERT(bitmap, bit);

	index_t i = BITMAP_WORD(bit);

	return (bitmap[i] & BITMAP_SET_BIT(bit)) != 0U;
}

void
bitmap_set(register_t *bitmap, index_t bit)
{
	BITMAP_SIZE_ASSERT(bitmap, bit);

	index_t i = BITMAP_WORD(bit);

	bitmap[i] |= BITMAP_SET_BIT(bit);
}

void
bitmap_clear(register_t *bitmap, index_t bit)
{
	BITMAP_SIZE_ASSERT(bitmap, bit);

	index_t i = BITMAP_WORD(bit);

	bitmap[i] &= ~BITMAP_SET_BIT(bit);
}

register_t
bitmap_extract(const register_t *bitmap, index_t bit, index_t width)
{
	BITMAP_SIZE_ASSERT(bitmap, bit + width - 1U);
	assert((width <= BITMAP_WORD_BITS) &&
	       (BITMAP_WORD(bit) == BITMAP_WORD(bit + width - 1U)));

	index_t i = BITMAP_WORD(bit);

	return (bitmap[i] >> (bit % BITMAP_WORD_BITS)) & util_mask(width);
}

void
bitmap_insert(register_t *bitmap, index_t bit, index_t width, register_t value)
{
	BITMAP_SIZE_ASSERT(bitmap, bit + width - 1U);
	assert((width <= BITMAP_WORD_BITS) &&
	       (BITMAP_WORD(bit) == BITMAP_WORD(bit + width - 1U)));

	index_t i = BITMAP_WORD(bit);

	bitmap[i] &= ~(util_mask(width) << (bit % BITMAP_WORD_BITS));
	bitmap[i] |= (value & util_mask(width)) << (bit % BITMAP_WORD_BITS);
}

bool
bitmap_ffs(const register_t *bitmap, index_t num_bits, index_t *bit)
{
	assert(num_bits > 0U);
	BITMAP_SIZE_ASSERT(bitmap, num_bits - 1U);

	bool result = false;
	BITMAP_FOREACH_SET_BEGIN(i, bitmap, num_bits)
		result = true;
		*bit   = i;
		break;
	BITMAP_FOREACH_SET_END

	return result;
}

bool
bitmap_ffc(const register_t *bitmap, index_t num_bits, index_t *bit)
{
	assert(num_bits > 0U);
	BITMAP_SIZE_ASSERT(bitmap, num_bits - 1U);

	bool result = false;
	BITMAP_FOREACH_CLEAR_BEGIN(i, bitmap, num_bits)
		result = true;
		*bit   = i;
		break;
	BITMAP_FOREACH_CLEAR_END

	return result;
}

bool
bitmap_empty(const register_t *bitmap, index_t num_bits)
{
	assert(num_bits > 0U);
	BITMAP_SIZE_ASSERT(bitmap, num_bits - 1U);

	index_t i;
	bool	result = true;

	for (i = 0U; i < BITMAP_WORD(num_bits); i++) {
		if (bitmap[i] != 0U) {
			result = false;
			break;
		}
	}

	if ((i + 1U) == BITMAP_NUM_WORDS(num_bits)) {
		if ((bitmap[i] & (BITMAP_SET_BIT(num_bits) - 1U)) != 0U) {
			result = false;
		}
	}

	return result;
}

bool
bitmap_full(const register_t *bitmap, index_t num_bits)
{
	assert(num_bits > 0U);
	BITMAP_SIZE_ASSERT(bitmap, num_bits - 1U);

	index_t i;
	bool	result = true;

	for (i = 0U; i < BITMAP_WORD(num_bits); i++) {
		if (~bitmap[i] != 0U) {
			result = false;
			break;
		}
	}

	if ((i + 1U) == BITMAP_NUM_WORDS(num_bits)) {
		if ((~bitmap[i] & (BITMAP_SET_BIT(num_bits) - 1U)) != 0U) {
			result = false;
		}
	}

	return result;
}

bool
bitmap_atomic_isset(const _Atomic register_t *bitmap, index_t bit,
		    memory_order order)
{
	BITMAP_SIZE_ASSERT(bitmap, bit);

	index_t i = BITMAP_WORD(bit);

	return (atomic_load_explicit(&bitmap[i], order) &
		BITMAP_SET_BIT(bit)) != 0U;
}

bool
bitmap_atomic_test_and_set(_Atomic register_t *bitmap, index_t bit,
			   memory_order order)
{
	BITMAP_SIZE_ASSERT(bitmap, bit);

	index_t	   i   = BITMAP_WORD(bit);
	register_t old = atomic_fetch_or_explicit(&bitmap[i],
						  BITMAP_SET_BIT(bit), order);

	return (old & BITMAP_SET_BIT(bit)) != 0U;
}

bool
bitmap_atomic_test_and_clear(_Atomic register_t *bitmap, index_t bit,
			     memory_order order)
{
	BITMAP_SIZE_ASSERT(bitmap, bit);

	index_t	   i   = BITMAP_WORD(bit);
	register_t old = atomic_fetch_and_explicit(&bitmap[i],
						   ~BITMAP_SET_BIT(bit), order);

	return (old & BITMAP_SET_BIT(bit)) != 0U;
}

bool
bitmap_atomic_ffs(const _Atomic register_t *bitmap, index_t num_bits,
		  index_t *bit)
{
	assert(num_bits > 0U);
	BITMAP_SIZE_ASSERT(bitmap, num_bits - 1U);

	bool result = false;
	BITMAP_ATOMIC_FOREACH_SET_BEGIN(i, bitmap, num_bits)
		result = true;
		*bit   = i;
		break;
	BITMAP_ATOMIC_FOREACH_SET_END

	return result;
}

bool
bitmap_atomic_ffc(const _Atomic register_t *bitmap, index_t num_bits,
		  index_t *bit)
{
	assert(num_bits > 0U);
	BITMAP_SIZE_ASSERT(bitmap, num_bits - 1U);

	bool result = false;
	BITMAP_ATOMIC_FOREACH_CLEAR_BEGIN(i, bitmap, num_bits)
		result = true;
		*bit   = i;
		break;
	BITMAP_ATOMIC_FOREACH_CLEAR_END

	return result;
}

bool
bitmap_atomic_empty(const _Atomic register_t *bitmap, index_t num_bits)
{
	assert(num_bits > 0U);
	BITMAP_SIZE_ASSERT(bitmap, num_bits - 1U);

	index_t i;
	bool	result = true;

	for (i = 0U; i < BITMAP_WORD(num_bits); i++) {
		if (atomic_load_relaxed(&bitmap[i]) != 0U) {
			result = false;
			break;
		}
	}

	if ((i + 1U) == BITMAP_NUM_WORDS(num_bits)) {
		if ((atomic_load_relaxed(&bitmap[i]) &
		     (BITMAP_SET_BIT(num_bits) - 1U)) != 0U) {
			result = false;
		}
	}

	return result;
}

bool
bitmap_atomic_full(const _Atomic register_t *bitmap, index_t num_bits)
{
	assert(num_bits > 0U);
	BITMAP_SIZE_ASSERT(bitmap, num_bits - 1U);

	index_t i;
	bool	result = true;

	for (i = 0U; i < BITMAP_WORD(num_bits); i++) {
		if (~atomic_load_relaxed(&bitmap[i]) != 0U) {
			result = false;
			break;
		}
	}

	if ((i + 1U) == BITMAP_NUM_WORDS(num_bits)) {
		if ((~atomic_load_relaxed(&bitmap[i]) &
		     (BITMAP_SET_BIT(num_bits) - 1U)) != 0U) {
			result = false;
		}
	}

	return result;
}

register_t
bitmap_atomic_extract(const _Atomic register_t *bitmap, index_t bit,
		      index_t width, memory_order order)
{
	BITMAP_SIZE_ASSERT(bitmap, bit + width - 1U);
	assert((width <= BITMAP_WORD_BITS) &&
	       (BITMAP_WORD(bit) == BITMAP_WORD(bit + width - 1U)));

	index_t i = BITMAP_WORD(bit);

	return (atomic_load_explicit(&bitmap[i], order) >>
		(bit % BITMAP_WORD_BITS)) &
	       util_mask(width);
}

void
bitmap_atomic_insert(_Atomic register_t *bitmap, index_t bit, index_t width,
		     register_t value, memory_order order)
{
	BITMAP_SIZE_ASSERT(bitmap, bit + width - 1U);
	assert((width <= BITMAP_WORD_BITS) &&
	       (BITMAP_WORD(bit) == BITMAP_WORD(bit + width - 1U)));

	index_t i = BITMAP_WORD(bit);

	memory_order load_order =
		(order == memory_order_release)	  ? memory_order_relaxed
		: (order == memory_order_acq_rel) ? memory_order_acquire
						  : order;

	register_t old_word = atomic_load_explicit(&bitmap[i], load_order),
		   new_word;
	do {
		new_word = (old_word &
			    ~(util_mask(width) << (bit % BITMAP_WORD_BITS))) |
			   ((value & util_mask(width))
			    << (bit % BITMAP_WORD_BITS));
	} while (!atomic_compare_exchange_weak_explicit(
		&bitmap[i], &old_word, new_word, order, load_order));
}
