// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <stdbool.h>

#include <atomic.h>
#include <compiler.h>
#include <refcount.h>

// Initialise a reference count, with a single reference held.
void
refcount_init(refcount_t *ref)
{
	atomic_init(&ref->count, 1);
}

// Get a reference, assuming that the count is nonzero. This must only be used
// in cases where the the caller already knows that there is at least one
// reference that cannot be concurrently released by another thread, hence the
// name. No memory barrier is implied; adequate barriers should be provided by
// whatever other mechanism is used to guarantee that the count is nonzero,
// e.g. RCU.
void
refcount_get_additional(refcount_t *ref)
{
	uint32_t count =
		atomic_fetch_add_explicit(&ref->count, 1, memory_order_relaxed);

	assert(count > 0U);
	(void)count;
}

// Get a reference, without assuming that the count is nonzero. The caller
// must check the result; if it is false, the count had already reached zero
// and the reference could not be token. An acquire memory barrier is implied.
bool
refcount_get_safe(refcount_t *ref)
{
	uint32_t count	 = atomic_load_relaxed(&ref->count);
	bool	 success = false;

	while (count > 0U) {
		assert(count < (uint32_t)UINT32_MAX);
		if (atomic_compare_exchange_weak_explicit(
			    &ref->count, &count, count + 1U,
			    memory_order_acquire, memory_order_relaxed)) {
			success = true;
			break;
		}
	}

	return success;
}

// Release a reference. The caller must check the result; if it is true, the
// count has now reached zero and the caller must take action to free the
// underlying resource. This is always a release operation. If this reduces the
// count to zero (and returns true), it is also an acquire operation.
bool
refcount_put(refcount_t *ref)
{
	uint32_t count = atomic_fetch_sub_explicit(&ref->count, 1U,
						   memory_order_release);
	assert(count > 0U);
	if (compiler_expected(count > 1U)) {
		return false;
	} else {
		atomic_thread_fence(memory_order_acquire);
		return true;
	}
}
