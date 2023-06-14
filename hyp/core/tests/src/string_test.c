// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <hypconstants.h>

#include <atomic.h>
#include <compiler.h>
#include <cpu.h>
#include <log.h>
#include <trace.h>

#include <events/tests.h>

static uint8_t test_data_buff[512], test_buff[2048];

#define CPU_MEMCPY_STRIDE 256

void
memmove_test(void)
{
	uint16_t i;
	uint8_t	 b = 0;

	for (i = 0; i < sizeof(test_data_buff); ++i) {
		test_data_buff[i] = b;
		test_buff[i]	  = b;

		++b;
		if (b == 251) {
			b = 0;
		}
	}

	uint8_t *test_src, *test_dst;

	test_src = test_buff;
	test_dst = test_src + CPU_MEMCPY_STRIDE + 1;

	memmove(test_dst, test_src, CPU_MEMCPY_STRIDE + 13);

	for (i = 0; i < (CPU_MEMCPY_STRIDE + 13); ++i) {
		if (test_dst[i] != test_data_buff[i]) {
			LOG(ERROR, WARN, "Err: pos {:d}, exp {:d}, act,{:d}\n",
			    i, test_dst[i], test_data_buff[i]);
		}
	}
}
