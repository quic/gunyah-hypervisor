// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Appends a message to the tail of a message queue, if it is not full. If push
// is true or the buffer was previously below the not empty threshold, wake up
// receiver by asserting receiver virq. Return bool that indicates if queue is
// not full.
bool_result_t
msgqueue_send(msgqueue_t *msgqueue, size_t size, gvaddr_t data, bool push);

// Fetch a message from the head of a message queue, if it is not empty. If the
// buffer was previously above the not full threshold, wake up the sender by
// asserting sender virq. Return size of received message and bool that
// indicates if queue is not empty.
receive_info_result_t
msgqueue_receive(msgqueue_t *msgqueue, gvaddr_t buffer, size_t max_size);

// Removes all messages from message queue. If the message queue was previously
// not empty, deassert virq.
void
msgqueue_flush(msgqueue_t *msgqueue);

// Modify notfull configuration of a message queue send interface. Any parameter
// passed in as MSGQUEUE_THRESHOLD_UNCHANGED indicates no change to the
// corresponding is requested.
error_t
msgqueue_configure_send(msgqueue_t *msgqueue, count_t notfull_thd,
			count_t notfull_delay);

// Modify notemtpy configuration of a message queue receive interface. Any
// parameter passed in as MSGQUEUE_THRESHOLD_UNCHANGED indicates no change to
// the corresponding is requested. A notempty_thd special value of
// MSGQUEUE_THRESHOLD_MAXIMUM sets the threshold to the message queue’s depth.
error_t
msgqueue_configure_receive(msgqueue_t *msgqueue, count_t notempty_thd,
			   count_t notempty_delay);

// Binds message queue send interface to a virtual interrupt.
error_t
msgqueue_bind_send(msgqueue_t *msgqueue, vic_t *vic, virq_t virq);

// Binds message queue receive interface to a virtual interrupt.
error_t
msgqueue_bind_receive(msgqueue_t *msgqueue, vic_t *vic, virq_t virq);

// Unbinds message queue send interface from a virtual interrupt.
void
msgqueue_unbind_send(msgqueue_t *msgqueue);

// Unbinds message queue receive interface from a virtual interrupt.
void
msgqueue_unbind_receive(msgqueue_t *msgqueue);
