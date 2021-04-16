// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Block function of the chacha20 cipher
void
chacha20_block(const uint32_t (*key)[8], uint32_t counter,
	       const uint32_t (*nonce)[3], uint32_t (*out)[16]);
