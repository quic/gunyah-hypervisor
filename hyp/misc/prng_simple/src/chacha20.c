// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>
#include <string.h>

#include "chacha20.h"

// Subset of ChaCha20 cipher (block generation) for DRBG.
// Implementation based on RFC8439

static const uint32_t chacha20_const[4] = { 0x61707865U, 0x3320646eU,
					    0x79622d32U, 0x6b206574U };

static uint32_t
rotl32(uint32_t x, index_t shift)
{
	return (x << shift) | (x >> (32U - shift));
}

static void Qround(uint32_t (*state)[16], index_t a, index_t b, index_t c,
		   index_t d)
{
	(*state)[a] += (*state)[b];
	(*state)[d] ^= (*state)[a];
	(*state)[d] = rotl32((*state)[d], 16U);
	(*state)[c] += (*state)[d];
	(*state)[b] ^= (*state)[c];
	(*state)[b] = rotl32((*state)[b], 12U);
	(*state)[a] += (*state)[b];
	(*state)[d] ^= (*state)[a];
	(*state)[d] = rotl32((*state)[d], 8U);
	(*state)[c] += (*state)[d];
	(*state)[b] ^= (*state)[c];
	(*state)[b] = rotl32((*state)[b], 7U);
}

static void chacha20_inner_block(uint32_t (*state)[16])
{
	Qround(state, 0U, 4U, 8U, 12U);
	Qround(state, 1U, 5U, 9U, 13U);
	Qround(state, 2U, 6U, 10U, 14U);
	Qround(state, 3U, 7U, 11U, 15U);
	Qround(state, 0U, 5U, 10U, 15U);
	Qround(state, 1U, 6U, 11U, 12U);
	Qround(state, 2U, 7U, 8U, 13U);
	Qround(state, 3U, 4U, 9U, 14U);
}

void
chacha20_block(const uint32_t (*key)[8], uint32_t counter,
	       const uint32_t (*nonce)[3], uint32_t (*out)[16])
{
	count_t i;

	// Setup output with input
	for (i = 0U; i < 4; i++) {
		(*out)[i] = chacha20_const[i];
	}
	for (i = 0U; i < 8; i++) {
		(*out)[i + 4] = (*key)[i];
	}
	(*out)[12] = counter;
	for (i = 0U; i < 3; i++) {
		(*out)[i + 13] = (*nonce)[i];
	}

	// Run 20 rounds (10 column interleaved with 10 diagonal rounds)
	for (i = 0U; i < 10U; i++) {
		chacha20_inner_block(out);
	}

	// Add the original state to the result
	for (i = 0U; i < 4; i++) {
		(*out)[i] += chacha20_const[i];
	}
	for (i = 0U; i < 8; i++) {
		(*out)[4 + i] += (*key)[i];
	}
	(*out)[12] += counter;
	for (i = 0U; i < 3; i++) {
		(*out)[13 + i] += (*nonce)[i];
	}
}
