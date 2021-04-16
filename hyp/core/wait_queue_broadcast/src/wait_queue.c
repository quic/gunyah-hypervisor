// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <list.h>
#include <preempt.h>
#include <scheduler.h>
#include <spinlock.h>
#include <thread.h>
#include <wait_queue.h>

void
wait_queue_init(wait_queue_t *wait_queue)
{
	assert(wait_queue != NULL);

	spinlock_init(&wait_queue->lock);

	list_init(&wait_queue->list);
}

void
wait_queue_prepare(wait_queue_t *wait_queue)
{
	thread_t *   self = thread_get_self();
	list_node_t *node = &self->wait_queue_list_node;

	assert(wait_queue != NULL);

	assert_preempt_enabled();
	preempt_disable();

	spinlock_acquire(&wait_queue->lock);

	// Insert at back
	list_insert_at_tail(&wait_queue->list, node);

	spinlock_release(&wait_queue->lock);
}

void
wait_queue_finish(wait_queue_t *wait_queue)
{
	thread_t *self = thread_get_self();

	assert(wait_queue != NULL);

	spinlock_acquire(&wait_queue->lock);

	// Dequeue the thread
	list_node_t *node = &self->wait_queue_list_node;

	list_delete_node(&wait_queue->list, node);

	spinlock_release(&wait_queue->lock);

	preempt_enable();
}

void
wait_queue_get(void)
{
	thread_t *self = thread_get_self();
	assert_preempt_disabled();

	scheduler_lock(self);
	assert(!scheduler_is_blocked(self, SCHEDULER_BLOCK_WAIT_QUEUE));
	scheduler_block(self, SCHEDULER_BLOCK_WAIT_QUEUE);
	scheduler_unlock(self);

	atomic_thread_fence(memory_order_seq_cst);
}

void
wait_queue_put(void)
{
	thread_t *self = thread_get_self();
	assert_preempt_disabled();

	scheduler_lock(self);
	scheduler_unblock(self, SCHEDULER_BLOCK_WAIT_QUEUE);
	scheduler_unlock(self);
}

void
wait_queue_wait(void)
{
	scheduler_yield();
	// returns when unblocked and scheduled again.
}

void
wait_queue_wakeup(wait_queue_t *wait_queue)
{
	assert(wait_queue != NULL);
	bool wakeup_any = false;

	// Order memory with respect to wait_queue_get()
	atomic_thread_fence(memory_order_seq_cst);

	spinlock_acquire(&wait_queue->lock);

	// Wakeup all waiters
	thread_t *thread;
	list_t *  list = &wait_queue->list;

	list_foreach_container (thread, list, thread, wait_queue_list_node) {
		scheduler_lock(thread);
		if (scheduler_unblock(thread, SCHEDULER_BLOCK_WAIT_QUEUE)) {
			wakeup_any = true;
		}
		scheduler_unlock(thread);
	}

	spinlock_release(&wait_queue->lock);

	if (wakeup_any) {
		scheduler_trigger();
	}
}
