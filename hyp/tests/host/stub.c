// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#define timer_t hyp_timer_t
#include <hyptypes.h>
#undef timer_t

#define register_t std_register_t
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>

#include <pthread.h>
#undef register_t

#include "mem_manager.h"
#include "stub.h"

#if !defined(NDEBUG)
noreturn void
assert_failed(const char *file, int line, const char *func, const char *err)
{
	(void)file;
	(void)line;
	(void)func;
	(void)err;
	abort();
}
#endif

void
panic(const char *msg)
{
	printf("PANIC: %s\n", msg);
	exit(-1);
}

// =========================================================================
// RCU handling stubs
// =========================================================================

void
rcu_read_start(void) LOCK_IMPL
{
}

void
rcu_read_finish(void) LOCK_IMPL
{
}

// =========================================================================
// Spinlock handling stubs
// =========================================================================

pthread_spinlock_t pthread_spinlocks[MAX_NR_SPINLOCKS];
spinlock_t	  *gunyah_spinlocks[MAX_NR_SPINLOCKS];
size_t		   nr_spinlocks;

static int
spinlocks_find_index(spinlock_t *spinlocks[], size_t spinlocks_size,
		     spinlock_t *target)
{
	for (size_t i = 0; i < spinlocks_size; ++i) {
		if (spinlocks[i] == target) {
			return i;
		}
	}
	return -1;
}

void
spinlock_init(spinlock_t *spinlock)
{
	if (spinlock == NULL) {
		panic("NULL spinlock in init");
	}
	if (spinlocks_find_index(gunyah_spinlocks, MAX_NR_SPINLOCKS, spinlock) <
	    0) {
		if (nr_spinlocks >= MAX_NR_SPINLOCKS) {
			panic("Insufficient number of stub spinlocks for testing - increase MAX_NR_SPINLOCKS");
		}
		gunyah_spinlocks[nr_spinlocks] = spinlock;
		if (pthread_spin_init(&pthread_spinlocks[nr_spinlocks], 0) !=
		    0) {
			panic("Failed to initialize stub spinlock");
		}
		++nr_spinlocks;
	}
}

void
spinlock_acquire(spinlock_t *spinlock) LOCK_IMPL
{
	if (spinlock == NULL) {
		panic("NULL spinlock in acquire");
	}
	const int idx = spinlocks_find_index(gunyah_spinlocks, MAX_NR_SPINLOCKS,
					     spinlock);
	if (idx < 0 || idx >= MAX_NR_SPINLOCKS) {
		panic("Invalid spinlock index in acquire");
	}
	if (pthread_spin_lock(&pthread_spinlocks[idx]) != 0) {
		panic("Failed to acquire stub spinlock");
	}
}

void
spinlock_acquire_nopreempt(spinlock_t *spinlock) LOCK_IMPL
{
	spinlock_acquire(spinlock);
}

void
spinlock_release(spinlock_t *spinlock) LOCK_IMPL
{
	if (spinlock == NULL) {
		panic("NULL spinlock in release");
	}
	const int idx = spinlocks_find_index(gunyah_spinlocks, MAX_NR_SPINLOCKS,
					     spinlock);
	if (idx < 0 || idx >= MAX_NR_SPINLOCKS) {
		panic("Invalid spinlock index in release");
	}
	if (pthread_spin_unlock(&pthread_spinlocks[idx]) != 0) {
		panic("Failed to release stub spinlock");
	}
}

void
spinlock_release_nopreempt(spinlock_t *spinlock) LOCK_IMPL
{
	spinlock_release(spinlock);
}
