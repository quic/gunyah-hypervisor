// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcall_def.h>
#include <hypcontainers.h>
#include <hyprights.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <list.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <platform_irq.h>
#include <qcbor.h>
#include <range_tree.h>
#include <rcu.h>
#include <spinlock.h>
#include <trace.h>
#include <util.h>
#include <vdevice.h>
#include <vgic_its.h>

#include <asm/barrier.h>

#include "event_handlers.h"
#include "gicv3_its.h"
#include "useraccess.h"
#include "vgic_its_base.h"

static error_t
vgic_its_reserve_unused_device(vgic_its_t		   *vgic_its,
			       platform_msi_controller_id_t phys_index)
{
	error_t err;

	// Reserve an unused device and allocate a dummy event on the physical
	// ITS. For each virtual ITS that is mapped to a physical ITS, a unique
	// event is reserved, up to the number of supported event bits.
	platform_msi_id_result_t msi_id_r = gicv3_its_reserve_unused_device(
		phys_index,
		(platform_msi_device_id_t)(util_bit(VGIC_ITS_EVENT_BITS) - 1U));
	if (msi_id_r.e != OK) {
		err = msi_id_r.e;
		goto out;
	}

	platform_msi_device_id_t device_id =
		platform_msi_id_get_device_id(&msi_id_r.r);
	platform_msi_event_id_t event_id =
		platform_msi_id_get_event_id(&msi_id_r.r);
	platform_msi_id_set_its_index(&vgic_its->dummy_event, phys_index);
	platform_msi_id_set_device_id(&vgic_its->dummy_event, device_id);
	platform_msi_id_set_event_id(&vgic_its->dummy_event, event_id);
	vgic_its->dummy_event_mapped_lpi = 0U;
	err				 = OK;
out:

	return err;
}

error_t
vgic_its_base_handle_object_create_vgic_its(vgic_its_create_t vgic_its_create)
{
	error_t	    err	     = OK;
	vgic_its_t *vgic_its = vgic_its_create.vgic_its;

	// When called from a hypercall, vgic_its_create.phys_index is
	// initialized to VGIC_ITS_MAX_NUM to indicate an invalid phys_index.
	// The phys_index will be set when a device is bound to the vITS.
	if (vgic_its_create.phys_index > VGIC_ITS_MAX_NUM) {
		err = ERROR_ARGUMENT_INVALID;
		goto fail;
	}

	atomic_store_relaxed(&vgic_its->phys_index, vgic_its_create.phys_index);
	spinlock_init(&vgic_its->lock);
	vdevice_init(&vgic_its->gits_device);

fail:
	return err;
}

void
vgic_its_base_handle_object_get_defaults_vgic_its(
	vgic_its_create_t *vgic_its_create)
{
	assert(vgic_its_create != NULL);
	vgic_its_create->phys_index = VGIC_ITS_MAX_NUM;
}

error_t
vgic_its_base_handle_object_activate_vgic_its(vgic_its_t *vgic_its)
{
	error_t err = range_tree_init(&vgic_its->device_tree,
				      vgic_its->header.partition);

	if (OK != err) {
		goto fail;
	}

	// The ITS is initially quiescent.
	GITS_CTLR_set_Quiescent(&vgic_its->ctlr, true);

fail:
	if (err != OK) {
		vgic_its_base_handle_object_deactivate_vgic_its(vgic_its);
	}
	return err;
}

static error_t
vgic_its_unmap_events(const vgic_its_t *vgic_its, platform_msi_device_t *device)
	REQUIRE_SPINLOCK(vgic_its -> lock)
{
	error_t			     err;
	platform_msi_controller_id_t phys_index =
		atomic_load_consume(&vgic_its->phys_index);
	platform_msi_id_t msi_id = platform_msi_id_default();
	platform_msi_id_set_its_index(&msi_id, phys_index);
	platform_msi_id_set_device_id(&msi_id, device->device_id);

	BITMAP_FOREACH_SET_BEGIN(event, device->vgic_its_mapped_events,
				 (index_t)util_bit(VGIC_ITS_EVENT_BITS))
		platform_msi_id_set_event_id(&msi_id, event);
		err = gicv3_its_discard(msi_id);
		if (OK != err) {
			goto out;
		}
	BITMAP_FOREACH_SET_END

	errno_t err_mem = memset_s(device->vgic_its_mapped_events,
				   sizeof(device->vgic_its_mapped_events), 0,
				   sizeof(device->vgic_its_mapped_events));

	if (err_mem != 0) {
		panic("Error in memset_s operation!");
	}

	err = OK;
out:
	return err;
}

static count_result_t
vgic_its_unbind_devices(vgic_its_t *vgic_its, index_t device_id_start,
			count_t device_count)
{
	count_result_t res;
	count_t	       unmapped_count = 0;

	platform_msi_controller_id_t phys_index =
		atomic_load_consume(&vgic_its->phys_index);
	const platform_msi_controller_t *pits =
		platform_irq_msi_devices(phys_index);

	if (pits == NULL) {
		res = count_result_error(ERROR_ARGUMENT_INVALID);
		goto out;
	}

	spinlock_acquire(&vgic_its->lock);

	rcu_read_start();
	range_tree_lookup_result_t lookup = range_tree_lookup(
		&vgic_its->device_tree, device_id_start, device_count);

	// Only allow the unbind if it exactly matches an existing range.
	// TODO: permit partial unbinding of existing ranges.
	if ((lookup.node == NULL) || (lookup.node->base != device_id_start) ||
	    (lookup.node->size != device_count)) {
		res = count_result_error(ERROR_ARGUMENT_INVALID);
		rcu_read_finish();
		goto out_locked;
	}

	vgic_its_device_range_t *range =
		vgic_its_device_range_container_of_its_node(lookup.node);

	// Don't allow unbinding of a range that is already being unbound.
	if (range->unbound) {
		res = count_result_error(ERROR_IDLE);
		rcu_read_finish();
		goto out_locked;
	}
	range->unbound = true;

	// We know that the range won't be freed (because we hold a reference to
	// the ITS and we have set the unbound flag so nobody else can unbind
	// the range), so we can drop the RCU critical section.
	rcu_read_finish();

	// DISCARD for all mapped events
	for (index_t i = 0; i < device_count; i++) {
		error_t		       err;
		platform_msi_device_t *device =
			&pits->devices[range->device_index + i];

		assert_debug(atomic_load_acquire(&device->vgic_its_owner) ==
			     vgic_its);

		// DISCARD for all mapped events
		err = vgic_its_unmap_events(vgic_its, device);
		if (OK != err) {
			res = count_result_error(err);
			goto out_locked;
		}

		unmapped_count++;

		// Drop the lock to permit preemption
		spinlock_release(&vgic_its->lock);
		asm_yield();
		spinlock_acquire(&vgic_its->lock);
	}

	res = count_result_ok(unmapped_count);

	// Remove the range from the tree. This will make it inaccessible to
	// any further commands by this ITS.
	range_tree_lock_nopreempt(&vgic_its->device_tree);
	error_t range_err =
		range_tree_remove(&vgic_its->device_tree, lookup.node);
	assert(OK == range_err);
	range_tree_unlock_nopreempt(&vgic_its->device_tree);

	// Schedule deletion of the range. The ownership of the devices will be
	// released when the range is deleted.
	rcu_enqueue(&range->rcu,
		    RCU_UPDATE_CLASS_VGIC_ITS_DEVICE_RANGE_CLEANUP);

out_locked:
	spinlock_release(&vgic_its->lock);

	// Ownership release takes a grace period, ensure it's visible
	// FIXME: QC Gunyah issue #181
	rcu_sync();
out:
	res.r = unmapped_count;

	return res;
}

