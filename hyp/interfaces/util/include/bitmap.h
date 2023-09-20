// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <limits.h>

#include <types/bitmap.h>

#define BITMAP_DECLARE(bits, name)     register_t name[BITMAP_NUM_WORDS(bits)]
#define BITMAP_DECLARE_PTR(bits, name) register_t(*name)[BITMAP_NUM_WORDS(bits)]

bool
bitmap_isset(const register_t *bitmap, index_t bit);

void
bitmap_set(register_t *bitmap, index_t bit);

void
bitmap_clear(register_t *bitmap, index_t bit);

register_t
bitmap_extract(const register_t *bitmap, index_t bit, index_t width);

void
bitmap_insert(register_t *bitmap, index_t bit, index_t width, register_t value);

bool
bitmap_ffs(const register_t *bitmap, index_t num_bits, index_t *bit);

bool
bitmap_ffc(const register_t *bitmap, index_t num_bits, index_t *bit);

bool
bitmap_empty(const register_t *bitmap, index_t num_bits);

bool
bitmap_full(const register_t *bitmap, index_t num_bits);

bool
bitmap_atomic_isset(const _Atomic register_t *bitmap, index_t bit,
		    memory_order order);

bool
bitmap_atomic_test_and_set(_Atomic register_t *bitmap, index_t bit,
			   memory_order order);

#define bitmap_atomic_set(bitmap, bit, order)                                  \
	(void)bitmap_atomic_test_and_set((bitmap), (bit), (order))

bool
bitmap_atomic_test_and_clear(_Atomic register_t *bitmap, index_t bit,
			     memory_order order);

#define bitmap_atomic_clear(bitmap, bit, order)                                \
	(void)bitmap_atomic_test_and_clear((bitmap), (bit), (order))

bool
bitmap_atomic_ffs(const _Atomic register_t *bitmap, index_t num_bits,
		  index_t *bit);

bool
bitmap_atomic_ffc(const _Atomic register_t *bitmap, index_t num_bits,
		  index_t *bit);

bool
bitmap_atomic_empty(const _Atomic register_t *bitmap, index_t num_bits);

bool
bitmap_atomic_full(const _Atomic register_t *bitmap, index_t num_bits);

register_t
bitmap_atomic_extract(const _Atomic register_t *bitmap, index_t bit,
		      index_t width, memory_order order);

void
bitmap_atomic_insert(_Atomic register_t *bitmap, index_t bit, index_t width,
		     register_t value, memory_order order);

static inline register_t
bitmap__get_word(const register_t *bitmap, index_t word)
{
	return bitmap[word];
}

static inline register_t
bitmap__atomic_get_word(const _Atomic register_t *bitmap, index_t word)
{
	return atomic_load_explicit(&bitmap[word], memory_order_relaxed);
}

// Loop macros for iterating over bitmaps. Note that these are written
// to avoid using break statements, so the body provided by the caller
// can use a break or goto statement without breaking MISRA rule 15.4.
#define BITMAP__FOREACH_BEGIN(i, w, r, b, g, n)                                \
	{                                                                      \
		index_t	   w = 0;                                              \
		register_t r = 0;                                              \
		while ((r != 0U) || ((w * BITMAP_WORD_BITS) < n)) {            \
			if (r == 0U) {                                         \
				r = g((b), (w));                               \
				w++;                                           \
			}                                                      \
			if (r != 0U) {                                         \
				index_t i = compiler_ctz(r);                   \
				r &= ~(register_t)1 << i;                      \
				i += ((w - 1U) * BITMAP_WORD_BITS);            \
				if (i >= n) {                                  \
					r = 0;                                 \
				} else {
// clang-format off
#define BITMAP__FOREACH_END }}}}
// clang-format on

#define BITMAP_FOREACH_SET_BEGIN(i, b, n)                                      \
	BITMAP__FOREACH_BEGIN(i, util_cpp_unique_ident(w),                     \
			      util_cpp_unique_ident(r), b, bitmap__get_word,   \
			      n)
#define BITMAP_FOREACH_SET_END BITMAP__FOREACH_END

#define BITMAP_FOREACH_CLEAR_BEGIN(i, b, n)                                    \
	BITMAP__FOREACH_BEGIN(i, util_cpp_unique_ident(w),                     \
			      util_cpp_unique_ident(r), b, ~bitmap__get_word,  \
			      n)
#define BITMAP_FOREACH_CLEAR_END BITMAP__FOREACH_END

#define BITMAP_ATOMIC_FOREACH_SET_BEGIN(i, b, n)                               \
	BITMAP__FOREACH_BEGIN(i, util_cpp_unique_ident(w),                     \
			      util_cpp_unique_ident(r), b,                     \
			      bitmap__atomic_get_word, n)
#define BITMAP_ATOMIC_FOREACH_SET_END BITMAP__FOREACH_END

#define BITMAP_ATOMIC_FOREACH_CLEAR_BEGIN(i, b, n)                             \
	BITMAP__FOREACH_BEGIN(i, util_cpp_unique_ident(w),                     \
			      util_cpp_unique_ident(r), b,                     \
			      ~bitmap__atomic_get_word, n)
#define BITMAP_ATOMIC_FOREACH_CLEAR_END BITMAP__FOREACH_END
