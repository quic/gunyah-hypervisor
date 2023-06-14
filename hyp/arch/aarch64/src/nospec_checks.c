// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <asm/nospec_checks.h>

index_result_t
nospec_range_check(index_t val, index_t limit)
{
	index_result_t result = index_result_error(ERROR_ARGUMENT_INVALID);
	bool	       valid;

	__asm__ volatile("cmp %w[val], %w[limit];"
			 "cset %w[valid], lo;"
			 "csel %w[result_r], %w[val], wzr, lo;"
			 "csdb;"
			 : [valid] "=&r"(valid), [result_r] "=r"(result.r)
			 : [val] "r"(val), [limit] "r"(limit)
			 : "cc");

	if (valid) {
		result.e = OK;
	}

	return result;
}