static GITS_BASER_t
vgic_its_baser_default(GITS_BASER_Type_t type, size_t entry_size)
{
	GITS_BASER_t baser = GITS_BASER_default();

	// The following fields of GITS_BASER are treated as RO. The others,
	// including the Valid bit, are initialised to 0 and must be set by
	// the VM.
	GITS_BASER_set_Entry_Size(&baser, entry_size - 1U);
	GITS_BASER_set_Type(&baser, type);
	// Page size is fixed at 4KB, regardless of the S1 or S2 page size
	// (this is the most space-efficient, especially for indirect tables)
	GITS_BASER_set_Page_Size(&baser, GITS_BASER_PAGE_SIZE_SIZE_4KB);
	// Shareability == 1: Inner shareable
	GITS_BASER_set_Shareability(&baser, 1U);
	// OuterCache == 0: Inner and outer attributes are the same
	GITS_BASER_set_OuterCache(&baser, 0U);
	// InnerCache == 7: Inner write back, read + write alloc
	GITS_BASER_set_InnerCache(&baser, 7U);
	// Indirect is writable only for the device table
	GITS_BASER_set_Indirect(&baser, false);

	return baser;
}

static GITS_CBASER_t
vgic_its_cbaser_default(void)
{
	GITS_CBASER_t cbaser = GITS_CBASER_default();

	// The following fields of GITS_CBASER are treated as RO. The others,
	// including the Valid bit, are initialised to 0 and must be set by
	// the VM.
	// Shareability == 1: Inner shareable
	GITS_CBASER_set_Shareability(&cbaser, 1U);
	// OuterCache == 0: Inner and outer attributes are the same
	GITS_CBASER_set_OuterCache(&cbaser, 0U);
	// InnerCache == 7: Inner write back, read + write alloc
	GITS_CBASER_set_InnerCache(&cbaser, 7U);

	return cbaser;
}

static error_t
vgic_its_attach_vic(vgic_its_t *vgic_its, vic_t *vic,
		    platform_msi_controller_id_t index)
{
	assert(vgic_its != NULL);
	assert(vic != NULL);
	assert(index < util_array_size(vic->vgic_its_ptrs));

	error_t err = OK;

	spinlock_acquire(&vgic_its->lock);

	// Check that we don't already have an owner
	vic_t *prev_owner = atomic_load_acquire(&vgic_its->owner);
	if (prev_owner != NULL) {
		err = ERROR_BUSY;
		goto fail_locked;
	}

	// Check that we already have an address space attachment; this must
	// be done first because the ITS might be enabled as soon as it has a
	// VIC attachment.
	if (vgic_its->addrspace == NULL) {
		err = ERROR_ADDR_INVALID;
		goto fail_locked;
	}

	// A detached ITS should always be disabled and quiescent.
	assert(!GITS_CTLR_get_Enabled(&vgic_its->ctlr) &&
	       GITS_CTLR_get_Quiescent(&vgic_its->ctlr));

	// Clear the rest of the ITS register state
	vgic_its->device_table_base = vgic_its_baser_default(
		GITS_BASER_TYPE_DEVICES, sizeof(vgic_its_dte_t));
	vgic_its->collection_table_base = vgic_its_baser_default(
		GITS_BASER_TYPE_COLLECTIONS, sizeof(vgic_its_cte_t));
	vgic_its->command_queue_base  = vgic_its_cbaser_default();
	vgic_its->command_queue_read  = GITS_CREADR_default();
	vgic_its->command_queue_write = GITS_CWRITER_default();

	errno_t err_mem = memset_s(vgic_its->collection_table,
				   sizeof(vgic_its->collection_table), 0,
				   sizeof(vgic_its->collection_table));
	if (err_mem != 0) {
		panic("Error in memset_s operation!");
	}

	atomic_store_release(&vgic_its->owner, vic);

	// Attempt to change the VIC's ITS pointer from NULL. If it is not
	// already NULL, fail with a busy error. Once this is set, a virtual
	// device access in the ITS range might be routed to this vITS.
	//
	// This pointer is reference-counted, so we take an extra reference.
	vgic_its_t *prev_vgic_its = NULL;
	(void)object_get_vgic_its_additional(vgic_its);
	if (!atomic_compare_exchange_strong_explicit(
		    &vic->vgic_its_ptrs[index], &prev_vgic_its, vgic_its,
		    memory_order_release, memory_order_relaxed)) {
		object_put_vgic_its(vgic_its);

		// Undo the ownership change.
		atomic_store_release(&vgic_its->owner, NULL);
		err = ERROR_BUSY;

		// Drop the lock and wait for an RCU grace period, in case
		// some other thread has seen the owner we temporarily set.
		spinlock_release(&vgic_its->lock);
		rcu_sync();
		goto fail_unlocked;
	}

fail_locked:
	spinlock_release(&vgic_its->lock);
fail_unlocked:
	return err;
}

error_t
vgic_its_base_handle_vic_bind_msi_source(vic_t *vic, cap_id_t msi_source_cap,
					 vic_msi_source_config_t source_config)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();
	index_t	  index	 = vic_msi_source_config_get_index(&source_config);
	uint64_t  res0	 = vic_msi_source_config_get_res0(&source_config);

	if (res0 != 0U) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	vgic_its_ptr_result_t vgic_its_r = cspace_lookup_vgic_its(
		cspace, msi_source_cap, CAP_RIGHTS_VGIC_ITS_BIND_VIC);
	if (compiler_unexpected(vgic_its_r.e != OK)) {
		err = vgic_its_r.e;
		goto out;
	}

	err = vgic_its_attach_vic(vgic_its_r.r, vic, index);

	object_put_vgic_its(vgic_its_r.r);

out:
	return err;
}

