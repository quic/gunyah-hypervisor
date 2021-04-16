// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// More concise aliases for common atomic operations.

#include <asm/atomic.h>

// Shortcuts for load-relaxed and load-acquire
#define atomic_load_relaxed(p) atomic_load_explicit((p), memory_order_relaxed)
#define atomic_load_acquire(p) atomic_load_explicit((p), memory_order_acquire)

// Atomic load-consume.
//
// Perform a load-consume, with the semantics it should have rather than the
// semantics it is defined to have in the standard. On most CPUs, this is just
// a relaxed atomic load (assuming that volatile has the new semantics specified
// in C18, as it does in virtually every C implementation ever).
#if !defined(atomic_load_consume)
#define atomic_load_consume(p) atomic_load_explicit(p, memory_order_relaxed)
#endif

// Shortcuts for store-relaxed and store-release
#define atomic_store_relaxed(p, v)                                             \
	atomic_store_explicit((p), (v), memory_order_relaxed)
#define atomic_store_release(p, v)                                             \
	atomic_store_explicit((p), (v), memory_order_release)

// Device memory fences
//
// A fence affecting device accesses may need to use a stronger barrier
// instruction compared to a fence affecting only CPU threads. This macro
// may be redefined by <asm/atomic.h> to use stronger instructions if necessary.
#if !defined(atomic_device_fence)
#define atomic_device_fence(o) atomic_thread_fence(o)
#endif
