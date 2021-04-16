// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Prefetch operations.
//
// Calling these macros may cause the compiler to generate hint instructions
// or otherwise reorder operations so that values are fetched by the CPU
// eagerly when it is known that they will be needed in the near future.
//
// Prefetch instructions, where available, can typically distinguish between
// addresses that will be loaded or stored; this distinction is useful on
// cache-coherent multiprocessor systems, where a store prefetch will try to
// bring the target cache line into an exclusive state.
//
// On some architectures, including ARMv8, the prefetch instructions can
// further distinguish between temporal and non-temporal accesses. A temporal,
// or "keep", prefetch means that the address will be accessed repeatedly and
// should be kept in the cache (i.e. the default behaviour of most caches). A
// non-temporal, or "stream", prefetch means that the address will be accessed
// only once, so cache allocations for it should be kept to a minimum - e.g.
// bypassing outer caches on eviction from the innermost cache.
//
// This is in an asm header to allow the macros to be replaced with asm
// directives or no-ops for targets where the compiler makes a suboptimal
// decision by default, typically because the target CPU has a broken
// implementation of the prefetch instructions.

#ifndef prefetch_load_keep
#define prefetch_load_keep(addr) __builtin_prefetch(addr, 0, 3)
#endif

#ifndef prefetch_store_keep
#define prefetch_store_keep(addr) __builtin_prefetch(addr, 1, 3)
#endif

#ifndef prefetch_load_stream
#define prefetch_load_stream(addr) __builtin_prefetch(addr, 0, 0)
#endif

#ifndef prefetch_store_stream
#define prefetch_store_stream(addr) __builtin_prefetch(addr, 1, 0)
#endif
