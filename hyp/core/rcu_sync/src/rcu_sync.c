// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <compiler.h>
#include <log.h>
#include <object.h>
#include <rcu.h>
#include <scheduler.h>
#include <thread.h>
#include <trace.h>

#include <asm/barrier.h>
#include <asm/event.h>

#include "event_handlers.h"

void
rcu_sync(void)
{
	thread_t *thread = thread_get_self();

	scheduler_lock(thread);
	scheduler_block(thread, SCHEDULER_BLOCK_RCU_SYNC);
	rcu_sync_state_t state = {
		.thread = object_get_thread_additional(thread),
	};
	rcu_enqueue(&state.rcu_entry, RCU_UPDATE_CLASS_SYNC_COMPLETE);

	do {
		scheduler_unlock(thread);
		(void)scheduler_schedule();
		scheduler_lock(thread);
	} while (scheduler_is_blocked(thread, SCHEDULER_BLOCK_RCU_SYNC));

	scheduler_unlock(thread);
}

bool
rcu_sync_killable(void)
{
	thread_t *thread = thread_get_self();
	bool	  killed = false;

	// We use a thread-local sync state here, so it remains valid if we
	// return early and doesn't need to be cancelled (which the RCU API
	// currently does not allow).
	static _Thread_local rcu_sync_state_t state;

	// If the state struct's thread is already set, then an earlier killable
	// sync on this thread has not yet completed. We can't reuse it as that
	// may complete too early, so just fail immediately.
	if (compiler_unexpected(atomic_load_acquire(&state.thread) != NULL)) {
		killed = true;
	} else {
		scheduler_lock(thread);
		scheduler_block(thread, SCHEDULER_BLOCK_RCU_SYNC);
		atomic_store_relaxed(&state.thread,
				     object_get_thread_additional(thread));
		rcu_enqueue(&state.rcu_entry, RCU_UPDATE_CLASS_SYNC_COMPLETE);
		scheduler_unlock(thread);

		(void)scheduler_schedule();

		scheduler_lock(thread);
		if (scheduler_is_blocked(thread, SCHEDULER_BLOCK_RCU_SYNC)) {
			assert(thread_is_dying(thread_get_self()));
			(void)scheduler_unblock(thread,
						SCHEDULER_BLOCK_RCU_SYNC);
			killed = true;
		}
		scheduler_unlock(thread);
	}

	return !killed;
}

rcu_update_status_t
rcu_sync_handle_update(rcu_entry_t *entry)
{
	rcu_update_status_t ret = rcu_update_status_default();

	rcu_sync_state_t *state	 = rcu_sync_state_container_of_rcu_entry(entry);
	thread_t *	  thread = atomic_load_relaxed(&state->thread);

	scheduler_lock(thread);
	assert(scheduler_is_blocked(thread, SCHEDULER_BLOCK_RCU_SYNC));

	if (scheduler_unblock(thread, SCHEDULER_BLOCK_RCU_SYNC)) {
		rcu_update_status_set_need_schedule(&ret, true);
	}
	scheduler_unlock(thread);

	object_put_thread(thread);
	atomic_store_release(&state->thread, NULL);

	return ret;
}
