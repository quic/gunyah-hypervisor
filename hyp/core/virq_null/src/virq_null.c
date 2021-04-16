// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <vic.h>
#include <virq.h>

error_t
vic_bind_shared(virq_source_t *source, vic_t *vic, virq_t virq,
		virq_trigger_t trigger)
{
	(void)source;
	(void)vic;
	(void)virq;
	(void)trigger;

	return ERROR_UNIMPLEMENTED;
}

error_t
vic_bind_private_vcpu(virq_source_t *source, thread_t *vcpu, virq_t virq,
		      virq_trigger_t trigger)
{
	(void)source;
	(void)vcpu;
	(void)virq;
	(void)trigger;

	return ERROR_UNIMPLEMENTED;
}

error_t
vic_bind_private_index(virq_source_t *source, vic_t *vic, index_t index,
		       virq_t virq, virq_trigger_t trigger)
{
	(void)source;
	(void)vic;
	(void)index;
	(void)virq;
	(void)trigger;

	return ERROR_UNIMPLEMENTED;
}

bool_result_t
virq_assert(virq_source_t *source, bool edge_only)
{
	(void)source;
	(void)edge_only;

	return bool_result_error(ERROR_VIRQ_NOT_BOUND);
}

error_t
virq_clear(virq_source_t *source)
{
	(void)source;

	return ERROR_VIRQ_NOT_BOUND;
}

bool_result_t
virq_query(virq_source_t *source)
{
	(void)source;

	return bool_result_error(ERROR_VIRQ_NOT_BOUND);
}

void
vic_unbind(virq_source_t *source)
{
	(void)source;
}

void
vic_unbind_sync(virq_source_t *source)
{
	(void)source;
}
