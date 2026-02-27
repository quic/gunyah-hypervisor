// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcontainers.h>
#include <hyprights.h>

#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <hyp_aspace.h>
#include <list.h>
#include <log.h>
#include <memdb.h>
#include <memextent.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <range_tree.h>
#include <rcu.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <util.h>
#include <virtio.h>

#include "event_handlers.h"
#include "internal.h"
#include "smmuv3.h"

error_t
virtio_iommu_handle_object_create_virtio_iommu(
	virtio_iommu_create_t virtio_iommu_create)
{
	virtio_iommu_t *virtio_iommu = virtio_iommu_create.virtio_iommu;

	spinlock_init(&virtio_iommu->lock);
	for (index_t i = 0; i < VIRTIO_IOMMU_NUM_QUEUES; i++) {
		spinlock_init(&virtio_iommu->queues[i].lock);
	}

	virtio_iommu->num_streams = 0;

	virtio_iommu->viommu.type      = VIOMMU_TYPE_VIRTIO;
	virtio_iommu->viommu.partition = virtio_iommu->header.partition;

	return OK;
}

error_t
virtio_iommu_handle_object_activate_virtio_iommu(virtio_iommu_t *virtio_iommu)
{
	error_t err;
	error_t ret;

	if ((virtio_iommu->viommu.addrspace == NULL) ||
	    (virtio_iommu->iommu == NULL)) {
		ret = ERROR_OBJECT_CONFIG;
		goto fail_config;
	}

	err = range_tree_init(&virtio_iommu->viommu.endpoint_ranges,
			      virtio_iommu->header.partition);
	if (err != OK) {
		ret = err;
		goto fail_stream_ranges;
	}

	err = range_tree_init(&virtio_iommu->domain_tree,
			      virtio_iommu->header.partition);
	if (err != OK) {
		ret = err;
		goto fail_domain_ranges;
	}

	virtio_status_lock(&virtio_iommu->virtio);

	// The needs-reset state is set by default.

	// Feature flags
	// - Keep to a minimum for now
	// - Support version 1 of the spec.
	// - Support only ATTACH_TABLE
	// - ATTACH_TABLE needs probe so driver can get table format
	uint32_t features;
	features = (uint32_t)util_bit(VIRTIO_IOMMU_F_PROBE) |
		   (uint32_t)util_bit(VIRTIO_IOMMU_F_ATTACH_TABLE);
	err = virtio_set_dev_features(&virtio_iommu->virtio, 0U, features);
	if (err != OK) {
		ret = err;
		goto out_unlock;
	}

	features = (uint32_t)util_bit((index_t)VIRTIO_F_VERSION_1 - 32U);
	err	 = virtio_set_dev_features(&virtio_iommu->virtio, 1U, features);
	if (err != OK) {
		ret = err;
		goto out_unlock;
	}

	for (index_t i = 0; i < VIRTIO_IOMMU_NUM_QUEUES; i++) {
		err = virtio_set_queue_size_max(&virtio_iommu->virtio, i,
						VIRTIO_IOMMU_QUEUE_SIZE_MAX);
		if (err != OK) {
			ret = err;
			goto out_unlock;
		}
	}

	// Guest driver needs to provide memory for the probe result
	virtio_iommu->probe_size = sizeof(virtio_iommu_reply_probe_t) -
				   sizeof(virtio_iommu_reply_tail_t);

	virtio_iommu->page_size_mask = VIRTIO_IOMMU_SMMU_MAP_SIZE;
	virtio_iommu->virtq_size     = VIRTIO_IOMMU_QUEUE_SIZE_MAX;

	virtio_status_unlock(&virtio_iommu->virtio);

	err = virtio_activate(&virtio_iommu->virtio);
	if (err != OK) {
		ret = err;
		goto fail_virtio;
	}

	ret = OK;
	goto out;

out_unlock:
	virtio_status_unlock(&virtio_iommu->virtio);
out:
fail_virtio:
	if (ret != OK) {
		range_tree_lock(&virtio_iommu->domain_tree);
		range_tree_destroy(&virtio_iommu->domain_tree,
				   RANGE_TREE_NODE_TYPE_NONE);
		range_tree_unlock(&virtio_iommu->domain_tree);
	}
fail_domain_ranges:
	if (ret != OK) {
		range_tree_lock(&virtio_iommu->viommu.endpoint_ranges);
		range_tree_destroy(&virtio_iommu->viommu.endpoint_ranges,
				   RANGE_TREE_NODE_TYPE_NONE);
		range_tree_unlock(&virtio_iommu->viommu.endpoint_ranges);
	}
fail_stream_ranges:
fail_config:
	return ret;
}