static error_t
vgic_its_base_attach_addrspace(vgic_its_t *vgic_its, addrspace_t *addrspace)
{
	assert(vgic_its != NULL);
	assert(addrspace != NULL);

	error_t err = OK;

	spinlock_acquire(&vgic_its->lock);

	// Check that we don't already have an address space
	if (vgic_its->addrspace != NULL) {
		err = ERROR_BUSY;
		goto fail_locked;
	}

	vgic_its->addrspace = object_get_addrspace_additional(addrspace);

fail_locked:
	spinlock_release(&vgic_its->lock);

	return err;
}

error_t
vgic_its_base_handle_addrspace_attach_vdma(addrspace_t *addrspace,
					   cap_id_t	vdma_device_cap,
					   index_t	index)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	vgic_its_ptr_result_t vgic_its_r = cspace_lookup_vgic_its(
		cspace, vdma_device_cap, CAP_RIGHTS_VGIC_ITS_ATTACH_ADDRSPACE);
	if (compiler_unexpected(vgic_its_r.e != OK)) {
		err = vgic_its_r.e;
		goto out;
	}

	if (index != 0U) {
		err = ERROR_ARGUMENT_INVALID;
		goto out_ref;
	}

	err = vgic_its_base_attach_addrspace(vgic_its_r.r, addrspace);

out_ref:
	object_put_vgic_its(vgic_its_r.r);
out:
	return err;
}

error_t
vgic_its_base_handle_addrspace_attach_vdevice(
	addrspace_t *addrspace, cap_id_t vdevice_cap, index_t index,
	vmaddr_t vbase, size_t size, addrspace_attach_vdevice_flags_t flags)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	vgic_its_ptr_result_t vgic_its_r = cspace_lookup_vgic_its(
		cspace, vdevice_cap, CAP_RIGHTS_VGIC_ITS_ATTACH_ADDRSPACE);
	if (compiler_unexpected(vgic_its_r.e != OK)) {
		err = vgic_its_r.e;
		goto out;
	}

	if (flags.raw != 0U) {
		err = ERROR_ARGUMENT_INVALID;
		goto out_ref;
	}

	if (index != 0U) {
		err = ERROR_ARGUMENT_INVALID;
		goto out_ref;
	}

	spinlock_acquire(&vgic_its_r.r->lock);

	err = vdevice_attach_vmaddr(VDEVICE_TYPE_VGIC_ITS,
				    &vgic_its_r.r->gits_device, addrspace,
				    vbase, size);

	spinlock_release(&vgic_its_r.r->lock);
out_ref:
	object_put_vgic_its(vgic_its_r.r);
out:
	return err;
}

void
vgic_its_base_handle_object_deactivate_vgic_its(vgic_its_t *vgic_its)
{
	// Delete all of the ranges. This will implicitly release the
	// corresponding physical devices.
	range_tree_lock(&vgic_its->device_tree);
	range_tree_destroy(&vgic_its->device_tree,
			   RANGE_TREE_NODE_TYPE_VGIC_ITS_DEVICE_RANGE);
	range_tree_unlock(&vgic_its->device_tree);

	// Detach from the address space.
	if (vgic_its->gits_device.type != VDEVICE_TYPE_NONE) {
		vdevice_detach_vmaddr(&vgic_its->gits_device);
	}
}

void
vgic_its_base_handle_object_cleanup_vgic_its(vgic_its_t *vgic_its)
{
	if (vgic_its->addrspace != NULL) {
		object_put_addrspace(vgic_its->addrspace);
	}
}

void
vgic_its_base_handle_object_deactivate_vic(vic_t *vic)
{
	for (index_t i = 0U; i < util_array_size(vic->vgic_its_ptrs); i++) {
		vgic_its_t *vgic_its =
			atomic_load_acquire(&vic->vgic_its_ptrs[i]);
		if (vgic_its == NULL) {
			continue;
		}
		assert(atomic_load_relaxed(&vgic_its->owner) == vic);

		// Discard all of the virtual LPI mappings.
		vgic_its_set_enabled(vgic_its, false);
		assert(!GITS_CTLR_get_Enabled(&vgic_its->ctlr) &&
		       GITS_CTLR_get_Quiescent(&vgic_its->ctlr));

		atomic_store_release(&vgic_its->owner, NULL);
	}
}

void
vgic_its_base_handle_object_cleanup_vic(vic_t *vic)
{
	for (index_t i = 0U; i < util_array_size(vic->vgic_its_ptrs); i++) {
		vgic_its_t *vgic_its =
			atomic_load_relaxed(&vic->vgic_its_ptrs[i]);
		if (vgic_its != NULL) {
			object_put_vgic_its(vgic_its);
		}
	}
}

void
vgic_its_base_handle_rootvm_init(partition_t *root_partition,
				 cspace_t *root_cspace, hyp_env_data_t *hyp_env,
				 qcbor_enc_ctxt_t *qcbor_enc_ctxt)
{
	(void)root_partition;
	(void)root_cspace;

	hyp_env->gits_base   = PLATFORM_GITS_BASE;
	hyp_env->gits_stride = (size_t)util_bit(GITS_STRIDE_SHIFT);
	assert(qcbor_enc_ctxt != NULL);

	QCBOREncode_AddUInt64ToMap(qcbor_enc_ctxt, "gits_stride",
				   hyp_env->gits_stride);
	// Array of tuples of base address and number of ITSs for each
	// contiguous ITS range. Currently only one range is supported.
	QCBOREncode_OpenArrayInMap(qcbor_enc_ctxt, "gits_ranges");
	QCBOREncode_OpenArray(qcbor_enc_ctxt);
	QCBOREncode_AddUInt64(qcbor_enc_ctxt, PLATFORM_GITS_BASE);
	QCBOREncode_AddUInt64(qcbor_enc_ctxt, PLATFORM_GITS_COUNT);
	QCBOREncode_CloseArray(qcbor_enc_ctxt);
	QCBOREncode_CloseArray(qcbor_enc_ctxt);
}

static error_t
vgic_its_process_id_mapd_command(vgic_its_t *vgic_its, gic_its_cmd_t cmd)
	REQUIRE_SPINLOCK(vgic_its -> lock)
{
	error_t err;
	if (gic_its_cmd_mapd_get_valid(&cmd.mapd)) {
		err = vgic_its_cmd_mapd_valid(
			vgic_its, gic_its_cmd_mapd_get_device_id(&cmd.mapd),
			gic_its_cmd_mapd_get_itt_addr(&cmd.mapd),
			gic_its_cmd_mapd_get_size(&cmd.mapd) + 1U);
	} else {
		err = vgic_its_cmd_mapd_invalid(
			vgic_its, gic_its_cmd_mapd_get_device_id(&cmd.mapd));
	}
	return err;
}

