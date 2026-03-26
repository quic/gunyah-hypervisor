// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <compiler.h>
#include <trace.h>
#include <util.h>

#include "useraccess.h"
#include "virtio_virtq.h"

virtio_virtq_desc_chain_t
virtio_virtq_read_desc_chain(virtio_iommu_t *virtio_iommu, index_t qnum,
			     index_t first_desc_idx, uint8_t *req_buf,
			     size_t req_buf_size)
{
	assert((req_buf != NULL) || (req_buf_size == 0U));

	virtio_virtq_desc_chain_t ret = { 0 };

	virtio_iommu_queue_t *q		     = &virtio_iommu->queues[qnum];
	index_t		      desc_entry_idx = first_desc_idx;
	bool		      found_writable = false;

	bool do_next = true;
	while (do_next && (ret.count < util_array_size(ret.entries))) {
		if (desc_entry_idx >= q->size) {
			TRACE(DEBUG, INFO, "virtq: idx {:d} >= size {:d}",
			      desc_entry_idx, q->size);
			break;
		}

		virtio_virtq_desc_t *desc_entry = &ret.entries[ret.count];
		error_t		     err = useraccess_copy_from_guest_ipa(
						   virtio_iommu->viommu.addrspace,
						   desc_entry, sizeof(*desc_entry),
						   q->desc + (sizeof(virtio_virtq_desc_t) *
							      desc_entry_idx),
						   sizeof(virtio_virtq_desc_t), false, false)
						   .e;
		if (err != OK) {
			TRACE(DEBUG, INFO, "user access fail {:d} err={:d}}",
			      __LINE__, (register_t)err);
			break;
		}

		// Find the next descriptor in the chain.
		do_next = virtio_virtq_desc_flags_get_next(&desc_entry->flags);
		desc_entry_idx = desc_entry->next;

		// Validate the descriptor type and read in the request.
		if (!found_writable &&
		    !virtio_virtq_desc_flags_get_write(&desc_entry->flags)) {
			// Skip any read-only descriptors that are entirely
			// beyond the end of the expected request buffer. Note
			// that ret.count is intentionally not incremented.
			if (ret.req_size >= req_buf_size) {
				continue;
			}

			err = useraccess_copy_from_guest_ipa(
				      virtio_iommu->viommu.addrspace,
				      req_buf + ret.req_size,
				      req_buf_size - ret.req_size,
				      desc_entry->addr, desc_entry->len, false,
				      false)
				      .e;
			if (err != OK) {
				TRACE(DEBUG, INFO,
				      "user access fail {:d} err={:d}}",
				      __LINE__, (register_t)err);
				break;
			}

			if (util_add_overflows(ret.req_size, desc_entry->len)) {
				ret.req_size = SIZE_MAX;
			} else {
				ret.req_size += desc_entry->len;
			}
		} else if (virtio_virtq_desc_flags_get_write(
				   &desc_entry->flags)) {
			if (!found_writable) {
				found_writable	= true;
				ret.reply_start = ret.count;
			}
			if (util_add_overflows(ret.reply_space,
					       desc_entry->len)) {
				ret.reply_space = SIZE_MAX;
			} else {
				ret.reply_space += desc_entry->len;
			}
		} else {
			// Driver has placed a read-only buffer after the first
			// write-only buffer. This is not permitted for split
			// virtqueues. Stop reading descriptors.
			TRACE(DEBUG, INFO, "virtq: bad desc, ignored");
			break;
		}

		ret.count++;
	}

	if (ret.req_size < req_buf_size) {
		(void)memset_s(req_buf + ret.req_size,
			       req_buf_size - ret.req_size, 0,
			       req_buf_size - ret.req_size);
	}

	return ret;
}

void
virtio_virtq_write_reply(virtio_iommu_t *virtio_iommu, index_t qnum,
			 count_t			  first_desc_idx,
			 const virtio_virtq_desc_chain_t *chain,
			 const uint8_t *reply_buf, size_t reply_size)
{
	assert(chain->reply_start < chain->count);
	assert((reply_buf != NULL) || (reply_size == 0U));

	size_t used_bytes = 0U;

	virtio_iommu_queue_t *q = &virtio_iommu->queues[qnum];

	// Write out the reply
	for (index_t desc_idx = chain->reply_start; desc_idx < chain->count;
	     desc_idx++) {
		if (reply_size <= used_bytes) {
			break;
		}

		const virtio_virtq_desc_t *desc_entry =
			&chain->entries[desc_idx];

		size_result_t write_r = useraccess_copy_to_guest_ipa(
			virtio_iommu->viommu.addrspace, desc_entry->addr,
			desc_entry->len, reply_buf + used_bytes,
			reply_size - used_bytes, false, false);
		if (write_r.e != OK) {
			TRACE(DEBUG, INFO, "user access fail {:d} err={:d}}",
			      __LINE__, (register_t)write_r.e);
			break;
		}
		used_bytes += write_r.r;
	}

	// update the used ring
	index_t uidx = q->curr_dev_idx % q->size;
	q->curr_dev_idx++;

	virtio_virtq_used_elem_t used_entry = {
		.id  = first_desc_idx,
		.len = (uint32_t)used_bytes,
	};

	// We have to update the node info before updating the index. Can we
	// lookup the 'used' buf address once and use it multiple times here? We
	// are walking the IPA->PA tables multiple times here. Copy the used
	// node info
	vmaddr_t ring_addr = q->dev + sizeof(virtio_virtq_used_t) +
			     (sizeof(virtio_virtq_used_elem_t) * uidx);
	error_t	 err	   = useraccess_copy_to_guest_ipa(
				     virtio_iommu->viommu.addrspace, ring_addr,
				     sizeof(used_entry), &used_entry,
				     sizeof(used_entry), false, false)
				     .e;
	if (err != OK) {
		TRACE(DEBUG, INFO, "user access fail {:d} err={:d}", __LINE__,
		      (register_t)err);
		goto out;
	}

	// Release fence, to ensure that all writes into used buffers are
	// ordered before the write of the used head. This matches an acquire
	// fence / read barrier that is required in the frontend driver before
	// reading from the used ring.
	atomic_thread_fence(memory_order_release);

	virtio_virtq_used_t used = {
		.flags = { 0 },
		.idx   = q->curr_dev_idx,
	};
	err = useraccess_copy_to_guest_ipa(
		      virtio_iommu->viommu.addrspace,
		      q->dev + offsetof(virtio_virtq_used_t, idx),
		      sizeof(used.idx), &used.idx, sizeof(used.idx), false,
		      false)
		      .e;
	if (err != OK) {
		TRACE(DEBUG, INFO, "user access fail {:d} err={:d}}", __LINE__,
		      (register_t)err);
		goto out;
	}

out:
	return;
}
