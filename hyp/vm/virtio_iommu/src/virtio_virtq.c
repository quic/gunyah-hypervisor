// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <compiler.h>
#include <list.h>
#include <log.h>
#include <panic.h>
#include <partition.h>
#include <range_tree.h>
#include <rcu.h>
#include <spinlock.h>
#include <trace.h>
#include <util.h>
#include <virtio.h>

#include "event_handlers.h"
#include "internal.h"
#include "smmuv3.h"
#include "useraccess.h"
#include "virtio_hw_probe.h"
#include "virtio_virtq.h"

#if defined(SMMU_V3_ENABLE) && SMMU_V3_ENABLE

static virtio_iommu_domain_t *
virtio_iommu_find_domain(virtio_iommu_t *virtio_iommu, uint32_t guest_domain_id)
	REQUIRE_SPINLOCK(virtio_iommu -> lock)
{
	virtio_iommu_domain_t *ret = NULL;

	rcu_read_start();
	range_tree_lookup_result_t lookup_r = range_tree_lookup(
		&virtio_iommu->domain_tree, guest_domain_id, 1U);
	if (lookup_r.node == NULL) {
		ret = NULL;
		goto out;
	}

	ret = virtio_iommu_domain_container_of_tree_node(lookup_r.node);
out:
	// The domain's lifetime is protected by the lock so there is no need
	// to retain the RCU critical section.
	rcu_read_finish();
	return ret;
}

static virtio_iommu_endpoint_t *
virtio_iommu_find_endpoint(virtio_iommu_t	 *virtio_iommu,
			   smmuv3_stream_range_t *range,
			   smmu_v3_stream_id_t	  stream_id)
	REQUIRE_SPINLOCK(virtio_iommu -> lock)
{
	assert(range->viommu == &virtio_iommu->viommu);

	virtio_iommu_endpoint_t *ret = NULL;

	rcu_read_start();
	range_tree_lookup_result_t lookup_r =
		range_tree_lookup(&range->endpoint_tree, stream_id, 1U);
	if (lookup_r.node == NULL) {
		ret = NULL;
		goto out;
	}

	ret = virtio_iommu_endpoint_container_of_tree_node(lookup_r.node);
out:
	// The domain's lifetime is protected by the lock so there is no need
	// to retain the RCU critical section.
	rcu_read_finish();
	return ret;
}

static void
virtio_iommu_do_detach_stream(virtio_iommu_t	      *virtio_iommu,
			      virtio_iommu_endpoint_t *endpoint)
	REQUIRE_SPINLOCK(virtio_iommu -> lock)
{
	smmuv3_stream_range_t *range  = endpoint->range;
	virtio_iommu_domain_t *domain = endpoint->domain;
	smmu_v3_stream_id_t    stream_id =
		(smmu_v3_stream_id_t)endpoint->tree_node.base;

	// Tell the HW to detach the physical stream.
	smmuv3_detach_stream(range, stream_id);

	// Remove the endpoint from the range's tree and domain's list.
	range_tree_lock_nopreempt(&range->endpoint_tree);
	(void)range_tree_remove(&range->endpoint_tree, &endpoint->tree_node);
	range_tree_unlock_nopreempt(&range->endpoint_tree);

	(void)list_delete_node(&domain->stream_id_list,
			       &endpoint->domain_list_node);

	// Free the endpoint structure, which has no more references.
	partition_free(virtio_iommu->header.partition, endpoint,
		       sizeof(*endpoint));

	virtio_iommu->num_streams--;

	// If the domain is now empty, free it too.
	if (list_is_empty(&domain->stream_id_list)) {
		range_tree_lock_nopreempt(&virtio_iommu->domain_tree);
		(void)range_tree_remove(&virtio_iommu->domain_tree,
					&domain->tree_node);
		range_tree_unlock_nopreempt(&virtio_iommu->domain_tree);
		partition_free(virtio_iommu->header.partition, domain,
			       sizeof(*domain));
	}
}

static void
do_invalidate_scope_type_domain(virtio_iommu_t			*virtio_iommu,
				virtio_iommu_domain_t		*iommu_domain,
				virtio_iommu_invalidate_caches_t caches,
				virtio_iommu_invalidate_flags_t	 flags,
				smmu_v3_asid_t			 asid)
{
	// From SMMUv3 doc
	// Invalidate all CDs referenced by StreamID, for example when
	// decommissioning a device. A separate command must also be issued to
	// invalidate TLB entries for any ASIDs used, either by ASID or all.
	bool do_inv_tlb = false;

	if (virtio_iommu_invalidate_caches_get_pasid(&caches)) {
		// Find all the stream and do the invalidations
		LIST_FOREACH_CONTAINER_BEGIN(virtio_iommu_endpoint_t,
					     &iommu_domain->stream_id_list,
					     virtio_iommu_endpoint,
					     domain_list_node, elem)
			// PASID flag not valid for DOMAIN scope, ignore
			TRACE(DEBUG, INFO,
			      "virtio_iommu:  smmuv3_cfgi_cd_all {:#x}",
			      elem->tree_node.base);
			rcu_read_start();
			smmuv3_cfgi_cd_all(
				elem->range,
				(smmu_v3_stream_id_t)elem->tree_node.base);
			rcu_read_finish();

			do_inv_tlb = true;
		LIST_FOREACH_CONTAINER_END
	}

	if (virtio_iommu_invalidate_caches_get_tlb(&caches) || do_inv_tlb) {
		// If an ASID is provided we use it otherwise do the whole VM
		// space.
		if (virtio_iommu_invalidate_flags_get_asid(&flags)) {
			smmuv3_tlbi_el1_asid(virtio_iommu->iommu,
					     &virtio_iommu->viommu, asid);
		} else {
			smmuv3_tlbi_el1_vmid(virtio_iommu->iommu,
					     &virtio_iommu->viommu);
		}
	}

	// For ATS we need to complete the TLB invalidations first, then
	// do per stream ATC invalidate.
	// Do ATS invalidations for full addr range
	if (iommu_domain->pci_ats != SMMU_V3_STE_EATS_DISABLE) {
		// Ensure the previous invalidations complete first, as per
		// spec; We dont have to wait here, just let it happen in order
		smmuv3_sync(virtio_iommu->iommu, false, false);

		LIST_FOREACH_CONTAINER_BEGIN(virtio_iommu_endpoint_t,
					     &iommu_domain->stream_id_list,
					     virtio_iommu_endpoint,
					     domain_list_node, elem)
			smmu_v3_atc_inv_stream_all(
				elem->range,
				(smmu_v3_stream_id_t)elem->tree_node.base);
		LIST_FOREACH_CONTAINER_END
	}
}

