// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Configure the message queue.
// The object's header lock must be held and object state must be
// OBJECT_STATE_INIT.
error_t
msgqueue_configure(msgqueue_t *msgqueue, size_t max_msg_size,
		   count_t queue_depth);

// Send a message to a message queue
// The argument from_kernel, if true, indicates that the message is in a kernel
// buffer.
bool_result_t
msgqueue_send_msg(msgqueue_t *msgqueue, size_t size, kernel_or_gvaddr_t msg,
		  bool push, bool from_kernel);

// Receive a message from a message queue
// The argument to_kernel, if true, indicates that the destination message
// buffer is a kernel address.
receive_info_result_t
msgqueue_receive_msg(msgqueue_t *msgqueue, kernel_or_gvaddr_t buffer,
		     size_t max_size, bool to_kernel);

void
msgqueue_flush_queue(msgqueue_t *msgqueue);

error_t
msgqueue_bind(msgqueue_t *msgqueue, vic_t *vic, virq_t virq,
	      virq_source_t *source, virq_trigger_t trigger);

void
msgqueue_unbind(virq_source_t *source);
