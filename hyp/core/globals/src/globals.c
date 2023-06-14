// © 2023 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <globals.h>
#include <spinlock.h>

#include "event_handlers.h"

static global_options_t global_options;
static spinlock_t	global_options_lock;

void
globals_handle_boot_cold_init(void)
{
	spinlock_init(&global_options_lock);
}

const global_options_t *
globals_get_options(void)
{
	return &global_options;
}

void
globals_set_options(global_options_t set)
{
	spinlock_acquire(&global_options_lock);
	global_options = global_options_union(global_options, set);
	spinlock_release(&global_options_lock);
}

void
globals_clear_options(global_options_t clear)
{
	spinlock_acquire(&global_options_lock);
	global_options = global_options_difference(global_options, clear);
	spinlock_release(&global_options_lock);
}
