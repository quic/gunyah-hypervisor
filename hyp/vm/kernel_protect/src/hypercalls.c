// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause
#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcall_def.h>
#include <hypconstants.h>
#include <hypregisters.h>
#include <hyprights.h>

#include <atomic.h>
#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <log.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <power.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <trace_helpers.h>

static error_t
kernel_protect_init_objects(addrspace_t *addrspace)
{
#if defined(VERBOSE) && VERBOSE
	TRACE_AND_LOG(ERROR, WARN, "KP Init object Called");
#endif
	error_t err;
	assert((addrspace->kp_extent_info == NULL) &&
	       (addrspace->kp_asset_info == NULL));
	partition_t *partition = addrspace->header.partition;

	void_ptr_result_t alloc_ret =
		partition_alloc(partition, sizeof(*addrspace->kp_extent_info),
				alignof(*addrspace->kp_extent_info));
	if (alloc_ret.e != OK) {
#if defined(VERBOSE) && VERBOSE
		TRACE_AND_LOG(
			ERROR, WARN,
			"Not enough memory for vm_image protection info data ({:d} bytes)",
			sizeof(*addrspace->kp_extent_info));
#endif
		err = alloc_ret.e;
		goto err;
	}

	addrspace->kp_extent_info = (kp_mem_info_t *)alloc_ret.r;
	errno_t err_mem		  = memset_s(addrspace->kp_extent_info,
					     sizeof(*addrspace->kp_extent_info), 0,
					     sizeof(*addrspace->kp_extent_info));
	if (err_mem != 0) {
		panic("Error in memset_s operation!");
	}

	alloc_ret = partition_alloc(partition,
				    sizeof(*addrspace->kp_asset_info),
				    alignof(*addrspace->kp_asset_info));
	if (alloc_ret.e != OK) {
#if defined(VERBOSE) && VERBOSE
		TRACE_AND_LOG(
			ERROR, WARN,
			"Not enough memory for vm_image protection info data ({:d} bytes)",
			sizeof(*addrspace->kp_asset_info));
#endif
		err = alloc_ret.e;
		partition_free(partition, addrspace->kp_extent_info,
			       sizeof(*addrspace->kp_extent_info));
		addrspace->kp_extent_info = NULL;
		goto err;
	}
	addrspace->kp_asset_info = (kernel_asset_info_t *)alloc_ret.r;
	err_mem			 = memset_s(addrspace->kp_asset_info,
					    sizeof(*addrspace->kp_asset_info), 0,
					    sizeof(*addrspace->kp_asset_info));
	if (err_mem != 0) {
		panic("Error in memset_s operation!");
	}
	err = OK;
err:
	return err;
}

static void
kernel_protect_deinit_objects(addrspace_t *addrspace)
{
	partition_t *partition = addrspace->header.partition;

	if (addrspace->kp_asset_info != NULL) {
		partition_free(partition, addrspace->kp_asset_info,
			       sizeof(*addrspace->kp_asset_info));
		addrspace->kp_asset_info = NULL;
	}

	if (addrspace->kp_extent_info != NULL) {
		partition_free(partition, addrspace->kp_extent_info,
			       sizeof(*addrspace->kp_extent_info));
		addrspace->kp_extent_info = NULL;
	}
}

