// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcontainers.h>

#include <scheduler.h>
#include <spinlock.h>
#include <vic.h>
#include <virq.h>

#include "event_handlers.h"
#include "msgqueue_common.h"
#include "useraccess.h"

bool_result_t
msgqueue_send_msg(msgqueue_t *msgqueue, size_t size, kernel_or_gvaddr_t msg,
		  bool push, bool from_kernel)
{
	bool_result_t ret;

	ret.r = true;
	ret.e = OK;

	assert(msgqueue != NULL);

	spinlock_acquire(&msgqueue->lock);

	if (msgqueue->count == msgqueue->queue_depth) {
		ret.e = ERROR_MSGQUEUE_FULL;
		ret.r = false;
		goto out;
	}

	// Enqueue message at the tail of the queue
	void *hyp_va =
		(void *)(msgqueue->buf + msgqueue->tail + sizeof(size_t));

	if (from_kernel) {
		(void)memcpy(hyp_va, (void *)msg.kernel_addr, size);
	} else {
		ret.e = useraccess_copy_from_guest(
			hyp_va, msgqueue->max_msg_size, msg.guest_addr, size);
		if (ret.e != OK) {
			goto out;
		}
	}

	(void)memcpy(msgqueue->buf + msgqueue->tail, &size, sizeof(size_t));
	msgqueue->count++;

	// Update tail value
	msgqueue->tail += msgqueue->max_msg_size + sizeof(size_t);

	if (msgqueue->tail == msgqueue->queue_size) {
		msgqueue->tail = 0U;
	}

	// If buffer was previously below the not empty threshold, we must
	// wake up the receiver side by asserting the receiver virq source.
	if (push || (msgqueue->count == msgqueue->notempty_thd)) {
		(void)virq_assert(&msgqueue->rcv_source, false);
	}

	if (msgqueue->count == msgqueue->queue_depth) {
		ret.r = false;
	}

out:
	spinlock_release(&msgqueue->lock);

	return ret;
}

receive_info_result_t
msgqueue_receive_msg(msgqueue_t *msgqueue, kernel_or_gvaddr_t buffer,
		     size_t max_size, bool to_kernel)
{
	receive_info_result_t ret  = { 0 };
	size_t		      size = 0U;

	ret.e	       = OK;
	ret.r.size     = 0U;
	ret.r.notempty = true;

	assert(msgqueue != NULL);
	assert(msgqueue->buf != NULL);

	spinlock_acquire(&msgqueue->lock);

	if (msgqueue->count == 0U) {
		ret.e	       = ERROR_MSGQUEUE_EMPTY;
		ret.r.notempty = false;
		goto out;
	}

	memcpy(&size, msgqueue->buf + msgqueue->head, sizeof(size_t));

	// Dequeue message from the head of the queue
	void *hyp_va =
		(void *)(msgqueue->buf + msgqueue->head + sizeof(size_t));

	if (to_kernel) {
		(void)memcpy((void *)buffer.kernel_addr, hyp_va, size);
	} else {
		ret.e = useraccess_copy_to_guest(buffer.guest_addr, max_size,
						 hyp_va, size);
		if (ret.e != OK) {
			goto out;
		}
	}

	ret.r.size = size;
	msgqueue->count--;

	// Update head value
	msgqueue->head += msgqueue->max_msg_size + sizeof(size_t);

	if (msgqueue->head == msgqueue->queue_size) {
		msgqueue->head = 0U;
	}

	// If buffer was previously above the not full threshold, we must let
	// the sender side know that it can send more messages.
	if (msgqueue->count == msgqueue->notfull_thd) {
		// We wake up the sender side by asserting the sender virq
		// source.
		(void)virq_assert(&msgqueue->send_source, false);
	}

	if (msgqueue->count == 0U) {
		ret.r.notempty = false;
	}

out:
	spinlock_release(&msgqueue->lock);

	return ret;
}

void
msgqueue_flush_queue(msgqueue_t *msgqueue)
{
	assert(msgqueue != NULL);
	assert(msgqueue->buf != NULL);

	spinlock_acquire(&msgqueue->lock);

	// If there is a pending bound interrupt, it will be de-asserted
	if (msgqueue->count != 0U) {
		(void)virq_assert(&msgqueue->send_source, false);
		(void)virq_clear(&msgqueue->rcv_source);
	}

	memset(msgqueue->buf, 0, msgqueue->queue_size);
	msgqueue->count = 0U;
	msgqueue->head	= 0U;
	msgqueue->tail	= 0U;

	spinlock_release(&msgqueue->lock);
}

error_t
msgqueue_bind(msgqueue_t *msgqueue, vic_t *vic, virq_t virq,
	      virq_source_t *source, virq_trigger_t trigger)
{
	assert(msgqueue != NULL);
	assert(vic != NULL);
	assert(source != NULL);

	error_t ret = vic_bind_shared(source, vic, virq, trigger);

	return ret;
}

void
msgqueue_unbind(virq_source_t *source)
{
	assert(source != NULL);

	vic_unbind_sync(source);
}

bool
msgqueue_rx_handle_virq_check_pending(virq_source_t *source, bool reasserted)
{
	bool ret;

	assert(source != NULL);

	msgqueue_t *msgqueue = msgqueue_container_of_rcv_source(source);

	if (reasserted) {
		// Previous VIRQ wasn't delivered yet. If we return false in
		// this case, we can't be sure that we won't race with a
		// msgqueue_send_msg() on another CPU.
		ret = true;
	} else {
		ret = (msgqueue->count >= msgqueue->notempty_thd);
	}

	return ret;
}

bool
msgqueue_tx_handle_virq_check_pending(virq_source_t *source, bool reasserted)
{
	bool ret;

	assert(source != NULL);

	msgqueue_t *msgqueue = msgqueue_container_of_send_source(source);

	if (reasserted) {
		// Previous VIRQ wasn't delivered yet. If we return false in
		// this case, we can't be sure that we won't race with a
		// msgqueue_receive_msg() on another CPU.
		ret = true;
	} else {
		ret = (msgqueue->count <= msgqueue->notfull_thd);
	}

	return ret;
}
