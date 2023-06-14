// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#define ENUM_FOREACH(name, i)                                                  \
	static_assert(name##__MIN >= 0, "negative enum");                      \
	static_assert(name##__MAX >= name##__MIN, "invalid enum");             \
	for (index_t i = (index_t)name##__MIN; i <= (index_t)name##__MAX; i++)