error_t
hypercall_addrspace_protect_vm_assets(cap_id_t addrspace_cap,
				      cap_id_t memextent_ro_cap,
				      cap_id_t memextent_rw_cap,
				      cap_id_t memextent_rx_cap,
				      cap_id_t memextent_rwx_cap)
{
	error_t err;

	cspace_t    *cspace    = cspace_get_self();
	addrspace_t *addrspace = NULL;

	memextent_t *ro	 = NULL;
	memextent_t *rw	 = NULL;
	memextent_t *rx	 = NULL;
	memextent_t *rwx = NULL;

	addrspace_ptr_result_t vm_ret;
#if defined(VERBOSE) && VERBOSE
	TRACE_AND_LOG(ERROR, WARN, "GH_KERNEL_PROTECT: hypercall start");
#endif

	/* Lookup VM address space */
	vm_ret = cspace_lookup_addrspace_any(cspace, addrspace_cap,
					     CAP_RIGHTS_ADDRSPACE_LOOKUP);
	if (compiler_unexpected(vm_ret.e != OK)) {
		err = vm_ret.e;
		TRACE(ERROR, WARN, "Address space lookup failed ({:d})",
		      (register_t)err);
		goto out;
	}

	addrspace	     = vm_ret.r;
	object_state_t state = atomic_load_relaxed(&addrspace->header.state);
	if (state != OBJECT_STATE_INIT) {
		err = ERROR_OBJECT_STATE;
		TRACE(ERROR, WARN, "Address space state invalid ({:d})",
		      (register_t)err);
		goto out;
	}

	/* Check kernel-protect state: kp_extent_info is allocated iff KP is
	 * active */
	if (addrspace->kp_extent_info != NULL) {
		err = ERROR_OBJECT_STATE;
		TRACE(ERROR, WARN, "KP object already initialized {:d}",
		      (register_t)err);
		goto out;
	}

	err = kernel_protect_init_objects(addrspace);
	if (compiler_unexpected(err != OK)) {
		TRACE(ERROR, WARN, "KP object initialization failed ({:d})",
		      (register_t)err);
		goto out;
	}

	/* RO memextent */
	{
		memextent_ptr_result_t r = cspace_lookup_memextent(
			cspace, memextent_ro_cap, CAP_RIGHTS_MEMEXTENT_ATTACH);
		if (compiler_unexpected(r.e != OK)) {
			err = r.e;
			TRACE(ERROR, WARN, "RO memextent lookup failed ({:d})",
			      (register_t)err);
			goto out;
		}
		ro = r.r;
		addrspace->kp_extent_info->memext_ro_region =
			object_get_memextent_additional(ro);
	}

	/* RW memextent */
	{
		memextent_ptr_result_t r = cspace_lookup_memextent(
			cspace, memextent_rw_cap, CAP_RIGHTS_MEMEXTENT_ATTACH);
		if (compiler_unexpected(r.e != OK)) {
			err = r.e;
			TRACE(ERROR, WARN, "RW memextent lookup failed ({:d})",
			      (register_t)err);
			goto out;
		}
		rw = r.r;
		addrspace->kp_extent_info->memext_rw_region =
			object_get_memextent_additional(rw);
	}

	/* RX memextent */
	{
		memextent_ptr_result_t r = cspace_lookup_memextent(
			cspace, memextent_rx_cap, CAP_RIGHTS_MEMEXTENT_ATTACH);
		if (compiler_unexpected(r.e != OK)) {
			err = r.e;
			TRACE(ERROR, WARN, "RX memextent lookup failed ({:d})",
			      (register_t)err);
			goto out;
		}
		rx = r.r;
		addrspace->kp_extent_info->memext_rx_region =
			object_get_memextent_additional(rx);
	}

	/* RWX memextent */
	{
		memextent_ptr_result_t r = cspace_lookup_memextent(
			cspace, memextent_rwx_cap, CAP_RIGHTS_MEMEXTENT_ATTACH);
		if (compiler_unexpected(r.e != OK)) {
			err = r.e;
			TRACE(ERROR, WARN, "RWX memextent lookup failed ({:d})",
			      (register_t)err);
			goto out;
		}
		rwx = r.r;
		addrspace->kp_extent_info->memext_rwx_region =
			object_get_memextent_additional(rwx);
	}

#if defined(VERBOSE) && VERBOSE
	TRACE_AND_LOG(ERROR, WARN,
		      "GH_KERNEL_PROTECT: hypercall completed successfully");
#endif
out:
	/* Release only what we actually acquired */
	if (rwx != NULL) {
		object_put_memextent(rwx);
	}
	if (rx != NULL) {
		object_put_memextent(rx);
	}
	if (rw != NULL) {
		object_put_memextent(rw);
	}
	if (ro != NULL) {
		object_put_memextent(ro);
	}
	if (addrspace != NULL) {
		if (addrspace->kp_extent_info == NULL) {
			kernel_protect_deinit_objects(addrspace);
		}
		object_put_addrspace(addrspace);
	}
	return err;
}
