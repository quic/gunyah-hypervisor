// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcall_def.h>

#include <platform_prng.h>
#include <platform_timer.h>
#include <thread.h>
#include <util.h>

hypercall_prng_get_entropy_result_t
hypercall_prng_get_entropy(count_t num_bytes)
{
	hypercall_prng_get_entropy_result_t ret = { 0 };

	if ((num_bytes == 0) || (num_bytes > sizeof(uint32_t) * 4)) {
		ret.error = ERROR_ARGUMENT_SIZE;
		goto out;
	}
	if (!util_is_baligned(num_bytes, sizeof(uint32_t))) {
		ret.error = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}

	ret.error = OK;

	thread_t *thread = thread_get_self();
	ticks_t	  now	 = platform_timer_get_current_ticks();

	// The bottom two bits encode the number reads per window, to permit up
	// to 128*4 (512-bits) to be read within the rate-limit window.
	ticks_t last_read = thread->prng_last_read & ~util_mask(2);
	assert(now >= last_read);

	count_t read_count = thread->prng_last_read & util_mask(2);

	// Read rate-limit window is 33ms per thread to reduce DoS.
	if ((now - last_read) < platform_convert_ns_to_ticks(33000000U)) {
		if (read_count == util_mask(2)) {
			ret.error = ERROR_BUSY;
			goto out;
		}
		read_count++;
		thread->prng_last_read = last_read | read_count;
	} else {
		thread->prng_last_read = now & ~util_mask(2);
	}

	if (num_bytes >= sizeof(uint32_t)) {
		error_t err = platform_get_random32(&ret.data0);

		if (err != OK) {
			ret.error = err;
			goto out;
		}
	}
	if (num_bytes >= (2 * sizeof(uint32_t))) {
		error_t err = platform_get_random32(&ret.data1);

		if (err != OK) {
			ret.error = err;
			goto out;
		}
	}
	if (num_bytes >= (3 * sizeof(uint32_t))) {
		error_t err = platform_get_random32(&ret.data2);

		if (err != OK) {
			ret.error = err;
			goto out;
		}
	}
	if (num_bytes >= (4 * sizeof(uint32_t))) {
		error_t err = platform_get_random32(&ret.data3);

		if (err != OK) {
			ret.error = err;
			goto out;
		}
	}
out:
	// On any error, don't return any data
	if (ret.error != OK) {
		ret.data0 = 0U;
		ret.data1 = 0U;
		ret.data2 = 0U;
		ret.data3 = 0U;
	}
	return ret;
}