void
virtio_iommu_handle_object_deactivate_virtio_iommu(virtio_iommu_t *virtio_iommu)
{
	assert(virtio_iommu != NULL);
	(void)virtio_shutdown(&virtio_iommu->virtio);

	spinlock_acquire(&virtio_iommu->lock);
	virtio_iommu_disable_all_streams(virtio_iommu);
	spinlock_release(&virtio_iommu->lock);

	// There should be no domains left after the streams were all detached.
	range_tree_lock(&virtio_iommu->domain_tree);
	range_tree_destroy(&virtio_iommu->domain_tree,
			   RANGE_TREE_NODE_TYPE_NONE);
	range_tree_unlock(&virtio_iommu->domain_tree);

	// Destroy the virtual range tree. The release callback will unbind the
	// corresponding physical ranges.
	range_tree_lock(&virtio_iommu->viommu.endpoint_ranges);
	range_tree_destroy(&virtio_iommu->viommu.endpoint_ranges,
			   RANGE_TREE_NODE_TYPE_VIRTIO_IOMMU_STREAM_RANGE);
	range_tree_unlock(&virtio_iommu->viommu.endpoint_ranges);
}

void
virtio_iommu_handle_object_cleanup_virtio_iommu(virtio_iommu_t *virtio_iommu)
{
	assert(virtio_iommu != NULL);
	(void)virtio_cleanup(&virtio_iommu->virtio);

	if (virtio_iommu->iommu != NULL) {
		object_put_smmuv3(virtio_iommu->iommu);
	}
	if (virtio_iommu->viommu.addrspace != NULL) {
		object_put_addrspace(virtio_iommu->viommu.addrspace);
	}
}

void
virtio_iommu_unwind_object_activate_virtio_iommu(virtio_iommu_t *virtio_iommu)
{
	virtio_iommu_handle_object_deactivate_virtio_iommu(virtio_iommu);
}

static void
virtio_iommu_reset_queue(virtio_iommu_queue_t *queue)
{
	queue->size    = VIRTIO_IOMMU_QUEUE_SIZE_MAX;
	queue->enabled = 0;

	queue->desc = 0;
	queue->dev  = 0;
	queue->drv  = 0;

	queue->curr_drv_idx = 0;
	queue->curr_dev_idx = 0;
}

void
virtio_iommu_handle_virtio_reset_requested(virtio_t *virtio)
{
	virtio_iommu_t *virtio_iommu = virtio_iommu_container_of_virtio(virtio);

	// TODO:
	//   What to do with active SMMU entries linked to this virtio?
	//   Reprogram all active streams attached to the instance to
	//   default/abort?
	//   -> Just go and mark all the STEs invalid. They will probably get
	//      used again, so we dont free the STE memory, that can be re-used
	//      when programmed again.

	for (count_t i = 0; i < virtio->vqs; i++) {
		virtio_iommu_reset_queue(&virtio_iommu->queues[i]);
	}

	// Preempt already held by this handler
	spinlock_acquire_nopreempt(&virtio_iommu->lock);
	virtio_iommu_disable_all_streams(virtio_iommu);
	spinlock_release_nopreempt(&virtio_iommu->lock);

	error_t err = virtio_reset_complete(virtio);
	// Reset completion can only fail if there was no reset requested,
	// which is obviously not true in this case.
	assert(err == OK);
}

