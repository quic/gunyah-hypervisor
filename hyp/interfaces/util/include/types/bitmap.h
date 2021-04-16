// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Bitmap definitions for the type DSL. This is also included by C code to
// get BITMAP_NUM_WORDS().

#define BITMAP_NUM_WORDS(x) (((x) + BITMAP_WORD_BITS - 1) / BITMAP_WORD_BITS)

#if defined(__TYPED_DSL__)
#define BITMAP(bits, ...)                                                      \
	array(BITMAP_NUM_WORDS(bits)) type register_t(__VA_ARGS__)
#endif