static void
do_invalidate_scope_type_address(virtio_iommu_t			*virtio_iommu,
				 virtio_iommu_domain_t		*iommu_domain,
				 virtio_iommu_invalidate_flags_t flags,
				 smmu_v3_asid_t asid, paddr_t address,
				 uint64_t nr_pages, uint8_t page_size)
{
	if (virtio_iommu_invalidate_flags_get_asid(&flags)) {
		smmuv3_tlbi_el1_asid_address_range(
			virtio_iommu->iommu, &virtio_iommu->viommu, asid,
			address, nr_pages, page_size,
			virtio_iommu_invalidate_flags_get_leaf(&flags));
	} else {
		smmuv3_tlbi_el1_address_range(
			virtio_iommu->iommu, &virtio_iommu->viommu, address,
			nr_pages, page_size,
			virtio_iommu_invalidate_flags_get_leaf(&flags));
	}

	// For ATS we need to complete the TLB invalidations first, then
	// do per stream ATC invalidation.
	// Do ATS invalidations for reduced addr range
	if (iommu_domain->pci_ats != SMMU_V3_STE_EATS_DISABLE) {
		// Ensure the previous invalidations complete first, as per
		// spec. We dont have to wait here, just let it happen in order
		smmuv3_sync(virtio_iommu->iommu, false, false);

		// The span/size here is in number of bits.
		// -> addr + (4096 * 2^Size)
		// -> ATC span is (12 + size) bits. Size = 52 invalidates 64bits
		// at endpoint.
		uint64_t inv_range  = nr_pages * util_bit(page_size);
		uint64_t num4kpages = inv_range >> 12U;
		uint8_t	 log2_size  = 0;

		// Find the minimum LOG2SIZE that will contain all Stream
		// entries
		while (util_bit(log2_size) <= num4kpages) {
			log2_size++;
		}

		LIST_FOREACH_CONTAINER_BEGIN(virtio_iommu_endpoint_t,
					     &iommu_domain->stream_id_list,
					     virtio_iommu_endpoint,
					     domain_list_node, elem)
			smmu_v3_atc_inv_stream_address_range(
				elem->range,
				(smmu_v3_stream_id_t)elem->tree_node.base,
				address, log2_size);
		LIST_FOREACH_CONTAINER_END
	}
}

static virtio_iommu_reply_base_t
virtio_iommu_process_attach(virtio_iommu_t		    *virtio_iommu,
			    const virtio_iommu_req_attach_t *req)
{
	// Calling Attach on an already attached device, to move it, is
	// best effort. We can require the client to detach first.

	TRACE(DEBUG, INFO, "virtio_iommu {:#x}: ATTACH d={:#x} e={:#x} f={:#x}",
	      (register_t)virtio_iommu, req->domain, req->endpoint,
	      virtio_iommu_req_attach_flags_raw(req->flags));

	// TODO:
	// - With ATTACH_TABLE this ATTACH call can still be used to
	// setup bypass for a stream
	// - All other configs should use the ATTACH_TABLE to setup a
	// context
	if (virtio_iommu_req_attach_flags_get_bypass(&req->flags)) {
		TRACE(DEBUG, INFO, "virtio_iommu: TODO: Attach Bypass");
	}

	// TODO - Stream attach Bypass
	// If the BYPASS feature is supported and the Flag is set, do we
	// want to create a ctx for it? Do we just attach the device
	// with BYPASS set in the STE? Need to check to ensure that a
	// bypass is even allowed, we might not want to allow bypass at
	// all
	// - For the moment unsupported.

	virtio_iommu_reply_base_t ret = { 0 };
	virtio_iommu_reply_tail_set_status(&ret.tail,
					   VIRTIO_IOMMU_REQ_STATUS_UNSUPP);

	return ret;
}

