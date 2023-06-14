// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

// Gunyah compiler assumption sanity checks.
#if ARCH_IS_64BIT
static_assert(SIZE_MAX == UINT64_MAX, "SIZE_MAX smaller than machine width");
#else
#error unsupported
#endif