static error_t
vgic_its_process_id_mapc_command(vgic_its_t *vgic_its, gic_its_cmd_t cmd)
	REQUIRE_SPINLOCK(vgic_its -> lock)
{
	error_t err;
	if (gic_its_cmd_mapc_get_valid(&cmd.mapc)) {
		err = vgic_its_cmd_mapc_valid(
			vgic_its, gic_its_cmd_mapc_get_icid(&cmd.mapc),
			(vgic_its_rdbase_t)gic_its_cmd_mapc_get_rdbase(
				&cmd.mapc));
	} else {
		err = vgic_its_cmd_mapc_invalid(
			vgic_its, gic_its_cmd_mapc_get_icid(&cmd.mapc));
	}

	return err;
}

static error_t
vgic_its_process_one_command(vgic_its_t *vgic_its, gic_its_cmd_t cmd)
	REQUIRE_SPINLOCK(vgic_its -> lock)
{
	error_t err;

	TRACE(VGIC, INFO, "VITS {:#x}: cmd {:#x} {:#x} {:#x} {:#x}",
	      (uintptr_t)vgic_its, cmd.base.bf[0], cmd.base.bf[1],
	      cmd.base.bf[2], cmd.base.bf[3]);

	switch (gic_its_cmd_base_get_cmd(&cmd.base)) {
	case GIC_ITS_CMD_ID_CLEAR:
		err = vgic_its_cmd_clear(
			vgic_its, gic_its_cmd_clear_get_device_id(&cmd.clear),
			gic_its_cmd_clear_get_event_id(&cmd.clear));
		break;
	case GIC_ITS_CMD_ID_DISCARD:
		err = vgic_its_cmd_discard(
			vgic_its,
			gic_its_cmd_discard_get_device_id(&cmd.discard),
			gic_its_cmd_discard_get_event_id(&cmd.discard));
		break;
	case GIC_ITS_CMD_ID_INT:
		err = vgic_its_cmd_int(vgic_its,
				       gic_its_cmd_int_get_device_id(&cmd.int_),
				       gic_its_cmd_int_get_event_id(&cmd.int_));
		break;
	case GIC_ITS_CMD_ID_INV:
		err = vgic_its_cmd_inv(vgic_its,
				       gic_its_cmd_inv_get_device_id(&cmd.inv),
				       gic_its_cmd_inv_get_event_id(&cmd.inv));
		break;
	case GIC_ITS_CMD_ID_INVALL:
		err = vgic_its_cmd_invall(
			vgic_its, gic_its_cmd_invall_get_icid(&cmd.invall));
		break;
	case GIC_ITS_CMD_ID_MAPC:
		err = vgic_its_process_id_mapc_command(vgic_its, cmd);
		break;
	case GIC_ITS_CMD_ID_MAPD:
		err = vgic_its_process_id_mapd_command(vgic_its, cmd);
		break;
	case GIC_ITS_CMD_ID_MAPI:
		err = vgic_its_cmd_mapti(
			vgic_its, gic_its_cmd_mapi_get_device_id(&cmd.mapi),
			gic_its_cmd_mapi_get_event_id(&cmd.mapi),
			gic_its_cmd_mapi_get_icid(&cmd.mapi),
			gic_its_cmd_mapi_get_event_id(&cmd.mapi));
		break;
	case GIC_ITS_CMD_ID_MAPTI:
		err = vgic_its_cmd_mapti(
			vgic_its, gic_its_cmd_mapti_get_device_id(&cmd.mapti),
			gic_its_cmd_mapti_get_event_id(&cmd.mapti),
			gic_its_cmd_mapti_get_icid(&cmd.mapti),
			gic_its_cmd_mapti_get_lpi(&cmd.mapti));
		break;
	case GIC_ITS_CMD_ID_MOVALL:
		err = vgic_its_cmd_movall(
			vgic_its,
			(vgic_its_rdbase_t)gic_its_cmd_movall_get_rdbase1(
				&cmd.movall),
			(vgic_its_rdbase_t)gic_its_cmd_movall_get_rdbase2(
				&cmd.movall));
		break;
	case GIC_ITS_CMD_ID_MOVI:
		err = vgic_its_cmd_movi(
			vgic_its, gic_its_cmd_movi_get_device_id(&cmd.movi),
			gic_its_cmd_movi_get_event_id(&cmd.movi),
			gic_its_cmd_movi_get_icid(&cmd.movi));
		break;
	case GIC_ITS_CMD_ID_SYNC:
		err = vgic_its_cmd_sync(
			vgic_its,
			(vgic_its_rdbase_t)gic_its_cmd_sync_get_rdbase(
				&cmd.sync));
		break;
#if GICV3_HAS_VLPI
#if GICV3_HAS_VLPI_V4_1
	case GIC_ITS_CMD_ID_INVDB:
#endif // GICV3_HAS_VLPI_V4_1
	case GIC_ITS_CMD_ID_VINVALL:
	case GIC_ITS_CMD_ID_VMAPI:
	case GIC_ITS_CMD_ID_VMAPP:
	case GIC_ITS_CMD_ID_VMAPTI:
	case GIC_ITS_CMD_ID_VMOVI:
	case GIC_ITS_CMD_ID_VMOVP:
#if GICV3_HAS_VLPI_V4_1
	case GIC_ITS_CMD_ID_VSGI:
#endif // GICV3_HAS_VLPI_V4_1
#endif // GICV3_HAS_VLPI
	default:
		// GICv4-only or invalid command ID
		TRACE(VGIC, WARN,
		      "VGIC ITS: bad cmd id {:#x} ({:#x} {:#x} {:#x} {:#x})",
		      gic_its_cmd_base_get_cmd(&cmd.base), cmd.base.bf[0],
		      cmd.base.bf[1], cmd.base.bf[2], cmd.base.bf[3]);
		err = ERROR_ARGUMENT_INVALID;
		break;
	}

	if (err != OK) {
		TRACE(VGIC, WARN, "VITS {:#x}: cmd failed: {:d}",
		      (uintptr_t)vgic_its, (register_t)err);
	}
	return err;
}