static virtio_iommu_reply_base_t
virtio_iommu_process_attach_table(virtio_iommu_t *virtio_iommu,
				  const virtio_iommu_req_attach_table_t *req)
	REQUIRE_SPINLOCK(virtio_iommu -> lock)
{
	TRACE(DEBUG, INFO,
	      "virtio_iommu {:#x}: ATTACH_TABLE 1 d={:#x} e={:#x} ste0={:#x} ste1={:#x}",
	      (register_t)virtio_iommu, req->domain, req->endpoint, req->ste0,
	      req->ste1);
	uint32_t	    guest_domain_id = req->domain;
	smmu_v3_stream_id_t stream_id	    = req->endpoint;

	virtio_iommu_req_status_t virtio_err;

	rcu_read_start();
	smmuv3_stream_range_t *range =
		virtio_iommu_find_range(virtio_iommu, stream_id);
	if (range == NULL) {
		TRACE(DEBUG, INFO,
		      "virtio_iommu: endpoint is not valid for this iommu instance");
		virtio_err = VIRTIO_IOMMU_REQ_STATUS_NOENT;
		goto out;
	}

	// TODO:
	//  - Don't use all memory on Stream/Domain allocation.
	//    Streams allocate memory in the SMMUv3 for STEs
	//    Domains allocate memory in virtio-iommu for metadata
	//  - Check num streams and num domains from RM limits
	//	 Multiple streams can be connected to a single Domain.
	//    If we have more domains than streams, something went wrong
	//    Worst case each stream has its own domain.
	//  - Free a Domain if no more streams are connected to it.
	//  - Stream Bypass should count as a domain as we are allocating
	//    STE memory for it.

	// Extract the STE fields that are defined for ATTACH_TABLE. Note that
	// most STE fields are ignored, particularly Valid and Config.
	smmu_v3_stream_table_entry_t ste = smmu_v3_stream_table_entry_cast(
		req->ste0, req->ste1, 0, 0, 0, 0, 0, 0);
	uint32_t s1cdmax = smmu_v3_stream_table_entry_get_S1CDMax(&ste);
	vmaddr_t ctxptr	 = smmu_v3_stream_table_entry_get_S1ContextPtr(&ste);
	smmu_v3_ste_s1fmt_t s1fmt = smmu_v3_stream_table_entry_get_S1Fmt(&ste);

	TRACE(DEBUG, INFO,
	      "virtio_iommu: process_request ATTACH_TABLE 2 d={:#x} e={:#x} cd_max={:d} cd_ptr={:#x} V={:d}",
	      req->domain, req->endpoint, s1cdmax, ctxptr, s1fmt);

	if (s1cdmax != 0U) {
		// No substream support yet
		virtio_err = VIRTIO_IOMMU_REQ_STATUS_UNSUPP;
		goto out;
	}

	smmu_v3_ste_eats_t eats = smmu_v3_stream_table_entry_get_EATS(&ste);
	// The ATTACH_TABLE specification doesn't mention EATS yet, so it is
	// not clear whether it is supposed to indicate the desired physical
	// EATS mode, or be treated like the EATS field of a S1-only SMMU.
	// For now, we enable EATS if there is anything other than 0 in the
	// field, but let the hypervisor control the actual mode. Only split
	// mode is safe in a multi-VM system (or full with DPT, but that is not
	// implemented in hardware yet).
	bool enable_eats = (eats != SMMU_V3_STE_EATS_DISABLE);

	// TODO:
	// Do some simple validation before we start doing allocations
	// and have to roll-back.

	// Clamp to VM address space
	// Is this valid range controlled by the input-size on probe?
	// TODO
	count_t numbits =
		virtio_iommu->viommu.addrspace->vm_pgtable.control.address_bits;
	ctxptr = ctxptr & util_mask(numbits);

	smmuv3_stream_config_t s1_cfg = {
		.cd_ptr = ctxptr,
		// No substream support yet.
		.s1cdmax = 0,
		.eats	 = enable_eats ? SMMU_V3_STE_EATS_SPLIT
				       : SMMU_V3_STE_EATS_DISABLE,
	};

	virtio_iommu_endpoint_t *endpoint =
		virtio_iommu_find_endpoint(virtio_iommu, range, stream_id);
	bool new_endpoint;

	if ((endpoint == NULL) &&
	    (virtio_iommu->num_streams < virtio_iommu->max_streams)) {
		new_endpoint = true;

		void_ptr_result_t alloc_r =
			partition_alloc(virtio_iommu->header.partition,
					sizeof(*endpoint), alignof(*endpoint));
		if (alloc_r.e != OK) {
			virtio_err = VIRTIO_IOMMU_REQ_STATUS_NOMEM;
			goto fail_alloc_endpoint;
		}
		endpoint	= alloc_r.r;
		*endpoint	= (virtio_iommu_endpoint_t){ 0 };
		endpoint->range = range;

		range_tree_lock_nopreempt(&range->endpoint_tree);
		if (range_tree_insert(&range->endpoint_tree,
				      &endpoint->tree_node, stream_id,
				      1U) != OK) {
			panic("virtio_iommu: endpoint_tree insert failed");
		}
		range_tree_unlock_nopreempt(&range->endpoint_tree);

		// This is to enforce a memory limit for the
		// virtio-iommu instance
		virtio_iommu->num_streams++;
	} else if (endpoint == NULL) {
		TRACE(DEBUG, INFO,
		      "virtio_iommu: too many streams num={:d} max={:d}",
		      virtio_iommu->num_streams, virtio_iommu->max_streams);
		virtio_err = VIRTIO_IOMMU_REQ_STATUS_NOMEM;
		goto fail_alloc_endpoint;
	} else {
		// Found an existing endpoint.
		new_endpoint = false;
	}

	virtio_iommu_domain_t *iommu_domain =
		virtio_iommu_find_domain(virtio_iommu, guest_domain_id);
	bool new_iommu_domain;

	if (iommu_domain == NULL) {
		new_iommu_domain = true;

		void_ptr_result_t ptr_r = partition_alloc(
			virtio_iommu->header.partition, sizeof(*iommu_domain),
			alignof(*iommu_domain));
		if (ptr_r.e != OK) {
			virtio_err = VIRTIO_IOMMU_REQ_STATUS_NOMEM;
			goto fail_alloc_domain;
		}
		(void)memset_s(ptr_r.r, sizeof(*iommu_domain), 0,
			       sizeof(*iommu_domain));
		iommu_domain  = ptr_r.r;
		*iommu_domain = (virtio_iommu_domain_t){ 0 };

		list_init(&iommu_domain->stream_id_list);
		iommu_domain->guest_domain_id = guest_domain_id;
		iommu_domain->pci_ats	      = eats;

		range_tree_lock_nopreempt(&virtio_iommu->domain_tree);
		if (range_tree_insert(&virtio_iommu->domain_tree,
				      &iommu_domain->tree_node, guest_domain_id,
				      1U) != OK) {
			panic("virtio_iommu: domain_tree insert failed");
		}
		range_tree_unlock_nopreempt(&virtio_iommu->domain_tree);
	} else {
		// Found an existing domain
		new_iommu_domain = false;
	}

	error_t err = smmuv3_attach_stream(range, stream_id, &s1_cfg, true);
	if (err != OK) {
		// Something was wrong with the S1 config.
		virtio_err = VIRTIO_IOMMU_REQ_STATUS_UNSUPP;
		goto fail_attach;
	}

	virtio_err = VIRTIO_IOMMU_REQ_STATUS_OK;

	if (endpoint->domain != iommu_domain) {
		if (endpoint->domain != NULL) {
			virtio_iommu_domain_t *old_domain = endpoint->domain;
			(void)list_delete_node(&old_domain->stream_id_list,
					       &endpoint->domain_list_node);

			// If the domain is now empty, remove it too.
			if (list_is_empty(&old_domain->stream_id_list)) {
				range_tree_lock_nopreempt(
					&virtio_iommu->domain_tree);
				(void)range_tree_remove(
					&virtio_iommu->domain_tree,
					&old_domain->tree_node);
				range_tree_unlock_nopreempt(
					&virtio_iommu->domain_tree);
				partition_free(virtio_iommu->header.partition,
					       old_domain, sizeof(*old_domain));
			}
		}

		endpoint->domain = iommu_domain;
		list_insert_at_tail(&iommu_domain->stream_id_list,
				    &endpoint->domain_list_node);
	}

fail_attach:
	if ((virtio_err != VIRTIO_IOMMU_REQ_STATUS_OK) && new_iommu_domain) {
		assert(list_is_empty(&iommu_domain->stream_id_list));

		range_tree_lock_nopreempt(&virtio_iommu->domain_tree);
		(void)range_tree_remove(&virtio_iommu->domain_tree,
					&iommu_domain->tree_node);
		range_tree_unlock_nopreempt(&virtio_iommu->domain_tree);
		partition_free(virtio_iommu->header.partition, iommu_domain,
			       sizeof(*iommu_domain));
	}
fail_alloc_domain:
	if ((virtio_err != VIRTIO_IOMMU_REQ_STATUS_OK) && new_endpoint) {
		range_tree_lock_nopreempt(&range->endpoint_tree);
		(void)range_tree_remove(&range->endpoint_tree,
					&endpoint->tree_node);
		range_tree_unlock_nopreempt(&range->endpoint_tree);
		partition_free(virtio_iommu->header.partition, endpoint,
			       sizeof(*endpoint));

		virtio_iommu->num_streams--;
	}
fail_alloc_endpoint:
out:
	rcu_read_finish();
	virtio_iommu_reply_base_t ret = { 0 };
	virtio_iommu_reply_tail_set_status(&ret.tail, virtio_err);
	return ret;
}

