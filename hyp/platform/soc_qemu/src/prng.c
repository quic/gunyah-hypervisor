// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <platform_prng.h>

#include <asm/sysregs.h>

error_t
platform_get_serial(uint32_t data[4])
{
	data[0] = 0U;
	data[1] = 0U;
	data[2] = 0U;
	data[3] = 0U;

	return OK;
}

error_t
platform_get_entropy(platform_prng_data256_t *data)
{
	assert(data != NULL);

	uint64_t prng_data[4] = { 0 };

	sysreg64_read(RNDR, prng_data[0]);
	sysreg64_read(RNDR, prng_data[1]);
	sysreg64_read(RNDR, prng_data[2]);
	sysreg64_read(RNDR, prng_data[3]);

	memscpy(data, sizeof(*data), &prng_data, sizeof(prng_data));

	return OK;
}
