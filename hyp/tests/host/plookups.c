// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include "plookups.h"

void
plookups_create_thread(plookups_state_t *state, void *args,
		       void *(*thread_fn)(void *))
{
	*state = (plookups_state_t){
		.mutex = PTHREAD_MUTEX_INITIALIZER,
		.cond  = PTHREAD_COND_INITIALIZER,
		.args  = args,
		// Threads start asleep until they're woken by the test running
		// on the main thread
		.thread_asleep = true,
	};
	atomic_store_release(&state->thread_exists, true);
	assert(pthread_create(&state->thread, NULL, thread_fn, state) == 0);
}

void
plookups_wake_thread(plookups_state_t *state)
{
	assert(state);
	lock(&state->mutex);
	state->thread_asleep = false;
	unlock(&state->mutex);
	signal(&state->cond);
}

void
plookups_sleep_thread(plookups_state_t *state)
{
	assert(state);
	lock(&state->mutex);
	state->thread_asleep = true;
	unlock(&state->mutex);
}

void
plookups_destroy_thread(plookups_state_t *state)
{
	atomic_store_release(&state->thread_exists, false);
	// Threads might be asleep so we wake them to break them out of their
	// loop
	plookups_wake_thread(state);
	assert(pthread_join(state->thread, NULL) == 0);
	assert(pthread_mutex_destroy(&state->mutex) == 0);
	assert(pthread_cond_destroy(&state->cond) == 0);
}