static virtio_iommu_reply_base_t
virtio_iommu_process_detach(virtio_iommu_t		    *virtio_iommu,
			    const virtio_iommu_req_detach_t *req, bool *do_sync)
	REQUIRE_SPINLOCK(virtio_iommu -> lock)
{
	virtio_iommu_req_status_t virtio_err;

	TRACE(DEBUG, INFO,
	      "virtio_iommu: process_request DETACH d={:#x} e={:#x}",
	      req->domain, req->endpoint);
	uint32_t	    guest_domain_id = req->domain;
	smmu_v3_stream_id_t stream_id	    = req->endpoint;

	rcu_read_start();
	smmuv3_stream_range_t *range =
		virtio_iommu_find_range(virtio_iommu, stream_id);
	if (range == NULL) {
		TRACE(DEBUG, INFO,
		      "virtio_iommu: endpoint is not valid for this iommu instance");
		virtio_err = VIRTIO_IOMMU_REQ_STATUS_NOENT;
		goto out;
	}

	virtio_iommu_endpoint_t *endpoint =
		virtio_iommu_find_endpoint(virtio_iommu, range, stream_id);
	if (endpoint == NULL) {
		TRACE(DEBUG, INFO,
		      "virtio_iommu: stream ID is not attached to any domain");
		virtio_err = VIRTIO_IOMMU_REQ_STATUS_INVAL;
		goto out;
	}

	virtio_iommu_domain_t *domain = endpoint->domain;
	assert(domain != NULL);
	if (domain->guest_domain_id != guest_domain_id) {
		TRACE(DEBUG, INFO,
		      "virtio_iommu: detach given the wrong domain ID");
		virtio_err = VIRTIO_IOMMU_REQ_STATUS_INVAL;
		goto out;
	}

	virtio_iommu_do_detach_stream(virtio_iommu, endpoint);

	// Make sure to wait for invalidations after detach
	*do_sync = true;

	TRACE(DEBUG, INFO, "virtio_iommu: Detached sid={:#x}", stream_id);
	virtio_err = VIRTIO_IOMMU_REQ_STATUS_OK;
out:
	rcu_read_finish();
	virtio_iommu_reply_base_t reply = { 0 };
	virtio_iommu_reply_tail_set_status(&reply.tail, virtio_err);
	return reply;
}

