// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcall_def.h>
#include <hyprights.h>

#include <atomic.h>
#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <object.h>
#include <spinlock.h>
#include <thread.h>

#include "msgqueue.h"
#include "msgqueue_common.h"

error_t
hypercall_msgqueue_bind_send_virq(cap_id_t msgqueue_cap, cap_id_t vic_cap,
				  virq_t virq)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	msgqueue_ptr_result_t p = cspace_lookup_msgqueue(
		cspace, msgqueue_cap, CAP_RIGHTS_MSGQUEUE_BIND_SEND);
	if (compiler_unexpected(p.e != OK)) {
		err = p.e;
		goto out;
	}
	msgqueue_t *msgqueue = p.r;

	vic_ptr_result_t v =
		cspace_lookup_vic(cspace, vic_cap, CAP_RIGHTS_VIC_BIND_SOURCE);
	if (compiler_unexpected(v.e != OK)) {
		err = v.e;
		goto out_msgqueue_release;
	}
	vic_t *vic = v.r;

	err = msgqueue_bind_send(msgqueue, vic, virq);

	object_put_vic(vic);
out_msgqueue_release:
	object_put_msgqueue(msgqueue);
out:
	return err;
}

error_t
hypercall_msgqueue_bind_receive_virq(cap_id_t msgqueue_cap, cap_id_t vic_cap,
				     virq_t virq)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	msgqueue_ptr_result_t p = cspace_lookup_msgqueue(
		cspace, msgqueue_cap, CAP_RIGHTS_MSGQUEUE_BIND_RECEIVE);
	if (compiler_unexpected(p.e != OK)) {
		err = p.e;
		goto out;
	}
	msgqueue_t *msgqueue = p.r;

	vic_ptr_result_t v =
		cspace_lookup_vic(cspace, vic_cap, CAP_RIGHTS_VIC_BIND_SOURCE);
	if (compiler_unexpected(v.e != OK)) {
		err = v.e;
		goto out_msgqueue_release;
	}
	vic_t *vic = v.r;

	err = msgqueue_bind_receive(msgqueue, vic, virq);

	object_put_vic(vic);
out_msgqueue_release:
	object_put_msgqueue(msgqueue);
out:
	return err;
}

error_t
hypercall_msgqueue_unbind_send_virq(cap_id_t msgqueue_cap)
{
	error_t	  err	 = OK;
	cspace_t *cspace = cspace_get_self();

	msgqueue_ptr_result_t p = cspace_lookup_msgqueue(
		cspace, msgqueue_cap, CAP_RIGHTS_MSGQUEUE_BIND_SEND);
	if (compiler_unexpected(p.e != OK)) {
		err = p.e;
		goto out;
	}
	msgqueue_t *msgqueue = p.r;

	msgqueue_unbind_send(msgqueue);

	object_put_msgqueue(msgqueue);
out:
	return err;
}

error_t
hypercall_msgqueue_unbind_receive_virq(cap_id_t msgqueue_cap)
{
	error_t	  err	 = OK;
	cspace_t *cspace = cspace_get_self();

	msgqueue_ptr_result_t p = cspace_lookup_msgqueue(
		cspace, msgqueue_cap, CAP_RIGHTS_MSGQUEUE_BIND_RECEIVE);
	if (compiler_unexpected(p.e != OK)) {
		err = p.e;
		goto out;
	}
	msgqueue_t *msgqueue = p.r;

	msgqueue_unbind_receive(msgqueue);

	object_put_msgqueue(msgqueue);
out:
	return err;
}

hypercall_msgqueue_send_result_t
hypercall_msgqueue_send(cap_id_t msgqueue_cap, size_t size, user_ptr_t data,
			msgqueue_send_flags_t send_flags)
{
	hypercall_msgqueue_send_result_t ret	= { 0 };
	cspace_t			*cspace = cspace_get_self();

	msgqueue_ptr_result_t p = cspace_lookup_msgqueue(
		cspace, msgqueue_cap, CAP_RIGHTS_MSGQUEUE_SEND);
	if (compiler_unexpected(p.e != OK)) {
		ret.error = p.e;
		goto out;
	}
	msgqueue_t *msgqueue = p.r;

	bool push = msgqueue_send_flags_get_push(&send_flags);

	bool_result_t res = msgqueue_send(msgqueue, size, (gvaddr_t)data, push);

	ret.error    = res.e;
	ret.not_full = res.r;

	object_put_msgqueue(msgqueue);
out:
	return ret;
}

