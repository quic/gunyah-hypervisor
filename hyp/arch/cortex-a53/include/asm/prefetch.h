// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Non-temporal accesses perform poorly on Cortex-A53 because they suppress
// allocation in the L1 cache. In most cases it is better to do a regular
// keep-prefetch instead. Note that there is a bit in CPUACTLR that has the
// same effect, and it is set by default at reset on r0p4 and later.
#define prefetch_load_stream(addr)  prefetch_load_keep(addr)
#define prefetch_store_stream(addr) prefetch_store_keep(addr)

#include <asm-generic/prefetch.h>