static void
vgic_its_process_commands(vgic_its_t *vgic_its)
{
	enum { disabled, invalid, empty, stalled, head_range } exit_reason;

	count_t cmds_done = 0U;

	TRACE(VGIC, INFO, "VITS {:#x}: starting command processing",
	      (uintptr_t)vgic_its);

	while (1) {
		spinlock_acquire(&vgic_its->lock);

		if (!GITS_CTLR_get_Enabled(&vgic_its->ctlr) ||
		    GITS_CTLR_get_Quiescent(&vgic_its->ctlr)) {
			// The ITS is disabled; we can't process commands.
			spinlock_release(&vgic_its->lock);
			exit_reason = disabled;
			break;
		}

		if (!GITS_CBASER_get_Valid(&vgic_its->command_queue_base)) {
			// The queue has not been configured by the VM yet
			spinlock_release(&vgic_its->lock);
			exit_reason = invalid;
			break;
		}

		index_t read_index =
			GITS_CREADR_get_Index(&vgic_its->command_queue_read);
		if (GITS_CWRITER_get_Index(&vgic_its->command_queue_write) ==
		    read_index) {
			// No commands waiting in the queue
			spinlock_release(&vgic_its->lock);
			exit_reason = empty;
			break;
		}

		if (GITS_CREADR_get_Stalled(&vgic_its->command_queue_read)) {
			// Stalled after a command failure, until CWRITER is
			// written with the Retry bit set
			spinlock_release(&vgic_its->lock);
			exit_reason = stalled;
			break;
		}

		size_t	queue_size = (((size_t)GITS_CBASER_get_Size(
					       &vgic_its->command_queue_base) +
				       1U) *
				      4096U);
		count_t queue_entries =
			(count_t)(queue_size / sizeof(gic_its_cmd_t));

		if (GITS_CWRITER_get_Index(&vgic_its->command_queue_write) >=
		    queue_entries) {
			// Write pointer out of range; treat queue as invalid
			spinlock_release(&vgic_its->lock);
			exit_reason = head_range;
			break;
		}

		// Read the next command out of the queue
		gic_its_cmd_t cmd;
		paddr_t	      queue_base = GITS_CBASER_get_Physical_Address(
			&vgic_its->command_queue_base);
		size_t cmd_offset = read_index * sizeof(cmd);

		assert(cmd_offset < queue_size);

		error_t err = useraccess_copy_from_guest_ipa(
				      vgic_its->addrspace, &cmd, sizeof(cmd),
				      queue_base + cmd_offset, sizeof(cmd),
				      false, false)
				      .e;
		if (err != OK) {
			TRACE(VGIC, WARN,
			      "VITS {:#x}: cmd read @ {:#x} failed: {:d}",
			      (uintptr_t)vgic_its,
			      (uintptr_t)(queue_base + cmd_offset),
			      (register_t)err);
		}

		// If we read the command successfully, try to execute it
		if (err == OK) {
			err = vgic_its_process_one_command(vgic_its, cmd);
			cmds_done++;
		}

		if (err == OK) {
			// Success; advance the read pointer
			GITS_CREADR_set_Index(&vgic_its->command_queue_read,
					      (read_index + 1U) %
						      queue_entries);
		} else {
			// Failure; stall the queue
			// FIXME: QC Gunyah issue #143
			GITS_CREADR_set_Stalled(&vgic_its->command_queue_read,
						true);
		}

		spinlock_release(&vgic_its->lock);
	}

	TRACE(VGIC, INFO,
	      "VITS {:#x}: processed {:d} commands (exit reason {:d})",
	      (uintptr_t)vgic_its, cmds_done, (register_t)exit_reason);
}

static error_t
vgic_its_range_map_event(vgic_its_t *vgic_its, vgic_its_dte_t dte,
			 platform_msi_device_t	*device,
			 platform_msi_event_id_t event)
	REQUIRE_SPINLOCK(vgic_its -> lock);

static void
vgic_its_enable(vgic_its_t *vgic_its)
{
	// This is a potentially very slow operation, since it must issue many
	// physical VMAPTI commands if the virtual ITS was previously disabled
	// with mapped LPIs. Therefore we frequently drop and re-acquire the
	// lock, and then re-check the enabled bit to ensure that we haven't
	// been overtaken by a vgic_its_disable() on a different CPU.
	spinlock_acquire(&vgic_its->lock);
	if (!GITS_CTLR_get_Enabled(&vgic_its->ctlr)) {
		spinlock_release(&vgic_its->lock);
		goto out;
	}

	vgic_its_copy_in_ctes(vgic_its);

	// Go through all the attached real devices and check their DTEs,
	// in case we need to restore a physical mapping.
	rcu_read_start();
	vgic_its_device_range_t *range;
	range_tree_foreach_container (range, &vgic_its->device_tree,
				      vgic_its_device_range, its_node) {
		platform_msi_controller_id_t phys_index =
			atomic_load_consume(&vgic_its->phys_index);
		const platform_msi_controller_t *pits =
			platform_irq_msi_devices(phys_index);

		count_t device_count	  = (count_t)range->its_node.size;
		index_t device_index_base = range->device_index;

		// We have finished with the range at this point
		rcu_read_finish();

		for (index_t i = 0; i < device_count; i++) {
			index_t device_index = device_index_base + i;
			assert((NULL != pits) &&
			       (device_index < pits->num_devices));

			platform_msi_device_t *device =
				&pits->devices[device_index];
			assert(NULL != device);

			vgic_its_dte_result_t dte_r = vgic_its_copy_in_dte(
				vgic_its, device->device_id);

			if ((dte_r.e != OK) ||
			    !vgic_its_dte_get_valid(&dte_r.r)) {
				// Device is not mapped; skip it
				continue;
			}

			// DTE is valid; iterate through the ITT
			count_t itt_entries = (count_t)util_bit(
				vgic_its_dte_get_itt_size(&dte_r.r) + 1U);
			for (index_t event = 0U; event < itt_entries; event++) {
				error_t err = vgic_its_range_map_event(
					vgic_its, dte_r.r, device,
					(platform_msi_event_id_t)event);

				if (err != OK) {
					// Log error and continue with the
					// remaining events.
					TRACE(VGIC, WARN,
					      "VITS {:#x}: Failed to map {:d}/{:d}: {:d}",
					      (register_t)vgic_its,
					      device->device_id, event,
					      (register_t)err);
				}

				spinlock_release(&vgic_its->lock);
				asm_yield();
				spinlock_acquire(&vgic_its->lock);
				if (!GITS_CTLR_get_Enabled(&vgic_its->ctlr)) {
					goto out_locked;
				}
			}
		}
		rcu_read_start();
	}
	rcu_read_finish();

	// We've successfully finished enabling; clear the Quiescent bit.
	GITS_CTLR_set_Quiescent(&vgic_its->ctlr, false);

out_locked:
	spinlock_release(&vgic_its->lock);

out:
	// If the command queue was initialised to not be empty, we might now
	// need to start processing commands.
	vgic_its_process_commands(vgic_its);
}

