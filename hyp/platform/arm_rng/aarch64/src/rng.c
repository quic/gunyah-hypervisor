// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypregisters.h>

#include <atomic.h>
#include <attributes.h>
#include <cpulocal.h>
#include <platform_prng.h>
#include <preempt.h>

#include <asm/barrier.h>

#if !defined(ARCH_ARM_FEAT_RNG) || !ARCH_ARM_FEAT_RNG
#error ARCH_ARM_FEAT_RNG not set
#endif

// We use a per-cpu counter in case the implementation is not shared, and we
// need to ensure reseeding occurs on each core. If the prng HW is shared,
// then the worst case reseeding interval is 32*(N cores).
CPULOCAL_DECLARE_STATIC(count_t, rng_reseed_count);

error_t NOINLINE
platform_get_entropy(platform_prng_data256_t *data)
{
	error_t	 ret	      = ERROR_FAILURE;
	count_t	 i	      = 0U;
	count_t	 retries      = 64;
	uint64_t prng_data[4] = { 0 };

	assert(data != NULL);

	do {
		bool	 ok;
		uint64_t res;
		__asm__ volatile("mrs	%[res], RNDR	;"
				 "cset	%w[ok], ne	;"
				 : [res] "=r"(res), [ok] "=r"(ok)::"cc");
		if (ok) {
			prng_data[i] = res;
			i++;
		} else {
			retries--;
		}
	} while ((i < 4U) && (retries != 0U));

	if (i == 4U) {
		(void)memscpy(data, sizeof(*data), &prng_data,
			      sizeof(prng_data));
		ret = OK;

		// Issue a reseed read, ignoring the result.
		uint64_t tmp = register_RNDRRS_read_ordered(&asm_ordering);
		(void)tmp;
	}

	return ret;
}

error_t NOINLINE
platform_get_random32(uint32_t *data)
{
	error_t	 ret	 = ERROR_BUSY;
	count_t	 retries = 16;
	uint64_t res;
	bool	 ok = false;

	assert(data != NULL);

	cpulocal_begin();

	do {
		__asm__ volatile("mrs	%[res], RNDR	;"
				 "cset	%w[ok], ne	;"
				 : [res] "=r"(res), [ok] "+r"(ok)::"cc");
		retries--;
	} while ((!ok) && (retries != 0U));

	if (ok) {
		*data = (uint32_t)res;
		ret   = OK;

		count_t count = CPULOCAL(rng_reseed_count)++;

		if ((count % 32) == 0U) {
			// Issue a reseed read, ignoring the result.
			uint64_t tmp =
				register_RNDRRS_read_ordered(&asm_ordering);
			(void)tmp;
		}
	}
	cpulocal_end();

	return ret;
}

error_t
platform_get_rng_uuid(uint32_t data[4])
{
	// Gunyah generic RNDR - ARM TRNG interface UUID
	data[0] = 0x45546e21U;
	data[1] = 0x92a1433dU;
	data[2] = 0xa2ea5fe2U;
	data[3] = 0x16397d4eU;
	return OK;
}