error_t
virtio_iommu_handle_virtio_driver_ok(virtio_t *virtio)
{
	error_t ret;
	assert(virtio != NULL);

	virtio_iommu_t *virtio_iommu = virtio_iommu_container_of_virtio(virtio);

	// Do some validations and setup mappings etc
	// We need to setup the virtq event queue
	// TODO: free on reset
	virtio_queue_info_result_t qinfo;
	for (index_t i = 0; i < virtio->vqs; i++) {
		qinfo = virtio_get_queue_info(virtio, i, true);
		if (qinfo.e != OK) {
			// Mark status failed
			// Dont enable the device
			// We will cleanup at reset as needed.
			ret = qinfo.e;
			virtio_needs_reset(virtio);
			goto out;
		}

		virtio_iommu_queue_t *q = &virtio_iommu->queues[i];
		if (!qinfo.r.ready) {
			ret = ERROR_OBJECT_STATE;
			virtio_needs_reset(virtio);
			goto out;
		}

		// If the Queue is marked as ready we can start using it.
		// Check the provided addrs look ok, then get the Phys addr so
		// Hyp can access it. These vals are in VM IPA. For HLOS it is
		// 1:1 for now.

		q->enabled = qinfo.r.ready;
		q->size	   = qinfo.r.size;
		q->desc	   = qinfo.r.desc;
		q->drv	   = qinfo.r.drv;
		q->dev	   = qinfo.r.dev;
	}
	ret = OK;
out:
	return ret;
}

void
virtio_iommu_handle_virtio_queue_notify(virtio_t *virtio, index_t vq)
{
	assert(virtio != NULL);

	// Check status is still ok.
	// Check if the Queue is still ready

	// Only handle notifications from the Request Queue
	// We also get this trigger if we send a msg to the eventQ. Is that ok?
	//  -> Seems to work fine, just ignore it.
	if (vq == VIRTIO_IOMMU_REQUEST_QUEUE) {
		virtio_iommu_t *virtio_iommu =
			virtio_iommu_container_of_virtio(virtio);

		// Locking:
		// Different vCPUs can kick the same Virtio RequestQ, then how
		// do we process the commands? For now have a big lock and then
		// we can see what the best way is later if the performance is
		// very bad on calls like invalidations. If the SMMUv3 supports
		// multiple RequestQs, we could assign one per vpcu and then the
		// big lock per Q should be fine. If we have fewer Qs than CPUs,
		// the CPU will have to try all of them and commit to waiting on
		// the last one.

		spinlock_acquire(&virtio_iommu->lock);
		virtio_iommu_process_queue_notification(virtio_iommu, vq);
		spinlock_release(&virtio_iommu->lock);
	}
}

uint32_result_t
virtio_iommu_handle_virtio_device_config_read(virtio_t *virtio, size_t offset,
					      size_t access_size)
{
	uint32_result_t ret;

	assert(virtio != NULL);

	virtio_iommu_t *virtio_iommu = virtio_iommu_container_of_virtio(virtio);

	// All supported properties are 32bit for now.
	// Split them out if we need others.
	if (access_size != sizeof(uint32_t)) {
		TRACE(DEBUG, INFO,
		      "virtio_iommu: Only 32bit config accesses supported");
		ret = uint32_result_error(ERROR_UNIMPLEMENTED);
		goto out;
	}

	uint32_t val;
	switch (offset) {
	case offsetof(virtio_iommu_config_t, page_size_mask_low):
		val = (uint32_t)(virtio_iommu->page_size_mask & util_mask(32U));
		ret = uint32_result_ok(val);
		break;
	case offsetof(virtio_iommu_config_t, page_size_mask_high):
		val = (uint32_t)((virtio_iommu->page_size_mask &
				  ~util_mask(32U)) >>
				 32U);
		ret = uint32_result_ok(val);
		break;
	case offsetof(virtio_iommu_config_t, probe_size):
		val = (uint32_t)(virtio_iommu->probe_size & util_mask(32U));
		ret = uint32_result_ok(val);
		break;
	default:
		ret = uint32_result_error(ERROR_UNIMPLEMENTED);
		TRACE(DEBUG, INFO,
		      "virtio_iommu: virtio_device_config_read failed {:d} offset = {:d}",
		      __LINE__, offset);
		break;
	}

out:
	return ret;
}