static void
vgic_its_disable(vgic_its_t *vgic_its)
{
	// This is a potentially very slow operation, since it must issue many
	// physical DISCARD commands if any LPIs were left mapped, and we must
	// block until they are complete. Therefore we frequently drop and
	// re-acquire the lock to permit preemption to occur.
	//
	// We don't need to re-check the enabled bit after acquiring the lock
	// because that can't be set while the Quiescent bit is clear, and after
	// activation the Quiescent bit is only set at the end of this function.
	// Concurrent calls to this function also should not be possible because
	// it is only called after changing the enable bit to false.
	spinlock_acquire(&vgic_its->lock);
	assert(!GITS_CTLR_get_Quiescent(&vgic_its->ctlr) &&
	       !GITS_CTLR_get_Enabled(&vgic_its->ctlr));

	// Copy the cached collections out to VM memory.
	vgic_its_copy_out_ctes(vgic_its);

	// Go through all the attached real devices and issue DISCARD commands
	// for any LPIs mapped in the physical ITS.
	rcu_read_start();
	vgic_its_device_range_t *range;
	range_tree_foreach_container (range, &vgic_its->device_tree,
				      vgic_its_device_range, its_node) {
		platform_msi_controller_id_t phys_index =
			atomic_load_consume(&vgic_its->phys_index);
		const platform_msi_controller_t *pits =
			platform_irq_msi_devices(phys_index);

		count_t device_count	  = (count_t)range->its_node.size;
		index_t device_index_base = range->device_index;

		// We have finished with the range at this point
		rcu_read_finish();

		for (index_t i = 0; i < device_count; i++) {
			index_t device_index = device_index_base + i;
			assert((NULL != pits) &&
			       (device_index < pits->num_devices));

			platform_msi_device_t *device =
				&pits->devices[device_index];
			assert(NULL != device);

			vgic_its_dte_result_t dte_r = vgic_its_copy_in_dte(
				vgic_its, device->device_id);

			if ((dte_r.e != OK) ||
			    !vgic_its_dte_get_valid(&dte_r.r)) {
				// Device is not mapped; skip it
				continue;
			}

			error_t err = vgic_its_unmap_events(vgic_its, device);
			assert(OK == err);

			spinlock_release(&vgic_its->lock);
			asm_yield();
			spinlock_acquire(&vgic_its->lock);
		}
		rcu_read_start();
	}
	rcu_read_finish();

	spinlock_release(&vgic_its->lock);

	vgic_its_sync_all(vgic_its);

	// Disable is complete; set the Quiescent bit.
	spinlock_acquire(&vgic_its->lock);
	GITS_CTLR_set_Quiescent(&vgic_its->ctlr, true);
	spinlock_release(&vgic_its->lock);
}

void
vgic_its_set_enabled(vgic_its_t *vgic_its, bool enabled)
{
	spinlock_acquire(&vgic_its->lock);

	TRACE(VGIC, INFO, "VITS {:#x}: ctrl {:#x} -> enable {:d}",
	      (uintptr_t)vgic_its, GITS_CTLR_raw(vgic_its->ctlr),
	      (register_t)enabled);

	if (enabled && GITS_CTLR_get_Quiescent(&vgic_its->ctlr) &&
	    !GITS_CTLR_get_Enabled(&vgic_its->ctlr)) {
		GITS_CTLR_set_Enabled(&vgic_its->ctlr, true);
		spinlock_release(&vgic_its->lock);
		vgic_its_enable(vgic_its);
	} else if (!enabled && GITS_CTLR_get_Enabled(&vgic_its->ctlr)) {
		// We might be interrupting an enable in progress on another
		// CPU. In that case, the Quiescent bit will currently be true;
		// set it to false to prevent any enable starting until this
		// disable has completed.
		GITS_CTLR_set_Quiescent(&vgic_its->ctlr, false);
		GITS_CTLR_set_Enabled(&vgic_its->ctlr, false);
		spinlock_release(&vgic_its->lock);
		vgic_its_disable(vgic_its);
	} else {
		// Either the Enabled bit has not been changed, or an attempt
		// was made to set Enabled while Quiescent was clear (i.e. a
		// disable was in progress on another CPU). Ignore the write.
		spinlock_release(&vgic_its->lock);
	}
}

void
vgic_its_set_collection_table_base(vgic_its_t  *vgic_its,
				   GITS_BASER_t collection_table_base)
{
	spinlock_acquire(&vgic_its->lock);

	// Copy only the writable fields
	GITS_BASER_copy_Valid(&vgic_its->collection_table_base,
			      &collection_table_base);
	GITS_BASER_copy_Physical_Address(&vgic_its->collection_table_base,
					 &collection_table_base);
	GITS_BASER_copy_Size(&vgic_its->collection_table_base,
			     &collection_table_base);

	// No need to flush or update anything; this register is only used
	// while enabling and disabling the ITS, and the effect of changing it
	// while the ITS is enabled or not quiescent is defined to be
	// UNPREDICTABLE.

	spinlock_release(&vgic_its->lock);
}

void
vgic_its_set_device_table_base(vgic_its_t  *vgic_its,
			       GITS_BASER_t device_table_base)
{
	spinlock_acquire(&vgic_its->lock);

	// Copy only the writable fields
	GITS_BASER_copy_Valid(&vgic_its->device_table_base, &device_table_base);
	GITS_BASER_copy_Physical_Address(&vgic_its->device_table_base,
					 &device_table_base);
	GITS_BASER_copy_Size(&vgic_its->device_table_base, &device_table_base);
	GITS_BASER_copy_Indirect(&vgic_its->device_table_base,
				 &device_table_base);

	// No need to flush or update anything; the effect of changing this
	// register while the ITS is enabled or not quiescent is defined to be
	// UNPREDICTABLE.

	spinlock_release(&vgic_its->lock);
}

void
vgic_its_set_command_queue_base(vgic_its_t   *vgic_its,
				GITS_CBASER_t command_queue_base)
{
	spinlock_acquire(&vgic_its->lock);

	// Copy only the writable fields
	GITS_CBASER_copy_Valid(&vgic_its->command_queue_base,
			       &command_queue_base);
	GITS_CBASER_copy_Physical_Address(&vgic_its->command_queue_base,
					  &command_queue_base);
	GITS_CBASER_copy_Size(&vgic_its->command_queue_base,
			      &command_queue_base);

	// GITS_CREADR is zeroed on writes to this register
	vgic_its->command_queue_read = GITS_CREADR_default();

	spinlock_release(&vgic_its->lock);
}

void
vgic_its_set_command_queue_write(vgic_its_t    *vgic_its,
				 GITS_CWRITER_t command_queue_write)
{
	spinlock_acquire(&vgic_its->lock);

	if (GITS_CWRITER_get_Retry(&command_queue_write)) {
		GITS_CWRITER_set_Retry(&command_queue_write, false);
		GITS_CREADR_set_Stalled(&vgic_its->command_queue_read, false);
	}

	vgic_its->command_queue_write = command_queue_write;

	spinlock_release(&vgic_its->lock);

	vgic_its_process_commands(vgic_its);
}

void
vgic_its_translate(vgic_its_t *vgic_its, platform_msi_event_id_t event)
{
	// The device ID used when the CPU writes to GITS_TRANSLATER is
	// strictly platform-specific, but is typically 0.
	//
	// Note that GITS_TRANSLATER is in a page of its own, and can safely
	// be mapped directly to a VM to allow it to raise device 0 events.
	spinlock_acquire(&vgic_its->lock);
	(void)vgic_its_cmd_int(vgic_its, (platform_msi_device_id_t)0U, event);
	spinlock_release(&vgic_its->lock);
}