static virtio_iommu_reply_probe_t
virtio_iommu_process_probe(virtio_iommu_t		  *virtio_iommu,
			   const virtio_iommu_req_probe_t *req)
	REQUIRE_SPINLOCK(virtio_iommu -> lock)
{
	virtio_iommu_reply_probe_t ret = { 0 };
	virtio_iommu_req_status_t  virtio_err;

	TRACE(DEBUG, INFO, "virtio_iommu: process_request PROBE e={:#x}",
	      req->endpoint);

	rcu_read_start();
	smmuv3_stream_range_t *range =
		virtio_iommu_find_range(virtio_iommu, req->endpoint);
	if (range == NULL) {
		TRACE(DEBUG, INFO,
		      "virtio_iommu: endpoint is not valid for this iommu instance");
		virtio_err = VIRTIO_IOMMU_REQ_STATUS_NOENT;
		goto out;
	}

	// v1.7 IDR registers
	// Probe result could be different per endpoint
	virtio_iommu_probe_hw_arm_smmuv3_t   idrs = { 0 };
	virtio_iommu_probe_property_header_t idrs_header =
		virtio_iommu_probe_property_header_default();
	virtio_iommu_probe_property_header_set_type(
		&idrs_header, VIRTIO_IOMMU_PROBE_PROPERTY_TYPE_HW_ARM_SMMU3);
	size_t probe_len = sizeof(virtio_iommu_probe_hw_arm_smmuv3_t) -
			   sizeof(virtio_iommu_probe_property_header_t);
	virtio_iommu_probe_property_header_set_length(&idrs_header,
						      (uint16_t)probe_len);
	idrs.head = idrs_header;

	// If we can get the IDR info then attach the probe entry, else
	// just return Zeros for it.
	error_t err = virtio_iommu_setup_hw_probe_idrs(virtio_iommu,
						       req->endpoint, &idrs);
	if (err != OK) {
		TRACE(DEBUG, INFO,
		      "virtio_iommu: Probe failed to get IDRs, err={:d}",
		      (register_t)err);
		// Return device error?
		virtio_err = VIRTIO_IOMMU_REQ_STATUS_DEVERR;
		goto out;
	}

	TRACE(DEBUG, INFO, "virtio_iommu: IDR0={:#x}", idrs.idr0);
	virtio_err	  = VIRTIO_IOMMU_REQ_STATUS_OK;
	ret.hw_arm_smmuv3 = idrs;
out:
	rcu_read_finish();
	virtio_iommu_reply_tail_set_status(&ret.tail, virtio_err);

	return ret;
}

static virtio_iommu_reply_base_t
virtio_iommu_process_invalidate(virtio_iommu_t *virtio_iommu,
				const virtio_iommu_req_invalidate_t *req,
				bool				    *do_sync)
	REQUIRE_SPINLOCK(virtio_iommu -> lock)
{
	virtio_iommu_req_status_t virtio_err;

	TRACE(DEBUG, INFO,
	      "virtio_iommu: process_request INVALIDATE 1 d={:#x} scope={:#x} caches={:#x} flags={:#x}",
	      req->domain, virtio_iommu_invalidate_scope_get_type(&req->scope),
	      virtio_iommu_invalidate_caches_raw(req->caches),
	      virtio_iommu_invalidate_flags_raw(req->flags));

	TRACE(DEBUG, INFO,
	      "virtio_iommu: process_request INVALIDATE 2 pasid={:#x} id={:#x} addr={:#x} nr={:d} sz={:d}",
	      req->pasid, req->asid, req->address, req->nr_pages,
	      req->page_size);

	virtio_iommu_invalidate_scope_type_t scope =
		virtio_iommu_invalidate_scope_get_type(&req->scope);
	if ((scope != VIRTIO_IOMMU_INVALIDATE_SCOPE_TYPE_DOMAIN) &&
	    (scope != VIRTIO_IOMMU_INVALIDATE_SCOPE_TYPE_ADDRESS)) {
		TRACE(DEBUG, INFO, "virtio_iommu: Unsupported Scope type");
		virtio_err = VIRTIO_IOMMU_REQ_STATUS_INVAL;
		goto out;
	}

	// Find the user domain
	virtio_iommu_domain_t *iommu_domain =
		virtio_iommu_find_domain(virtio_iommu, req->domain);
	if (iommu_domain == NULL) {
		TRACE(DEBUG, INFO,
		      "virtio_iommu: Inv could not find guest domain id");
		virtio_err = VIRTIO_IOMMU_REQ_STATUS_INVAL;
		goto out;
	}

	error_t err;

	switch (scope) {
	case VIRTIO_IOMMU_INVALIDATE_SCOPE_TYPE_DOMAIN: {
		// Work through the combinations
		// pasid, address, nr_pages, page_size fields
		// should be ignored
		do_invalidate_scope_type_domain(virtio_iommu, iommu_domain,
						req->caches, req->flags,
						(smmu_v3_asid_t)req->asid);
		err = OK;
		break;
	}
	case VIRTIO_IOMMU_INVALIDATE_SCOPE_TYPE_ADDRESS: {
		// Only allow limited page sizes for now.
		// Then Range operation may have to break them down.
		// Make thise proper errors, not assert.
		// Check sizes against the Mask we provide in the probe.
		// This should be based on the Hardware support.
		if (req->nr_pages == 0U) {
			TRACE(DEBUG, INFO, "virtio_iommu: Bad nr_pages");
			err = ERROR_ARGUMENT_INVALID;
			break;
		}

		// Check these against actual advertised page sizes
		if (util_bit(req->page_size) != VIRTIO_IOMMU_SMMU_MAP_SIZE) {
			TRACE(DEBUG, INFO, "virtio_iommu: Bad page_size");
			err = ERROR_ARGUMENT_INVALID;
			break;
		}

		if (!virtio_iommu_invalidate_caches_get_tlb(&req->caches)) {
			TRACE(DEBUG, INFO,
			      "virtio_iommu: Inv Address expected Caches TLB set");
			err = ERROR_ARGUMENT_INVALID;
			break;
		}

		do_invalidate_scope_type_address(virtio_iommu, iommu_domain,
						 req->flags,
						 (smmu_v3_asid_t)req->asid,
						 req->address, req->nr_pages,
						 req->page_size);
		err = OK;

		break;
	}
	case VIRTIO_IOMMU_INVALIDATE_SCOPE_TYPE_PASID:
		// TODO:
		// Work through the combinations
		// address, nr_pages, page_size fields should be ignored
		TRACE(DEBUG, INFO,
		      "virtio_iommu: Inv Scope PASID not yet suported");
		err = ERROR_UNIMPLEMENTED;
		break;

	default:
		TRACE(DEBUG, INFO, "virtio_iommu: Invalid inv scope type={:d}",
		      scope);
		err = ERROR_ARGUMENT_INVALID;
		break;
	}

	if (err == OK) {
		// Wait for invalidations to complete
		*do_sync   = true;
		virtio_err = VIRTIO_IOMMU_REQ_STATUS_OK;
	} else if (err == ERROR_ARGUMENT_INVALID) {
		virtio_err = VIRTIO_IOMMU_REQ_STATUS_INVAL;
	} else if (err == ERROR_UNIMPLEMENTED) {
		virtio_err = VIRTIO_IOMMU_REQ_STATUS_UNSUPP;
	} else {
		virtio_err = VIRTIO_IOMMU_REQ_STATUS_DEVERR;
	}

out:;
	virtio_iommu_reply_base_t reply = { 0 };
	virtio_iommu_reply_tail_set_status(&reply.tail, virtio_err);
	return reply;
}

