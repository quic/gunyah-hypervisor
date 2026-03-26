// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <atomic.h>
#include <panic.h>
#include <partition.h>
#include <scheduler.h>
#include <spinlock.h>
#include <vic.h>
#include <virq.h>

#include "event_handlers.h"
#include "msgqueue.h"
#include "msgqueue_common.h"

bool_result_t
msgqueue_send(msgqueue_t *msgqueue, size_t size, gvaddr_t data, bool push)
{
	kernel_or_gvaddr_t data_union;
	data_union.guest_addr = data;

	return msgqueue_send_msg(msgqueue, size, data_union, push, false);
}

receive_info_result_t
msgqueue_receive(msgqueue_t *msgqueue, gvaddr_t buffer, size_t max_size)
{
	kernel_or_gvaddr_t buffer_union;
	buffer_union.guest_addr = buffer;

	return msgqueue_receive_msg(msgqueue, buffer_union, max_size, false);
}

void
msgqueue_flush(msgqueue_t *msgqueue)
{
	msgqueue_flush_queue(msgqueue);
}

error_t
msgqueue_configure_send(msgqueue_t *msgqueue, count_t notfull_thd,
			count_t notfull_delay)
{
	error_t ret;

	assert(msgqueue != NULL);

	if (notfull_delay != MSGQUEUE_DELAY_UNCHANGED) {
		ret = ERROR_UNIMPLEMENTED;
		goto out;
	}

	if ((notfull_thd >= msgqueue->queue_depth) &&
	    (notfull_thd != MSGQUEUE_THRESHOLD_UNCHANGED)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	spinlock_acquire(&msgqueue->lock);

	if (notfull_thd != MSGQUEUE_THRESHOLD_UNCHANGED) {
		atomic_store_relaxed(&msgqueue->notfull_thd, notfull_thd);

		if (atomic_load_relaxed(&msgqueue->count) <= notfull_thd) {
			(void)virq_assert(&msgqueue->send_source, false);
		} else {
			(void)virq_clear(&msgqueue->send_source);
		}
	}
	spinlock_release(&msgqueue->lock);

	ret = OK;
out:
	return ret;
}

error_t
msgqueue_configure_receive(msgqueue_t *msgqueue, count_t notempty_thd,
			   count_t notempty_delay)
{
	error_t ret;

	assert(msgqueue != NULL);

	if (notempty_delay != MSGQUEUE_DELAY_UNCHANGED) {
		ret = ERROR_UNIMPLEMENTED;
		goto out;
	}

	if ((notempty_thd == 0U) ||
	    ((notempty_thd > msgqueue->queue_depth) &&
	     (notempty_thd != MSGQUEUE_THRESHOLD_MAXIMUM) &&
	     (notempty_thd != MSGQUEUE_THRESHOLD_UNCHANGED))) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	spinlock_acquire(&msgqueue->lock);

	if (notempty_thd != MSGQUEUE_THRESHOLD_UNCHANGED) {
		count_t new_notempty_thd =
			(notempty_thd == MSGQUEUE_THRESHOLD_MAXIMUM)
				? msgqueue->queue_depth
				: notempty_thd;
		atomic_store_relaxed(&msgqueue->notempty_thd, new_notempty_thd);

		if (atomic_load_relaxed(&msgqueue->count) >= new_notempty_thd) {
			(void)virq_assert(&msgqueue->rcv_source, false);
		} else {
			(void)virq_clear(&msgqueue->rcv_source);
		}
	}

	spinlock_release(&msgqueue->lock);

	ret = OK;
out:
	return ret;
}

error_t
msgqueue_bind_send(msgqueue_t *msgqueue, vic_t *vic, virq_t virq)
{
	return msgqueue_bind(msgqueue, vic, virq, &msgqueue->send_source,
			     VIRQ_TRIGGER_MSGQUEUE_TX);
}

error_t
msgqueue_bind_receive(msgqueue_t *msgqueue, vic_t *vic, virq_t virq)
{
	return msgqueue_bind(msgqueue, vic, virq, &msgqueue->rcv_source,
			     VIRQ_TRIGGER_MSGQUEUE_RX);
}

void
msgqueue_unbind_send(msgqueue_t *msgqueue)
{
	msgqueue_unbind(&msgqueue->send_source);
}

void
msgqueue_unbind_receive(msgqueue_t *msgqueue)
{
	msgqueue_unbind(&msgqueue->rcv_source);
}

error_t
msgqueue_handle_object_create_msgqueue(msgqueue_create_t msgqueue_create)
{
	msgqueue_t *msgqueue = msgqueue_create.msgqueue;
	assert(msgqueue != NULL);
	spinlock_init(&msgqueue->lock);

	return OK;
}

error_t
msgqueue_configure(msgqueue_t *msgqueue, size_t max_msg_size,
		   count_t queue_depth)
{
	error_t ret;

	assert(msgqueue != NULL);

	if ((queue_depth != 0U) && (max_msg_size != 0U) &&
	    (queue_depth < MSGQUEUE_MAX_QUEUE_DEPTH) &&
	    (max_msg_size < MSGQUEUE_MAX_MAX_MSG_SIZE)) {
		msgqueue->max_msg_size = max_msg_size;
		msgqueue->queue_depth  = queue_depth;
		ret		       = OK;
	} else {
		ret = ERROR_ARGUMENT_INVALID;
	}

	return ret;
}

error_t
msgqueue_handle_object_activate_msgqueue(msgqueue_t *msgqueue)
{
	error_t ret;

	assert(msgqueue != NULL);
	assert(msgqueue->buf == NULL);

	if ((msgqueue->queue_depth == 0U) || (msgqueue->max_msg_size == 0U)) {
		ret = ERROR_OBJECT_CONFIG;
		goto out;
	}

	// Message queue size consists of the message maximum size, a field to
	// know the exact size of the message and how many messages can the
	// queue contain.
	size_t	     queue_size = (msgqueue->max_msg_size + sizeof(size_t)) *
				  msgqueue->queue_depth;
	partition_t *partition	= msgqueue->header.partition;

	void_ptr_result_t res =
		partition_alloc(partition, queue_size, alignof(size_t));
	if (res.e != OK) {
		ret = res.e;
		goto out;
	}

	msgqueue->buf = (uint8_t *)res.r;
	atomic_init(&msgqueue->count, 0U);
	msgqueue->queue_size = queue_size;
	msgqueue->head	     = 0U;
	msgqueue->tail	     = 0U;
	atomic_init(&msgqueue->notfull_thd, msgqueue->queue_depth - 1U);
	atomic_init(&msgqueue->notempty_thd, 1U);

	ret = OK;
out:
	return ret;
}

void
msgqueue_handle_object_deactivate_msgqueue(msgqueue_t *msgqueue)
{
	assert(msgqueue != NULL);

	if (msgqueue->buf != NULL) {
		partition_t *partition = msgqueue->header.partition;

		partition_free(partition, msgqueue->buf, msgqueue->queue_size);
		msgqueue->buf = NULL;
	}

	vic_unbind(&msgqueue->send_source);
	vic_unbind(&msgqueue->rcv_source);
}
