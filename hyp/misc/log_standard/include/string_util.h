// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// snprint produces null terminated output string, of maximum length not
// exceeding size characters (including the terminating null).
//
// Unlike the C printf family of functions, the format string is however a
// python format string like syntax.
// For example  "This is a hex value: {:x}" will print the hex representation
// of the corresponding argument.
//
// Not all python format string syntax is implemented, including positional
// arguments. The following approximate python format string syntax is
// accepted.
//	[[fill]align][sign][#][0][minimumwidth][.precision][type]
//
// This function returns the count of bytes written, up to a maximum of size-1.
// A return value of size or larger indicates that the output was truncated.
size_result_t
snprint(char *str, size_t size, const char *format, register_t arg0,
	register_t arg1, register_t arg2, register_t arg3, register_t arg4);