static size_t
virtio_iommu_process_request(virtio_iommu_t		  *virtio_iommu,
			     virtio_iommu_req_type_t	   req_type,
			     const virtio_iommu_request_t *req, size_t req_size,
			     virtio_iommu_reply_t *reply, size_t reply_space,
			     bool *do_sync)
	REQUIRE_SPINLOCK(virtio_iommu -> lock)
{
	assert(virtio_iommu != NULL);
	assert(req != NULL);
	assert(reply != NULL);

	// Calculate and validate the request and reply sizes
	// TODO: request size
	size_t reply_size, req_expected_size;
	switch (req_type) {
	case VIRTIO_IOMMU_REQ_TYPE_ATTACH:
		reply_size	  = sizeof(reply->base);
		req_expected_size = sizeof(req->attach) - reply_size;
		break;
	case VIRTIO_IOMMU_REQ_TYPE_ATTACH_TABLE:
		reply_size	  = sizeof(reply->base);
		req_expected_size = sizeof(req->attach_table) - reply_size;
		break;
	case VIRTIO_IOMMU_REQ_TYPE_DETACH:
		reply_size	  = sizeof(reply->base);
		req_expected_size = sizeof(req->detach) - reply_size;
		break;
	case VIRTIO_IOMMU_REQ_TYPE_MAP:
		reply_size	  = sizeof(reply->base);
		req_expected_size = sizeof(req->map) - reply_size;
		break;
	case VIRTIO_IOMMU_REQ_TYPE_UNMAP:
		reply_size	  = sizeof(reply->base);
		req_expected_size = sizeof(req->unmap) - reply_size;
		break;
	case VIRTIO_IOMMU_REQ_TYPE_INVALIDATE:
		reply_size	  = sizeof(reply->base);
		req_expected_size = sizeof(req->invalidate) - reply_size;
		break;
	case VIRTIO_IOMMU_REQ_TYPE_PROBE:
		reply_size	  = sizeof(reply->probe);
		req_expected_size = sizeof(req->probe) - reply_size;
		break;
	case VIRTIO_IOMMU_REQ_TYPE_NONE:
	default:
		req_expected_size = 0U;
		// Returning a 0-sized reply will signal an invalid message
		// error to the VM without making assumptions about the reply
		// layout (i.e. it may be a new message type that doesn't use
		// the base reply type).
		reply_size = 0U;
		break;
	}

	if (req_expected_size > req_size) {
		// Request was truncated.
		reply_size = 0U;
		goto out;
	}

	if (reply_space < reply_size) {
		// Reply can't be sent; consume the buffer without writing
		// anything to it, which the driver is required to treat as an
		// error.
		reply_size = 0U;
		goto out;
	}

	switch (req_type) {
	case VIRTIO_IOMMU_REQ_TYPE_ATTACH:
		reply->base =
			virtio_iommu_process_attach(virtio_iommu, &req->attach);
		break;
	case VIRTIO_IOMMU_REQ_TYPE_ATTACH_TABLE:
		reply->base = virtio_iommu_process_attach_table(
			virtio_iommu, &req->attach_table);
		break;
	case VIRTIO_IOMMU_REQ_TYPE_DETACH:
		reply->base = virtio_iommu_process_detach(
			virtio_iommu, &req->detach, do_sync);
		break;
	case VIRTIO_IOMMU_REQ_TYPE_MAP:
	case VIRTIO_IOMMU_REQ_TYPE_UNMAP:
		virtio_iommu_reply_tail_set_status(
			&reply->base.tail, VIRTIO_IOMMU_REQ_STATUS_UNSUPP);
		break;
	case VIRTIO_IOMMU_REQ_TYPE_PROBE:
		reply->probe =
			virtio_iommu_process_probe(virtio_iommu, &req->probe);
		break;
	case VIRTIO_IOMMU_REQ_TYPE_INVALIDATE:
		reply->base = virtio_iommu_process_invalidate(
			virtio_iommu, &req->invalidate, do_sync);
		break;
	case VIRTIO_IOMMU_REQ_TYPE_NONE:
	default:
		TRACE(DEBUG, INFO, "virtio_iommu: Unknown request type={:d}",
		      req_type);
		break;
	}

out:
	return reply_size;
}

void
virtio_iommu_disable_all_streams(virtio_iommu_t *virtio_iommu)
{
	TRACE_AND_LOG(DEBUG, INFO,
		      "virtio_iommu: Disable all IOMMU device streams");

	smmuv3_stream_range_t *range;
	rcu_read_start();
	range_tree_foreach_container (range,
				      &virtio_iommu->viommu.endpoint_ranges,
				      smmuv3_stream_range, virt_node) {
		virtio_iommu_disable_range_streams(virtio_iommu, range);
	}
	rcu_read_finish();
}