hypercall_msgqueue_receive_result_t
hypercall_msgqueue_receive(cap_id_t msgqueue_cap, user_ptr_t buffer,
			   size_t buf_size)
{
	hypercall_msgqueue_receive_result_t ret	   = { 0 };
	cspace_t			   *cspace = cspace_get_self();

	msgqueue_ptr_result_t p = cspace_lookup_msgqueue(
		cspace, msgqueue_cap, CAP_RIGHTS_MSGQUEUE_RECEIVE);
	if (compiler_unexpected(p.e != OK)) {
		ret.error = p.e;
		goto out;
	}
	msgqueue_t *msgqueue = p.r;

	receive_info_result_t res;
	res = msgqueue_receive(msgqueue, (gvaddr_t)buffer, buf_size);

	ret.error     = res.e;
	ret.size      = res.r.size;
	ret.not_empty = res.r.notempty;

	object_put_msgqueue(msgqueue);
out:
	return ret;
}

error_t
hypercall_msgqueue_flush(cap_id_t msgqueue_cap)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	msgqueue_ptr_result_t p = cspace_lookup_msgqueue(
		cspace, msgqueue_cap, CAP_RIGHTS_MSGQUEUE_RECEIVE);
	if (compiler_unexpected(p.e != OK)) {
		err = p.e;
		goto out;
	}
	msgqueue_t *msgqueue = p.r;

	msgqueue_flush(msgqueue);
	err = OK;

	object_put_msgqueue(msgqueue);
out:
	return err;
}

error_t
hypercall_msgqueue_configure_send(cap_id_t msgqueue_cap, count_t not_full_thres,
				  count_t not_full_holdoff)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	msgqueue_ptr_result_t p = cspace_lookup_msgqueue(
		cspace, msgqueue_cap, CAP_RIGHTS_MSGQUEUE_SEND);
	if (compiler_unexpected(p.e != OK)) {
		err = p.e;
		goto out;
	}
	msgqueue_t *msgqueue = p.r;

	err = msgqueue_configure_send(msgqueue, not_full_thres,
				      not_full_holdoff);

	object_put_msgqueue(msgqueue);
out:
	return err;
}

error_t
hypercall_msgqueue_configure_receive(cap_id_t msgqueue_cap,
				     count_t  not_empty_thres,
				     count_t  not_empty_holdoff)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	msgqueue_ptr_result_t p = cspace_lookup_msgqueue(
		cspace, msgqueue_cap, CAP_RIGHTS_MSGQUEUE_RECEIVE);
	if (compiler_unexpected(p.e != OK)) {
		err = p.e;
		goto out;
	}
	msgqueue_t *msgqueue = p.r;

	err = msgqueue_configure_receive(msgqueue, not_empty_thres,
					 not_empty_holdoff);

	object_put_msgqueue(msgqueue);
out:
	return err;
}

error_t
hypercall_msgqueue_configure(cap_id_t		    msgqueue_cap,
			     msgqueue_create_info_t create_info)
{
	error_t	      err;
	cspace_t     *cspace = cspace_get_self();
	object_type_t type;

	object_ptr_result_t o = cspace_lookup_object_any(
		cspace, msgqueue_cap, CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE,
		&type);
	if (compiler_unexpected(o.e != OK)) {
		err = o.e;
		goto out;
	}
	if (type != OBJECT_TYPE_MSGQUEUE) {
		err = ERROR_CSPACE_WRONG_OBJECT_TYPE;
		goto out_msgqueue_release;
	}

	msgqueue_t *target_msgqueue = o.r.msgqueue;

	spinlock_acquire(&target_msgqueue->header.lock);

	size_t max_msg_size =
		msgqueue_create_info_get_max_msg_size(&create_info);
	count_t queue_depth =
		msgqueue_create_info_get_queue_depth(&create_info);

	if (atomic_load_relaxed(&target_msgqueue->header.state) ==
	    OBJECT_STATE_INIT) {
		err = msgqueue_configure(target_msgqueue, max_msg_size,
					 queue_depth);
	} else {
		err = ERROR_OBJECT_STATE;
	}

	spinlock_release(&target_msgqueue->header.lock);
out_msgqueue_release:
	object_put(type, o.r);
out:
	return err;
}
