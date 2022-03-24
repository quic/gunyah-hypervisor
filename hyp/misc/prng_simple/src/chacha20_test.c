// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(UNIT_TESTS)
#include <hyptypes.h>

#include <compiler.h>
#include <log.h>
#include <trace.h>

#include "chacha20.h"
#include "event_handlers.h"

bool
tests_chacha20_start(void)
{
	// Test vectors from RFC 8439
	static const uint32_t key[8] = { 0x03020100U, 0x07060504U, 0x0b0a0908U,
					 0x0f0e0d0cU, 0x13121110U, 0x17161514U,
					 0x1b1a1918U, 0x1f1e1d1cU };
	static const uint32_t nonce[3] = { 0x09000000U, 0x4a000000U,
					   0x00000000U };
	uint32_t	      counter  = 1U;

	uint32_t out[16];

	uint32_t i;

	chacha20_block(&key, counter, &nonce, &out);

	static const uint32_t expected[16] = {
		0xe4e7f110U, 0x15593bd1U, 0x1fdd0f50U, 0xc47120a3U,
		0xc7f4d1c7U, 0x0368c033U, 0x9aaa2204U, 0x4e6cd4c3U,
		0x466482d2U, 0x09aa9f07U, 0x05d7c214U, 0xa2028bd9U,
		0xd19c12b5U, 0xb94e16deU, 0xe883d0cbU, 0x4e3c50a2
	};

	bool fail = false;

	for (i = 0U; i < 16U; i++) {
		if (out[i] != expected[i]) {
			fail = true;
			break;
		}
	}

	if (fail) {
		LOG(ERROR, INFO, "chacha20 self-test failed");
	}

	return fail;
}

#else

extern char unused;

#endif