void
virtio_iommu_disable_range_streams(virtio_iommu_t	 *virtio_iommu,
				   smmuv3_stream_range_t *range)
{
	// This is called before a range is unbound.
	virtio_iommu_endpoint_t *endpoint;
	rcu_read_start();
	range_tree_foreach_container (endpoint, &range->endpoint_tree,
				      virtio_iommu_endpoint, tree_node) {
		virtio_iommu_do_detach_stream(virtio_iommu, endpoint);
	}
	rcu_read_finish();
}

static void
virtio_iommu_process_desc_chain(virtio_iommu_t *virtio_iommu, index_t qnum,
				index_t first_desc_idx, bool enable_notif,
				bool *do_sync)
	REQUIRE_SPINLOCK(virtio_iommu -> lock)
{
	virtio_iommu_request_t req     = { 0 };
	uint8_t		      *req_buf = (uint8_t *)&req;

	virtio_iommu_reply_t reply     = { 0 };
	uint8_t		    *reply_buf = (uint8_t *)&reply;

	// Set reply_size to 0 initially so that in any error cose, we will post
	// a used buffer notification with 0 bytes used. The driver will
	// interpret this as a buffer access error.
	size_t reply_size = 0U;

	const virtio_virtq_desc_chain_t chain = virtio_virtq_read_desc_chain(
		virtio_iommu, qnum, first_desc_idx, req_buf, sizeof(req));

	if (chain.req_size < sizeof(req.base.head)) {
		// Request is truncated, we can't determine the type.
		TRACE(DEBUG, INFO, "virtq: truncated request, ignored");
		goto out;
	}

	if (chain.reply_space < sizeof(reply.base.tail)) {
		// Reply buffer is too short to return an iommu result
		TRACE(DEBUG, INFO, "virtq: no reply space, ignored");
		goto out;
	}

	// Validate the request type and ignore invalid requests
	virtio_iommu_req_type_result_t request_type_r =
		virtio_iommu_req_type_cast_safe(
			virtio_iommu_req_head_get_req(&req.base.head));
	if (request_type_r.e != OK) {
		TRACE(DEBUG, INFO, "virtq: bad request type {:d}, ignored",
		      virtio_iommu_req_head_get_req(&req.base.head));
		goto out;
	}
	virtio_iommu_req_type_t request_type = request_type_r.r;

	reply_size = virtio_iommu_process_request(virtio_iommu, request_type,
						  &req, chain.req_size, &reply,
						  chain.reply_space, do_sync);

out:
	virtio_virtq_write_reply(virtio_iommu, qnum, first_desc_idx, &chain,
				 reply_buf, reply_size);

	// Raise the used buffer interrupt if it is enabled for this chain
	if (enable_notif) {
		// Do we need to wait for the SMMU commands to complete first?
		if (*do_sync) {
			// Send a Sync and wait here until it is complete.
			// This is used to wait for batch map/unmaps and
			// invalidations for example.
			smmuv3_sync(virtio_iommu->iommu, false, true);
			*do_sync = false;
		}

		error_t err = virtio_queue_ready(&virtio_iommu->virtio, qnum);
		if (err != OK) {
			TRACE(DEBUG, INFO,
			      "virtio_iommu: Failed to send virtio_queue_ready qnum={:d}",
			      qnum);
		}
	}
}

void
virtio_iommu_process_queue_notification(virtio_iommu_t *virtio_iommu,
					index_t		qnum)
{
	assert(virtio_iommu != NULL);

	// IOMMU only has one Request Queue, ignore writes to the event Queue
	if (qnum != VIRTIO_IOMMU_REQUEST_QUEUE) {
		goto out;
	}

	virtio_iommu_queue_t *q = &virtio_iommu->queues[qnum];

	// Under the virtio instance big lock
	// TODO: finer grained locking

	virtio_virtq_avail_t avail = { 0 };
	// Only copy up to the size negotiated
	size_t avail_sz = sizeof(avail.flags) + sizeof(avail.idx) +
			  (sizeof(avail.ring[0]) * q->size);
	error_t err = useraccess_copy_from_guest_ipa(
			      virtio_iommu->viommu.addrspace, &avail,
			      sizeof(avail), virtio_iommu->queues[qnum].drv,
			      avail_sz, false, false)
			      .e;
	if (err != OK) {
		TRACE(DEBUG, INFO, "user access fail {:d} err={:d}}", __LINE__,
		      (register_t)err);
		goto out;
	}

	// Acquire fence, to ensure that all subsequent accesses to the queue
	// are ordered after the read of the avail head. This matches a release
	// fence / write barrier that is required in the frontend driver before
	// writing to the available ring.
	atomic_thread_fence(memory_order_acquire);

	index_t drv_idx	 = q->curr_drv_idx;
	index_t idx_from = drv_idx % q->size;
	index_t idx_to	 = avail.idx % q->size;

	// Deal with wrapping
	if (idx_to < idx_from) {
		idx_to += q->size;
	}

	if (drv_idx == idx_to) {
		goto out;
	}

	bool do_sync = false;

	for (index_t idx = idx_from; idx < idx_to; idx++) {
		// The queue might wrap around, do mod size
		index_t desc_entry_idx = avail.ring[idx % q->size];
		virtio_iommu_process_desc_chain(
			virtio_iommu, qnum, desc_entry_idx,
			!virtio_virtq_avail_flags_get_no_interrupt(
				&avail.flags),
			&do_sync);
	}

	// We did some work, update the queue index
	q->curr_drv_idx = avail.idx;

	// Do we need to wait for the SMMU commands to complete?
	if (do_sync) {
		// Send a Sync and wait here until it is complete.
		// This is used to wait for batch map/unmaps and
		// invalidations for example.
		smmuv3_sync(virtio_iommu->iommu, false, true);
	}

out:
	// TODO: finer grained locking
	return;
}