static error_t
vgic_its_range_map_event(vgic_its_t *vgic_its, vgic_its_dte_t dte,
			 platform_msi_device_t	*device,
			 platform_msi_event_id_t event)
{
	error_t		       ret;
	vgic_its_itte_result_t itte_r =
		vgic_its_copy_in_itte(vgic_its, dte, event);

	if ((itte_r.e != OK) || !vgic_its_itte_get_valid(&itte_r.r)) {
		// Event is not mapped; skip it
		ret = OK;
		goto out;
	}

	vgic_its_cte_t *cte_ptr =
		&vgic_its->collection_table[vgic_its_itte_get_icid(&itte_r.r)];
	vgic_its_cte_set_known_empty(cte_ptr, false);

	if (!vgic_its_cte_get_valid(cte_ptr)) {
		// The collection is not mapped, nothing to do.
		ret = OK;
		goto out;
	}

	vgic_its_rdbase_t rd_index = vgic_its_cte_get_rd_index(cte_ptr);

	rcu_read_start();
	vic_t *vic = atomic_load_consume(&vgic_its->owner);
	if (rd_index < vic->gicr_count) {
		virq_t	  lpi = vgic_its_itte_get_lpi(&itte_r.r);
		thread_t *vcpu =
			atomic_load_consume(&vic->gicr_vcpus[rd_index]);

		if (vcpu == NULL) {
			// The vCPU has been removed, nothing to do.
			TRACE(VGIC, WARN,
			      "VITS {:#x}: range_map_event vCPU is NULL for {:d}",
			      (register_t)vgic_its, rd_index);
			ret = OK;
			goto out_rcu;
		}

		ret = vgic_its_map(vgic_its, device, event, vcpu, lpi, false);
		if (ret != OK) {
			goto out_rcu;
		}
	}
	ret = OK;
out_rcu:
	rcu_read_finish();
out:

	return ret;
}

static error_t
vgic_its_bind_devices(vgic_its_t		      *vgic_its,
		      const platform_msi_controller_t *pits,
		      vgic_device_id_t device_id_start, count_t device_count)
{
	error_t err;

	void_ptr_result_t alloc_r = partition_alloc(
		vgic_its->header.partition, sizeof(vgic_its_device_range_t),
		alignof(vgic_its_device_range_t));
	if (alloc_r.e != OK) {
		err = alloc_r.e;
		goto fail_alloc_range;
	}
	vgic_its_device_range_t *range = (vgic_its_device_range_t *)alloc_r.r;

	*range = (vgic_its_device_range_t){
		.vgic_its = vgic_its,
	};

	count_t			 devices_found = 0;
	platform_msi_device_id_t last_id       = 0;
	for (index_t i = 0U; i < pits->num_devices; i++) {
		platform_msi_device_t	*device	   = &pits->devices[i];
		platform_msi_device_id_t device_id = device->device_id;

		if ((devices_found == 0U) && (device_id_start != device_id)) {
			continue;
		}

		// Using a single vgic_its_device_range relies on the assumption
		// that the devices in each MSI controller are stored
		// consecutively and ordered by device ID, ascending. Do a
		// sanity check to verify this assumption.
		if ((devices_found > 0U) && (device_id != (last_id + 1U))) {
			break;
		}

		// Swap device owner
		vgic_its_t *prev_owner =
			atomic_load_acquire(&device->vgic_its_owner);
		if ((prev_owner != NULL) ||
		    !atomic_compare_exchange_strong_explicit(
			    &device->vgic_its_owner, &prev_owner, vgic_its,
			    memory_order_acquire, memory_order_acquire)) {
			err = ERROR_BUSY;
			goto fail_claim_device;
		}

		// If this is the first device, insert a new range_map entry.
		if (devices_found == 0U) {
			range->device_index = i;
		}

		devices_found++;
		last_id = device_id;

		if (devices_found >= device_count) {
			break;
		}
	}

	if (device_count != devices_found) {
		err = ERROR_ARGUMENT_INVALID;
		goto fail_claim_device;
	}

	range_tree_lock(&vgic_its->device_tree);
	err = range_tree_insert(&vgic_its->device_tree, &range->its_node,
				device_id_start, device_count);
	range_tree_unlock(&vgic_its->device_tree);

fail_claim_device:
	if (err != OK) {
		// Reset owners to NULL.
		for (index_t i = 0U; i < devices_found; i++) {
			platform_msi_device_t *device =
				&pits->devices[range->device_index + i];

			vgic_its_t *prev_owner =
				atomic_load_relaxed(&device->vgic_its_owner);
			assert(prev_owner == vgic_its);

			atomic_store_relaxed(&device->vgic_its_owner, NULL);
		}

		partition_free(vgic_its->header.partition, range,
			       sizeof(*range));
	}
fail_alloc_range:
	return err;
}

error_t
hypercall_vgic_its_bind_devices(cap_id_t vgic_its_cap, cap_id_t its_cap,
				vgic_device_id_t device_id_start,
				count_t		 device_count)
{
	error_t err;

	cspace_t		    *cspace = cspace_get_self();
	platform_msi_controller_id_t phys_index;
	platform_msi_controller_id_t prev_index;

	vgic_its_ptr_result_t vgic_its_r = cspace_lookup_vgic_its(
		cspace, vgic_its_cap, CAP_RIGHTS_VGIC_ITS_BIND_DEVICES);

	if (compiler_unexpected(vgic_its_r.e != OK)) {
		err = vgic_its_r.e;
		goto fail_get_vits;
	}

	vgic_its_t *vgic_its = vgic_its_r.r;

	gicv3_its_ptr_result_t gicv3_its_r = cspace_lookup_gicv3_its(
		cspace, its_cap, CAP_RIGHTS_GICV3_ITS_BIND_DEVICE);

	if (compiler_unexpected(gicv3_its_r.e != OK)) {
		err = gicv3_its_r.e;
		goto fail_get_pits;
	}

	gicv3_its_t *gicv3_its = gicv3_its_r.r;
	phys_index	       = gicv3_its->phys_index;
	assert(platform_irq_msi_devices(phys_index) != NULL);

	const platform_msi_controller_t *pits =
		platform_irq_msi_devices(phys_index);

	if (NULL == pits) {
		err = ERROR_ARGUMENT_INVALID;
		goto fail_lookup_pits;
	}

	prev_index = atomic_load_relaxed(&vgic_its->phys_index);
	// Check that the vITS either is already assigned to the ITs passed as
	// an argument or that it has not been assigned to an ITS yet. For the
	// time being, we allow a vITS to be bound to a single ITS only during
	// its lifetime. The ITS is selected on the first time a device is
	// bound to the vITS based on the ITS passed as an argument. In the
	// future, it may become possible to bind devices from different ITSes
	// to a single vITS. This will require additional considerations when
	// processing vITS commands not targeted at a specific device where it
	// is not trivial to determine the affected ITSes, e.g. SYNC.
	if ((prev_index != phys_index) && (prev_index != VGIC_ITS_MAX_NUM)) {
		err = ERROR_BUSY;
		goto fail_lookup_pits;
	}

	if (prev_index == VGIC_ITS_MAX_NUM) {
		if (!atomic_compare_exchange_strong_explicit(
			    &vgic_its->phys_index, &prev_index, phys_index,
			    memory_order_release, memory_order_relaxed)) {
			err = ERROR_BUSY;
			goto fail_claim_pits;
		}

		err = vgic_its_reserve_unused_device(vgic_its, phys_index);
		if (err != OK) {
			// Attempt to role back the phys_index change. We don't
			// care if this fails because in this case, a different
			// index was already assigned in concurrently.
			(void)atomic_compare_exchange_strong_explicit(
				&vgic_its->phys_index, &phys_index, prev_index,
				memory_order_release, memory_order_relaxed);
			goto fail_claim_pits;
		}
	}

	err = vgic_its_bind_devices(vgic_its, pits, device_id_start,
				    device_count);

	// We don't try to roll back the physical ITS claim if the device claim
	// failed. The virtual ITS should be deleted and recreated if the wrong
	// physical ITS was specified. This is temporary until we allow
	// per-range physical ITS bindings.
fail_claim_pits:
fail_lookup_pits:
	object_put_gicv3_its(gicv3_its);
fail_get_pits:
	object_put_vgic_its(vgic_its);
fail_get_vits:
	return err;
}

