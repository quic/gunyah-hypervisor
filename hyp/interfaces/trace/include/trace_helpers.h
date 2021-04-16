// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#define TRACE_SET_CLASS(bitmap, trace_class)                                   \
	do {                                                                   \
		(bitmap) |= 1U << (TRACE_CLASS_##trace_class);                 \
	} while (0)

#define TRACE_CLEAR_CLASS(bitmap, trace_class)                                 \
	do {                                                                   \
		(bitmap) &= (~(1U << (TRACE_CLASS_##trace_class)));            \
	} while (0)

#define TRACE_CLASS_ENABLED(bitmap, trace_class)                               \
	((bitmap) & (1U << (TRACE_CLASS_##trace_class)))
