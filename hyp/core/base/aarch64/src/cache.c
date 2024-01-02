// © 2023 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <util.h>

#include <asm/cache.h>
#include <asm/cpu.h>

void
cache_clean_range(const void *data, size_t size)
{
	CACHE_CLEAN_RANGE(data, size);
}
