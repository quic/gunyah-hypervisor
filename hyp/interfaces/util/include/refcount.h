// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Simple lock-free reference counting.

// Initialise a reference count, with a single reference held.
void
refcount_init(refcount_t *ref);

// Get a reference, assuming that the count is nonzero. This must only be used
// in cases where the caller already knows that there is at least one reference
// that cannot be concurrently released by another thread, hence the name. No
// memory barrier is implied; adequate barriers should be provided by whatever
// other mechanism is used to guarantee that the count is nonzero, e.g. RCU.
void
refcount_get_additional(refcount_t *ref);

// Get a reference, without assuming that the count is nonzero. The caller must
// check the result; if it is false, the count had already reached zero and the
// reference could not be token. An acquire memory barrier is implied.
bool
refcount_get_safe(refcount_t *ref);

// Release a reference. The caller must check the result; if it is true, the
// count has now reached zero and the caller must take action to free the
// underlying resource. A release memory barrier is implied.
bool
refcount_put(refcount_t *ref);