hypercall_vgic_its_unbind_devices_result_t
hypercall_vgic_its_unbind_devices(cap_id_t	   vgic_its_cap,
				  vgic_device_id_t device_id_start,
				  count_t	   device_count)
{
	cspace_t				  *cspace = cspace_get_self();
	hypercall_vgic_its_unbind_devices_result_t ret	  = { 0 };

	vgic_its_ptr_result_t vgic_its_r = cspace_lookup_vgic_its(
		cspace, vgic_its_cap, CAP_RIGHTS_VGIC_ITS_UNBIND_DEVICES);

	if (compiler_unexpected(vgic_its_r.e != OK)) {
		ret.error = vgic_its_r.e;
		ret.count = 0;
		goto out;
	}

	vgic_its_t *vgic_its = vgic_its_r.r;

	count_result_t count_r = vgic_its_unbind_devices(
		vgic_its, device_id_start, device_count);

	// Devices may have been unmapped even if an error was encountered
	// subsequently, so wait for the DISCARD commands to finish.
	if (count_r.r != 0U) {
		vgic_its_sync_all(vgic_its);
	}

	ret.error = count_r.e;
	ret.count = count_r.r;

	object_put_vgic_its(vgic_its);
out:

	return ret;
}

platform_msi_device_t *
vgic_its_lookup_phys_device(vgic_its_t		    *vgic_its,
			    platform_msi_device_id_t device_id, bool discard)
{
	platform_msi_device_t	    *device;
	platform_msi_controller_id_t phys_index =
		atomic_load_consume(&vgic_its->phys_index);

	range_tree_lookup_result_t lookup =
		range_tree_lookup(&vgic_its->device_tree, device_id, 1U);

	if (lookup.node == NULL) {
		device = NULL;
		goto out;
	}
	vgic_its_device_range_t *range =
		vgic_its_device_range_container_of_its_node(lookup.node);

	if (!discard && range->unbound) {
		// We're in the process of discarding all of the range's
		// physical mappings, so we can't allow lookups to use the
		// physical mapping except for discard commands. It is safe
		// to allow discard commands here because they will test and
		// clear the event's bit in vgic_its_mapped_events, so the
		// unbind won't need to issue a discard.
		device = NULL;
		goto out;
	}

	assert(lookup.node->base <= (size_t)device_id);
	index_t device_index =
		range->device_index +
		(device_id - (platform_msi_device_id_t)lookup.node->base);

	const platform_msi_controller_t *pits =
		platform_irq_msi_devices(phys_index);

	if ((NULL == pits) || (device_index >= pits->num_devices)) {
		device = NULL;
		goto out;
	}

	device = &pits->devices[device_index];
	if (device_id != device->device_id) {
		device = NULL;
		goto out;
	}

	if (atomic_load_consume(&device->vgic_its_owner) != vgic_its) {
		device = NULL;
		goto out;
	}

out:
	return device;
}

thread_ptr_result_t
vgic_its_lookup_vcpu(vic_t *vic, vgic_its_rdbase_t rdbase)
{
	thread_ptr_result_t ret;
	if (vic == NULL) {
		// VIC has been destroyed
		ret = thread_ptr_result_error(ERROR_OBJECT_STATE);
		goto out;
	}

	if (rdbase >= vic->gicr_count) {
		// RDbase is out of range
		ret = thread_ptr_result_error(ERROR_ARGUMENT_INVALID);
		goto out;
	}

	thread_t *vcpu = atomic_load_consume(&vic->gicr_vcpus[rdbase]);
	if (vcpu == NULL) {
		// VCPU does not exist or has been destroyed
		ret = thread_ptr_result_error(ERROR_OBJECT_STATE);
		goto out;
	}

	ret = thread_ptr_result_ok(vcpu);
out:
	return ret;
}

static void
vgic_its_device_range_free(vgic_its_device_range_t *range)
{
	assert(range != NULL);

	vgic_its_t *vgic_its = range->vgic_its;
	assert(vgic_its != NULL);

	platform_msi_controller_id_t phys_index =
		atomic_load_consume(&vgic_its->phys_index);
	const platform_msi_controller_t *pits =
		platform_irq_msi_devices(phys_index);
	assert(pits != NULL);

	// Clear device owners. A device can only be assigned to one vgic_its at
	// a time and cannot be re-assigned until its range is freed, so this
	// should never fail.
	for (index_t i = 0; i < range->its_node.size; i++) {
		platform_msi_device_t *device =
			&pits->devices[range->device_index + i];
		vgic_its_t *prev_owner = vgic_its;

		if (!atomic_compare_exchange_strong_explicit(
			    &device->vgic_its_owner, &prev_owner, NULL,
			    memory_order_release, memory_order_relaxed)) {
			panic("ITS device ownership mismatch");
		}
	}

	(void)partition_free(vgic_its->header.partition, range, sizeof(*range));
}

bool
vgic_its_base_handle_range_tree_release_node(range_tree_node_t *node)
{
	vgic_its_device_range_t *range =
		vgic_its_device_range_container_of_its_node(node);
	vgic_its_device_range_free(range);
	return true;
}

rcu_update_status_t
vgic_its_base_handle_rcu_update(rcu_entry_t *entry)
{
	vgic_its_device_range_t *range =
		vgic_its_device_range_container_of_rcu(entry);
	vgic_its_device_range_free(range);
	return rcu_update_status_default();
}