smmuv3_stream_range_t *
virtio_iommu_find_range(virtio_iommu_t	  *virtio_iommu,
			viommu_stream_id_t stream_id)
{
	smmuv3_stream_range_t *range;

	range_tree_lookup_result_t lookup_r = range_tree_lookup(
		&virtio_iommu->viommu.endpoint_ranges, stream_id, 1U);
	if (lookup_r.node == NULL) {
		range = NULL;
		goto out;
	}

	range = smmuv3_stream_range_container_of_virt_node(lookup_r.node);
out:
	return range;
}

static error_t
virtio_iommu_bind_streams(virtio_iommu_t    *virtio_iommu,
			  viommu_stream_id_t stream_id_start,
			  count_t	     stream_count)
	REQUIRE_SPINLOCK(virtio_iommu -> lock)
{
	error_t ret;

#if defined(MODULE_PLATFORM_SMMUV3) && MODULE_PLATFORM_SMMUV3 && SMMU_V3_ENABLE
	// SMMUv3 stream IDs are 32-bit
	if (stream_id_start > UINT32_MAX) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	smmuv3_stream_range_ptr_result_t range_r = smmuv3_bind_stream_range(
		virtio_iommu->iommu, &virtio_iommu->viommu,
		(uint32_t)stream_id_start, stream_count);
	if (range_r.e != OK) {
		ret = range_r.e;
		goto out;
	}

	range_tree_lock_nopreempt(&virtio_iommu->viommu.endpoint_ranges);
	ret = range_tree_insert(&virtio_iommu->viommu.endpoint_ranges,
				&range_r.r->virt_node, stream_id_start,
				stream_count);
	range_tree_unlock_nopreempt(&virtio_iommu->viommu.endpoint_ranges);
	if (ret != OK) {
		smmuv3_unbind_stream_range(range_r.r);
	}
out:
#else
	(void)stream_id_start;
	(void)stream_count;
	ret = ERROR_UNIMPLEMENTED;
#endif
	return ret;
}

static error_t
virtio_iommu_unbind_streams(virtio_iommu_t    *virtio_iommu,
			    viommu_stream_id_t stream_id_start,
			    count_t	       stream_count)
	REQUIRE_SPINLOCK(virtio_iommu -> lock)
{
	error_t ret;
#if defined(MODULE_PLATFORM_SMMUV3) && MODULE_PLATFORM_SMMUV3 && SMMU_V3_ENABLE
	range_tree_t *viommu_ranges = &virtio_iommu->viommu.endpoint_ranges;

	range_tree_lock_nopreempt(viommu_ranges);

	// Lookup the range in the viommu tree first.
	rcu_read_start();
	range_tree_lookup_result_t lookup_r =
		range_tree_lookup(viommu_ranges, stream_id_start, 1U);

	range_tree_node_t *node = lookup_r.node;
	if (node == NULL) {
		ret = ERROR_ARGUMENT_INVALID;
		range_tree_unlock_nopreempt(viommu_ranges);
		goto out;
	}

	// FIXME: Partial unbinding is not supported for now.
	if ((node->base != stream_id_start) || (node->size != stream_count)) {
		ret = ERROR_ARGUMENT_INVALID;
		range_tree_unlock_nopreempt(viommu_ranges);
		goto out;
	}

	// Try removing from the viommu tree first. If it succeeds then it can
	// be unbound from the physical iommu.
	ret = range_tree_remove(viommu_ranges, node);
	range_tree_unlock_nopreempt(viommu_ranges);
	if (ret != OK) {
		goto out;
	}

	// Actually delete the node, which will implicitly detach any streams
	// that were left attached. Note that we rely on this being called with
	// the lock held to prevent races with the stream IDs being re-bound.
	smmuv3_stream_range_t *range =
		smmuv3_stream_range_container_of_virt_node(node);

	virtio_iommu_disable_range_streams(virtio_iommu, range);

	// Disabling the range's streams should have emptied the endpoint tree.
	range_tree_lock_nopreempt(&range->endpoint_tree);
	range_tree_destroy(&range->endpoint_tree, RANGE_TREE_NODE_TYPE_NONE);
	range_tree_unlock_nopreempt(&range->endpoint_tree);

	smmuv3_unbind_stream_range(range);

out:
	rcu_read_finish();
#else
	(void)stream_id_start;
	(void)stream_count;
	ret = ERROR_UNIMPLEMENTED;
#endif
	return ret;
}

