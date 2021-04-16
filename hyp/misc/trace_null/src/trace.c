// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <trace.h>

trace_control_t trace_control;

void
trace_set_class_flags(register_t flags)
{
	(void)flags;
}

void
trace_clear_class_flags(register_t flags)
{
	(void)flags;
}

register_t
trace_get_class_flags()
{
	return (register_t)0;
}
