// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <platform_prng.h>

error_t
platform_get_serial(uint32_t data[4])
{
	data[0] = 0U;
	data[1] = 0U;
	data[2] = 0U;
	data[3] = 0U;

	return OK;
}
