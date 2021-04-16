// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <scheduler.h>
#include <spinlock.h>
#include <vic.h>
#include <virq.h>

#include "doorbell.h"
#include "event_handlers.h"

doorbell_flags_result_t
doorbell_send(doorbell_t *doorbell, doorbell_flags_t new_flags)
{
	doorbell_flags_result_t ret = { 0 };

	assert(doorbell != NULL);
	ret.e = OK;

	if (new_flags == 0U) {
		ret.e = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	spinlock_acquire(&doorbell->lock);

	ret.r = doorbell->flags;

	doorbell->flags |= new_flags;

	if ((doorbell->flags & doorbell->enable_mask) != 0U) {
		// Assert if there are flags enabled
		(void)virq_assert(&doorbell->source, false);
		doorbell->flags &= ~doorbell->ack_mask;
	}

	spinlock_release(&doorbell->lock);

out:
	return ret;
}

doorbell_flags_result_t
doorbell_receive(doorbell_t *doorbell, doorbell_flags_t clear_flags)
{
	doorbell_flags_result_t ret = { 0 };

	assert(doorbell != NULL);
	ret.e = OK;

	if (clear_flags == 0U) {
		ret.e = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	spinlock_acquire(&doorbell->lock);

	ret.r = doorbell->flags;

	doorbell->flags &= ~clear_flags;

	spinlock_release(&doorbell->lock);

out:
	return ret;
}

error_t
doorbell_reset(doorbell_t *doorbell)
{
	error_t ret = OK;

	assert(doorbell != NULL);

	spinlock_acquire(&doorbell->lock);

	// If there is a pending bound interrupt, it will be de-asserted
	(void)virq_clear(&doorbell->source);

	doorbell->flags	      = 0U;
	doorbell->ack_mask    = 0U;
	doorbell->enable_mask = ~doorbell->ack_mask;

	spinlock_release(&doorbell->lock);

	return ret;
}

error_t
doorbell_mask(doorbell_t *doorbell, doorbell_flags_t new_enable_mask,
	      doorbell_flags_t new_ack_mask)
{
	error_t ret = OK;

	assert(doorbell != NULL);

	spinlock_acquire(&doorbell->lock);

	bool was_asserted = (doorbell->flags & doorbell->enable_mask) != 0U;
	bool now_asserted = (doorbell->flags & new_enable_mask) != 0U;

	doorbell->enable_mask = new_enable_mask;
	doorbell->ack_mask    = new_ack_mask;

	if (was_asserted && !now_asserted) {
		// Deassert if new mask disables all currently asserted flags
		(void)virq_clear(&doorbell->source);

	} else if (!was_asserted & now_asserted) {
		// Assert if new mask enables flags that are already set
		(void)virq_assert(&doorbell->source, false);
		doorbell->flags &= ~doorbell->ack_mask;
	} else if (was_asserted && now_asserted) {
		doorbell->flags &= ~doorbell->ack_mask;
	} else {
		// Nothing to do.
	}

	spinlock_release(&doorbell->lock);

	return ret;
}

bool
doorbell_handle_virq_check_pending(virq_source_t *source, bool reasserted)
{
	bool ret;

	assert(source != NULL);

	doorbell_t *doorbell = doorbell_container_of_source(source);

	if (reasserted) {
		// Previous VIRQ wasn't delivered yet. If we return false in
		// this case, we can't be sure that we won't race with a
		// doorbell_send() or doorbell_mask() on another CPU.
		ret = true;
	} else {
		ret = ((doorbell->flags & doorbell->enable_mask) != 0U);
	}

	return ret;
}

error_t
doorbell_bind(doorbell_t *doorbell, vic_t *vic, virq_t virq)
{
	error_t ret = OK;

	assert(doorbell != NULL);
	assert(vic != NULL);

	ret = vic_bind_shared(&doorbell->source, vic, virq,
			      VIRQ_TRIGGER_DOORBELL);

	return ret;
}

void
doorbell_unbind(doorbell_t *doorbell)
{
	assert(doorbell != NULL);

	vic_unbind_sync(&doorbell->source);
}

error_t
doorbell_handle_object_create_doorbell(doorbell_create_t params)
{
	assert(params.doorbell != NULL);

	spinlock_init(&params.doorbell->lock);

	spinlock_acquire(&params.doorbell->lock);

	params.doorbell->flags	     = 0U;
	params.doorbell->ack_mask    = 0U;
	params.doorbell->enable_mask = ~params.doorbell->ack_mask;

	spinlock_release(&params.doorbell->lock);

	return OK;
}

void
doorbell_handle_object_deactivate_doorbell(doorbell_t *doorbell)
{
	assert(doorbell != NULL);

	spinlock_acquire(&doorbell->lock);

	vic_unbind(&doorbell->source);

	spinlock_release(&doorbell->lock);
}
