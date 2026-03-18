// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef PLOOKUPS_H
#define PLOOKUPS_H

#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>

#include <pthread.h>

// Synchronisation shortcuts
#define lock(mutex)	  assert(pthread_mutex_lock(mutex) == 0)
#define unlock(mutex)	  assert(pthread_mutex_unlock(mutex) == 0)
#define signal(cond)	  assert(pthread_cond_signal(cond) == 0)
#define wait(cond, mutex) assert(pthread_cond_wait(cond, mutex) == 0)

// Atomic shortcuts
#define atomic_load_acquire(p) atomic_load_explicit((p), memory_order_acquire)
#define atomic_store_release(p, v)                                             \
	atomic_store_explicit((p), (v), memory_order_release)

#define plookups_thread_exists(state) atomic_load_acquire(&state->thread_exists)

typedef struct plookups_state {
	pthread_t	thread;
	pthread_mutex_t mutex;
	pthread_cond_t	cond;
	bool		thread_asleep;
	_Atomic bool	thread_exists;
	void	       *args;
} plookups_state_t;

void
plookups_create_thread(plookups_state_t *state, void *args,
		       void *(*thread_fn)(void *));

void
plookups_wake_thread(plookups_state_t *state);

void
plookups_sleep_thread(plookups_state_t *state);

void
plookups_destroy_thread(plookups_state_t *state);

#endif // PLOOKUPS_H