static void
virtio_iommu_send_queue_notification(virtio_iommu_t		*virtio_iommu,
				     virtio_iommu_fault_reason_t reason,
				     uint32_t endpoint, uint64_t fault_addr)
	REQUIRE_LOCK(virtio_iommu -> lock)
{
	error_t err;

	assert(virtio_iommu != NULL);

	// Event qnum = 1, from the virtio-iommu spec
	index_t		      qnum = VIRTIO_IOMMU_EVENT_QUEUE;
	virtio_iommu_queue_t *q	   = &virtio_iommu->queues[qnum];

	// Under the Event big lock
	// TODO: finer grained locking

	// The driver provides bufs for us to use for events, get one, populate
	// it and send it off. If no bufs are available we can wait or drop the
	// event. Drop for now/panic?

	virtio_virtq_avail_t avail = { 0 };
	size_t avail_sz		   = sizeof(avail.flags) + sizeof(avail.idx) +
			  (sizeof(avail.ring[0]) * q->size);
	err = useraccess_copy_from_guest_ipa(virtio_iommu->viommu.addrspace,
					     &avail, sizeof(avail),
					     virtio_iommu->queues[qnum].drv,
					     avail_sz, false, false)
		      .e;
	if (err != OK) {
		TRACE(DEBUG, INFO, "user access fail {:d} err={:d}}", __LINE__,
		      (register_t)err);
		goto out;
	}

	// Acquire fence, to ensure that all subsequent accesses to the queue
	// are ordered after the read of the avail head. This matches a release
	// fence / write barrier that is required in the frontend driver before
	// writing to the available ring.
	atomic_thread_fence(memory_order_acquire);

	// The driver can disable notifications for this Q
	bool send_notif =
		!virtio_virtq_avail_flags_get_no_interrupt(&avail.flags);

	index_t dev_idx	 = q->curr_dev_idx;
	index_t idx_from = dev_idx % q->size;

	// avail.idx indicates where the driver would put the next descriptor
	// entry in the ring
	index_t idx_to = ((index_t)avail.idx - 1U) % q->size;

	if (idx_from == idx_to) {
		// No free buffers. Drop event for now. We can also choose to
		// wait for a buffer to become available.
		TRACE(DEBUG, INFO, "virtio_iommu: no free buf, dropped event");
		goto out;
	}
	const index_t			first_desc_idx = avail.ring[idx_from];
	const virtio_virtq_desc_chain_t chain = virtio_virtq_read_desc_chain(
		virtio_iommu, qnum, first_desc_idx, NULL, 0U);

	// Check the buf is big enough and writeable
	if (chain.reply_space < sizeof(virtio_iommu_fault_t)) {
		TRACE(DEBUG, INFO, "virtio_iommu: bad buf, dropped event");
		goto out;
	}

	virtio_iommu_fault_t event = virtio_iommu_fault_default();
	virtio_iommu_fault_set_reason(&event, reason);

	virtio_iommu_fault_flags_t fflags = virtio_iommu_fault_flags_default();
	virtio_iommu_fault_flags_set_address(&fflags, true);
	virtio_iommu_fault_flags_set_write(&fflags, true);

	virtio_iommu_fault_set_flags(&event, fflags);
	virtio_iommu_fault_set_endpoint(&event, endpoint);
	virtio_iommu_fault_set_address(&event, fault_addr);

	// Set the event
	virtio_virtq_write_reply(virtio_iommu, qnum, first_desc_idx, &chain,
				 (const uint8_t *)&event, sizeof(event));

	// Raise the Q interrupt?
	if (send_notif) {
		err = virtio_queue_ready(&virtio_iommu->virtio, qnum);
		if (err != OK) {
			TRACE(DEBUG, INFO,
			      "virtio_iommu: Failed to send virtio_queue_ready qnum={:d}",
			      qnum);
		}
	}

out:
	// Under the big lock
	// TODO: finer grained locking

	return;
}

bool
virtio_iommu_handle_smmuv3_stage1_event(const smmuv3_stream_range_t *range,
					smmu_v3_stream_id_t	     stream_id,
					const smmu_v3_event_t	    *event)
{
	bool handled;

	assert((range != NULL) && (range->viommu != NULL));
	virtio_iommu_t *virtio_iommu =
		virtio_iommu_container_of_viommu(range->viommu);

	spinlock_acquire(&virtio_iommu->lock);

	TRACE(DEBUG, INFO,
	      "virtio_iommu_handle_smmuv3_fault: Raise event to Guest {:#x}",
	      smmu_v3_event_base_get_event(&event->base));

	// For the moment we only raise Translation Events with the
	// Client. Everything else is unexpected.
	if (smmu_v3_event_base_get_event(&event->base) !=
	    SMMU_V3_EVENT_ID_F_TRANSLATION) {
		handled = false;
		goto out;
	}

	virtio_iommu_send_queue_notification(
		virtio_iommu, VIRTIO_IOMMU_FAULT_REASON_MAPPING, stream_id,
		smmu_v3_event_f_translation_get_InputAddr(
			&event->f_translation));
	handled = true;

out:
	spinlock_release(&virtio_iommu->lock);
	return handled;
}
#else // if !SMMU_V3_ENABLE

// These functions are used when the SMMUv3 is in Bypass mode
// The HW could be configured at boot, but will not receive any commands

void
virtio_iommu_disable_all_streams(virtio_iommu_t *virtio_iommu)
{
	(void)virtio_iommu;
}

void
virtio_iommu_process_queue_notification(virtio_iommu_t *virtio_iommu,
					index_t		qnum)
{
	(void)virtio_iommu;
	(void)qnum;
}

#endif // !SMMU_V3_ENABLE