error_t
virtio_iommu_handle_viommu_bind_streams(cap_id_t viommu_cap, cap_id_t iommu_cap,
					viommu_stream_id_t stream_id_start,
					count_t		   stream_count,
					count_t		  *bound_count)
{
	error_t	  ret;
	cspace_t *cspace = cspace_get_self();

	virtio_iommu_ptr_result_t virtio_iommu_r = cspace_lookup_virtio_iommu(
		cspace, viommu_cap, CAP_RIGHTS_VIRTIO_IOMMU_MANAGE_STREAMS);
	if (virtio_iommu_r.e != OK) {
		ret = virtio_iommu_r.e;
		goto out;
	}
	virtio_iommu_t *virtio_iommu = virtio_iommu_r.r;

	spinlock_acquire(&virtio_iommu->lock);

	smmuv3_ptr_result_t iommu_r = cspace_lookup_smmuv3(
		cspace, iommu_cap, CAP_RIGHTS_SMMUV3_MANAGE_STREAMS);
	if (iommu_r.e != OK) {
		ret = iommu_r.e;
		goto out_locked;
	}

	// Check if the virtio_iommu has been configured with provided iommu.
	if (virtio_iommu->iommu != iommu_r.r) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out_ref_smmu;
	}

	ret = virtio_iommu_bind_streams(virtio_iommu, stream_id_start,
					stream_count);
out_ref_smmu:
	object_put_smmuv3(iommu_r.r);
out_locked:
	spinlock_release(&virtio_iommu->lock);
	object_put_virtio_iommu(virtio_iommu);
out:
	*bound_count = (ret == OK) ? stream_count : 0U;
	return ret;
}

error_t
virtio_iommu_handle_viommu_unbind_streams(cap_id_t	     viommu_cap,
					  viommu_stream_id_t stream_id_start,
					  count_t	     stream_count,
					  count_t	    *unbound_count)
{
	error_t	  ret;
	cspace_t *cspace = cspace_get_self();

	virtio_iommu_ptr_result_t virtio_iommu_r = cspace_lookup_virtio_iommu(
		cspace, viommu_cap, CAP_RIGHTS_VIRTIO_IOMMU_MANAGE_STREAMS);
	if (virtio_iommu_r.e != OK) {
		ret = virtio_iommu_r.e;
		goto out;
	}
	virtio_iommu_t *virtio_iommu = virtio_iommu_r.r;

	spinlock_acquire(&virtio_iommu->lock);

	if (virtio_iommu->iommu == NULL) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out_locked;
	}

	ret = virtio_iommu_unbind_streams(virtio_iommu, stream_id_start,
					  stream_count);
out_locked:
	spinlock_release(&virtio_iommu->lock);
	object_put_virtio_iommu(virtio_iommu);
out:
	*unbound_count = (ret == OK) ? stream_count : 0U;
	return ret;
}

bool
virtio_iommu_handle_range_tree_release_node(range_tree_node_t *node)
{
#if defined(MODULE_PLATFORM_SMMUV3) && MODULE_PLATFORM_SMMUV3 && SMMU_V3_ENABLE
	smmuv3_stream_range_t *range =
		smmuv3_stream_range_container_of_virt_node(node);

	// At this point the endpoint tree should already be empty.
	range_tree_lock(&range->endpoint_tree);
	range_tree_destroy(&range->endpoint_tree, RANGE_TREE_NODE_TYPE_NONE);
	range_tree_unlock(&range->endpoint_tree);

	smmuv3_unbind_stream_range(range);

	return true;
#else
	(void)node;
	return false;
#endif
}
