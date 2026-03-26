// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <list.h>
#include <platform_irq.h>
#include <range_map.h>
#include <rcu.h>
#include <trace.h>
#include <util.h>

#include "gicv3_its.h"
#include "vgic_gicr.h"
#include "vgic_its_base.h"

// Given a VLPI and the virtual RDbase it is targeted to, map a dummy physical
// ITS event to the VLPI, and return that event.
//
// The returned event ID only remains valid while the virtual ITS lock is held;
// after that, it might be remapped to some other LPI.
static platform_msi_id_result_t
vgic_its_map_dummy_event(vgic_its_t *vgic_its, vgic_its_rdbase_t rdbase,
			 virq_t vlpi)
{
	assert(vgic_its != NULL);

	platform_msi_id_result_t ret =
		platform_msi_id_result_ok(vgic_its->dummy_event);

	if (vgic_its->dummy_event_mapped_lpi == vlpi) {
		// Already mapped. Nothing to do.
		goto out;
	}

	if (vgic_its->dummy_event_mapped_lpi != 0U) {
		// Mapped to the wrong LPI. Discard the mapping.
		ret.e = gicv3_its_discard(vgic_its->dummy_event);
		if (ret.e != OK) {
			goto out;
		}
	}

	rcu_read_start();

	vic_t		   *vic	   = atomic_load_consume(&vgic_its->owner);
	thread_ptr_result_t vcpu_r = vgic_its_lookup_vcpu(vic, rdbase);
	if (vcpu_r.e != OK) {
		ret = platform_msi_id_result_error(vcpu_r.e);
		goto out_rcu;
	}

	// Map to the dummy event.
	ret.e = gicv3_its_vmap(vgic_its->dummy_event, vcpu_r.r, vlpi);
	if (ret.e == OK) {
		vgic_its->dummy_event_mapped_lpi = vlpi;
	}

out_rcu:
	rcu_read_finish();

out:
	return ret;
}

// If a dummy physical event is currently mapped to a specified VLPI, discard
// the mapping.
static void
vgic_its_unmap_dummy_event(vgic_its_t *vgic_its, virq_t vlpi)
{
	assert(vgic_its != NULL);

	if (vgic_its->dummy_event_mapped_lpi == vlpi) {
		error_t err = gicv3_its_discard(vgic_its->dummy_event);
		// Currently we don't handle errors here correctly: the discard
		// has already been completed in the virtual tables and we don't
		// have the code to restore it.
		assert(err == OK);
		vgic_its->dummy_event_mapped_lpi = 0U;
	}
}

// Given a virtual ITS device and event, read the virtual ITS tables to
// translate it into a physical ITS device and event. This is used by the CLEAR,
// DISCARD, INT and INV commands. The DISCARD and INV commands need slightly
// different behaviour, which is triggered by the respective boolean arguments.
//
// If the specified device / event pair is not correctly mapped to a valid
// collection and LPI number, an error other than ERROR_IDLE will be returned.
// If the mapping is valid, then this function will return either a physical
// event ID to use in a physical ITS command implementing the operation, or
// ERROR_IDLE if no physical command is needed.
//
// If the operation is a DISCARD, then the ITTE will be zeroed once the mapping
// has been validated.
//
// If the operation is an INV and the hardware supports GICv4.1, the operation
// can be performed by writing the LPI number to GICR_INVLPIR rather than
// with a physical ITS command. In that case, this function performs the
// invalidate and returns ERROR_IDLE.
//
// If the lookup is for a software-implemented device and is inside the mapped
// event range, then we must temporarily map the virtual ITS's dummy physical
// event to the returned LPI in the physical ITS (if it isn't already mapped to
// that LPI). If the operation is a DISCARD, then the dummy mapping is then
// immediately removed again and ERROR_IDLE is returned. Otherwise, the dummy
// event is returned and the caller can enqueue a command for it.
//
// Otherwise, the specified physical event ID is returned unchanged.
static platform_msi_id_result_t
vgic_its_lookup_event(vgic_its_t *vgic_its, platform_msi_device_id_t device,
		      platform_msi_event_id_t event, bool inv, bool discard)
	REQUIRE_SPINLOCK(vgic_its -> lock)
{
	platform_msi_id_result_t ret;

	assert(vgic_its != NULL);

	vgic_its_dte_result_t dte_r = vgic_its_copy_in_dte(vgic_its, device);
	if (dte_r.e != OK) {
		// Device is out of range or table access faulted
		TRACE(VGIC, WARN,
		      "VITS {:#x}: DTE access fault for device {:d}",
		      (uintptr_t)vgic_its, device);
		ret = platform_msi_id_result_error(dte_r.e);
		goto out;
	}
	if (!vgic_its_dte_get_valid(&dte_r.r)) {
		// Device is not mapped
		TRACE(VGIC, WARN,
		      "VITS {:#x}: DTE invalid for device {:d} ({:#x})",
		      (uintptr_t)vgic_its, device, dte_r.r.bf[0]);
		ret = platform_msi_id_result_error(ERROR_ARGUMENT_INVALID);
		goto out;
	}

	vgic_its_itte_result_t itte_r =
		vgic_its_copy_in_itte(vgic_its, dte_r.r, event);
	if (itte_r.e != OK) {
		// Event is out of range or table access faulted
		TRACE(VGIC, WARN,
		      "VITS {:#x}: ITTE access fault for event {:d}/{:d}",
		      (uintptr_t)vgic_its, device, event);
		ret = platform_msi_id_result_error(itte_r.e);
		goto out;
	}
	if (!vgic_its_itte_get_valid(&itte_r.r)) {
		// Event is not mapped
		TRACE(VGIC, WARN,
		      "VITS {:#x}: ITTE invalid for event {:d}/{:d} ({:#x})",
		      (uintptr_t)vgic_its, device, event, itte_r.r.bf[0]);
		ret = platform_msi_id_result_error(ERROR_ARGUMENT_INVALID);
		goto out;
	}
	assert(event < util_bit(VGIC_ITS_EVENT_BITS));

	vgic_its_cte_t cte =
		vgic_its->collection_table[vgic_its_itte_get_icid(&itte_r.r)];
	if (!vgic_its_cte_get_valid(&cte)) {
		// Collection is not mapped
		TRACE(VGIC, WARN,
		      "VITS {:#x}: IC {:d} invalid for event {:d}/{:d} ({:#x})",
		      (uintptr_t)vgic_its, vgic_its_itte_get_icid(&itte_r.r),
		      device, event, cte.bf[0]);
		ret = platform_msi_id_result_error(ERROR_ARGUMENT_INVALID);
		goto out;
	}

	if (inv) {
		// Invalidate the cached LPI configuration, if it is currently
		// mapped to a valid GICR. If not, there is nothing to do here.
		rcu_read_start();
		vic_t *vic = atomic_load_consume(&vgic_its->owner);

		thread_ptr_result_t vcpu_r = vgic_its_lookup_vcpu(
			vic, vgic_its_cte_get_rd_index(&cte));
		if (vcpu_r.e == OK) {
#if GICV3_HAS_VLPI_V4_1
			vgic_gicr_rd_invlpi(vic, vcpu_r.r,
					    vgic_its_itte_get_lpi(&itte_r.r));
			rcu_read_finish();

			// There is no need to issue an ITS command.
			ret = platform_msi_id_result_error(ERROR_IDLE);
			goto out;
#else
			vgic_gicr_copy_propbase_one(vic, vcpu_r.r, vlpi);
#endif
		}
		rcu_read_finish();
	} else if (discard) {
		error_t err = vgic_its_copy_out_itte(vgic_its, dte_r.r, event,
						     vgic_its_itte_default());
		if (err != OK) {
			// Generally shouldn't fail, but it's possible if the
			// VM tries to place the ITT in read-only memory.
			ret = platform_msi_id_result_error(err);
			goto out;
		}
	} else {
		// CLEAR or INT command; nothing special to do.
	}

	rcu_read_start();
	platform_msi_device_t *phys_device =
		vgic_its_lookup_phys_device(vgic_its, device, discard);
	if (phys_device == NULL) {
		ret = vgic_its_map_dummy_event(
			vgic_its, vgic_its_cte_get_rd_index(&cte),
			vgic_its_itte_get_lpi(&itte_r.r));
		if (discard && (ret.e == OK)) {
			vgic_its_unmap_dummy_event(
				vgic_its, vgic_its_itte_get_lpi(&itte_r.r));
			ret = platform_msi_id_result_error(ERROR_IDLE);
		}
	} else if (!bitmap_isset(phys_device->vgic_its_mapped_events, event)) {
		// The physical event is unmapped; the VM has been messing with
		// the translation tables. There is no need to issue an ITS
		// command.
		TRACE(VGIC, WARN,
		      "VITS {:#x}: physical event unmapped {:d}/{:d}",
		      (uintptr_t)vgic_its, device, event);
		ret = platform_msi_id_result_error(ERROR_IDLE);
	} else {
		platform_msi_controller_id_t phys_index =
			atomic_load_consume(&vgic_its->phys_index);
		ret.r = platform_msi_id_default();
		platform_msi_id_set_its_index(&ret.r, phys_index);
		platform_msi_id_set_device_id(&ret.r, device);
		platform_msi_id_set_event_id(&ret.r, event);
		ret.e = OK;

		if (discard) {
			bitmap_clear(phys_device->vgic_its_mapped_events,
				     event);
		}
	}
	rcu_read_finish();

out:
	return ret;
}

error_t
vgic_its_cmd_clear(vgic_its_t *vgic_its, platform_msi_device_id_t device,
		   platform_msi_event_id_t event)
{
	assert(vgic_its != NULL);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "CLEAR {:#x}/{:#x}", device, event);

	error_t			 err;
	platform_msi_id_result_t msi_r =
		vgic_its_lookup_event(vgic_its, device, event, false, false);

	if (msi_r.e == OK) {
		err = gicv3_its_clear(msi_r.r);
	} else {
		err = msi_r.e;
	}

	return err;
}

error_t
vgic_its_cmd_discard(vgic_its_t *vgic_its, platform_msi_device_id_t device,
		     platform_msi_event_id_t event)
{
	assert(vgic_its != NULL);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "DISCARD {:#x}/{:#x}", device, event);

	error_t			 err;
	platform_msi_id_result_t msi_r =
		vgic_its_lookup_event(vgic_its, device, event, false, true);

	if (msi_r.e == OK) {
		err = gicv3_its_discard(msi_r.r);
		// Currently we don't handle errors here correctly: the discard
		// has already been completed in the virtual tables and we don't
		// have the code to restore it.
		assert(err == OK);
	} else if (msi_r.e == ERROR_IDLE) {
		// The discard was already performed on the dummy event; we
		// don't need to queue an ITS command.
		err = OK;
	} else {
		err = msi_r.e;
	}

	return err;
}

error_t
vgic_its_cmd_int(vgic_its_t *vgic_its, platform_msi_device_id_t device,
		 platform_msi_event_id_t event)
{
	assert(vgic_its != NULL);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "INT {:#x}/{:#x}", device, event);

	error_t			 err;
	platform_msi_id_result_t msi_r =
		vgic_its_lookup_event(vgic_its, device, event, false, false);

	if (msi_r.e == OK) {
		err = gicv3_its_assert(msi_r.r);
	} else {
		err = msi_r.e;
	}

	return err;
}

error_t
vgic_its_cmd_inv(vgic_its_t *vgic_its, platform_msi_device_id_t device,
		 platform_msi_event_id_t event)
{
	assert(vgic_its != NULL);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "INV {:#x}/{:#x}", device, event);

	error_t			 err;
	platform_msi_id_result_t msi_r =
		vgic_its_lookup_event(vgic_its, device, event, true, false);

	if (msi_r.e == OK) {
		err = gicv3_its_invalidate(msi_r.r);
	} else if (msi_r.e == ERROR_IDLE) {
		// The invalidate was issued via the physical GICR; we don't
		// need to queue an ITS command.
		err = OK;
	} else {
		err = msi_r.e;
	}

	return err;
}

error_t
vgic_its_cmd_invall(vgic_its_t *vgic_its, vgic_its_ic_id_t icid)
{
	assert(vgic_its != NULL);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "INVALL {:#x}", icid);

	error_t err;

	if (icid >= util_array_size(vgic_its->collection_table)) {
		// Collection is out of range
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	vgic_its_cte_t cte = vgic_its->collection_table[icid];
	if (!vgic_its_cte_get_valid(&cte)) {
		// Collection is not mapped
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	vgic_its_rdbase_t rdbase = vgic_its_cte_get_rd_index(&cte);
	rcu_read_start();
	vic_t		   *vic	   = atomic_load_consume(&vgic_its->owner);
	thread_ptr_result_t vcpu_r = vgic_its_lookup_vcpu(vic, rdbase);
	if (vcpu_r.e != OK) {
		err = vcpu_r.e;
		goto out_rcu;
	}

	vgic_gicr_rd_invall(vic, vcpu_r.r);
	err = OK;

out_rcu:
	rcu_read_finish();
out:
	return err;
}

error_t
vgic_its_cmd_mapc_valid(vgic_its_t *vgic_its, vgic_its_ic_id_t icid,
			vgic_its_rdbase_t rdbase)
{
	assert(vgic_its != NULL);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "MAPC V=1 {:#x} -> {:d}", icid,
		       rdbase);

	error_t err;

	if (icid >= util_array_size(vgic_its->collection_table)) {
		// Collection is out of range
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	vgic_its_cte_t *cte_ptr = &vgic_its->collection_table[icid];
	if (vgic_its_cte_get_valid(cte_ptr) &&
	    (vgic_its_cte_get_rd_index(cte_ptr) == rdbase)) {
		// Already mapped to the requested RD. Nothing to do.
		err = OK;
		goto out;
	}

	if (!vgic_its_cte_get_known_empty(cte_ptr)) {
		// Collection may not be empty. We need to search for any valid
		// ITTEs and issue physical VMAPTIs or VMOVIs for them. This is
		// unimplemented, because Linux never does it; it only issues
		// MAPC once at CPU boot time, before any MAP[T]I targeting the
		// collection.
		// FIXME: QC Gunyah issue #144
		err = ERROR_UNIMPLEMENTED;
		goto out;
	}

	// Collection is known to be empty; we don't have to issue any physical
	// commands. Just update the CTE.
	vgic_its_cte_set_valid(cte_ptr, true);
	vgic_its_cte_set_rd_index(cte_ptr, rdbase);
	err = OK;

out:
	return err;
}

error_t
vgic_its_cmd_mapc_invalid(vgic_its_t *vgic_its, vgic_its_ic_id_t icid)
{
	assert(vgic_its != NULL);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "MAPC V=0 {:#x}", icid);

	error_t err;

	if (icid >= util_array_size(vgic_its->collection_table)) {
		// Collection is out of range
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	vgic_its_cte_t *cte_ptr = &vgic_its->collection_table[icid];
	if (!vgic_its_cte_get_valid(cte_ptr)) {
		// Collection is already invalid. Nothing to do.
		err = OK;
		goto out;
	}

	if (!vgic_its_cte_get_known_empty(cte_ptr)) {
		// Collection may not be empty. We need to search for any valid
		// ITTEs and issue physical DISCARDs for them. This is
		// unimplemented, because Linux never does it; it only issues
		// MAPC once at CPU boot time, before any MAP[T]I targeting the
		// collection.
		// FIXME: QC Gunyah issue #144
		err = ERROR_UNIMPLEMENTED;
		goto out;
	}

	// Collection is known to be empty; we don't have to issue any physical
	// commands. Just update the CTE.
	vgic_its_cte_set_valid(cte_ptr, false);
	err = OK;

out:
	return err;
}

error_t
vgic_its_cmd_mapd_valid(vgic_its_t *vgic_its, platform_msi_device_id_t device,
			vmaddr_t itt_base, count_t event_bits)
{
	assert(vgic_its != NULL);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "MAPD V=1 {:#x} {:#x}/{:d}", device,
		       itt_base, event_bits);

	error_t err;

	if ((event_bits > VGIC_ITS_EVENT_BITS) || (event_bits < 1U)) {
		// ITT size is out of range
		err = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	// We're allowed to assume that the new ITTE is empty and that the old
	// ITTE, if any, is also empty, so there are no physical commands to
	// submit. Just write out the new DTE.
	vgic_its_dte_t dte = vgic_its_dte_default();
	vgic_its_dte_set_valid(&dte, true);
	vgic_its_dte_set_itt_addr(&dte, itt_base);
	vgic_its_dte_set_itt_size(&dte, event_bits - 1U);
	err = vgic_its_copy_out_dte(vgic_its, device, dte);

out:
	return err;
}

error_t
vgic_its_cmd_mapd_invalid(vgic_its_t *vgic_its, platform_msi_device_id_t device)
{
	assert(vgic_its != NULL);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "MAPD V=0 {:#x}", device);

	rcu_read_start();

	error_t err;

	// If we own the physical device, we must ensure that all events are
	// unmapped from it.
	platform_msi_device_t *phys_device =
		vgic_its_lookup_phys_device(vgic_its, device, true);
	if (phys_device != NULL) {
		platform_msi_controller_id_t phys_index =
			atomic_load_consume(&vgic_its->phys_index);

		platform_msi_id_t msi_id = platform_msi_id_default();
		platform_msi_id_set_its_index(&msi_id, phys_index);
		platform_msi_id_set_device_id(&msi_id, device);

		// Loop through the bits in the mapped events bitmap. We don't
		// trust the device table itself, since the VM might have
		// changed it. Also, all we need here are the mapped event
		// numbers so reading the bitmap is faster anyway.
		BITMAP_FOREACH_SET_BEGIN(event,
					 phys_device->vgic_its_mapped_events,
					 (index_t)util_bit(VGIC_ITS_EVENT_BITS))
			platform_msi_id_set_event_id(&msi_id, event);
			err = gicv3_its_discard(msi_id);
			if (err != OK) {
				goto out;
			}
			bitmap_clear(phys_device->vgic_its_mapped_events,
				     event);
		BITMAP_FOREACH_SET_END
	} else {
		// We need to clear all the software-asserted VLPIs, which means
		// iterating through all the DTEs. Not yet implemented.
		// FIXME: QC Gunyah issue #145
		err = ERROR_UNIMPLEMENTED;
		goto out;
	}

	// Write out the zeroed DTE.
	vgic_its_dte_t dte = vgic_its_dte_default();
	err		   = vgic_its_copy_out_dte(vgic_its, device, dte);

out:
	rcu_read_finish();
	return err;
}

// Also used for mapi, which is an alias of mapti when event==vlpi
error_t
vgic_its_cmd_mapti(vgic_its_t *vgic_its, platform_msi_device_id_t device,
		   platform_msi_event_id_t event, vgic_its_ic_id_t icid,
		   virq_t vlpi)
{
	assert(vgic_its != NULL);

	platform_msi_controller_id_t phys_index =
		atomic_load_consume(&vgic_its->phys_index);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "MAPI/MAPTI: {#:x} {:#x} -> {:d}",
		       ((uint64_t)device << 32U) | event, icid, vlpi);

	error_t err;

	if (icid >= util_array_size(vgic_its->collection_table)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (vlpi >= util_bit(VGIC_LPI_BITS)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	vgic_its_dte_result_t dte_r = vgic_its_copy_in_dte(vgic_its, device);
	if (dte_r.e != OK) {
		// Device is out of range or table access faulted
		err = dte_r.e;
		goto out;
	}
	if (!vgic_its_dte_get_valid(&dte_r.r)) {
		// Device is not mapped
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	vgic_its_itte_t itte = vgic_its_itte_default();
	vgic_its_itte_set_valid(&itte, true);
	vgic_its_itte_set_lpi(&itte, vlpi);
	vgic_its_itte_set_icid(&itte, icid);
	err = vgic_its_copy_out_itte(vgic_its, dte_r.r, event, itte);
	if (err != OK) {
		goto out;
	}

	// Collection is no longer empty; MAPC can't skip ITT iteration
	vgic_its_cte_t *cte = &vgic_its->collection_table[icid];
	vgic_its_cte_set_known_empty(cte, false);

	// If there is a physical device and the collection is mapped to a valid
	// virtual GICR, we must submit a VMAPTI to map the physical event to
	// the corresponding vPE.
	if (!vgic_its_cte_get_valid(cte)) {
		// The collection is not mapped; there's nothing to do.
		err = OK;
		goto out;
	}

	// Make sure we haven't left the specified LPI mapped to a dummy event.
	vgic_its_unmap_dummy_event(vgic_its, vlpi);

	rcu_read_start();

	vic_t		   *vic = atomic_load_consume(&vgic_its->owner);
	thread_ptr_result_t vcpu_r =
		vgic_its_lookup_vcpu(vic, vgic_its_cte_get_rd_index(cte));
	if (vcpu_r.e != OK) {
		// The mapped GICR is not valid; there's nothing to do.
		err = OK;
		goto out_rcu;
	}

	// Ensure that the cached LPI configuration is up to date.
	(void)vgic_gicr_copy_propbase_one(vic, vcpu_r.r, vlpi);

	// Find the physical device, if any.
	platform_msi_device_t *phys_device =
		vgic_its_lookup_phys_device(vgic_its, device, false);
	if (phys_device == NULL) {
		// No physical device; there's nothing to do.
		err = OK;
		goto out_rcu;
	}

	platform_msi_id_t msi_id = platform_msi_id_default();
	platform_msi_id_set_its_index(&msi_id, phys_index);
	platform_msi_id_set_device_id(&msi_id, device);
	platform_msi_id_set_event_id(&msi_id, event);
	err = gicv3_its_vmap(msi_id, vcpu_r.r, vlpi);

	if (err == OK) {
		bitmap_set(phys_device->vgic_its_mapped_events, event);
	}

out_rcu:
	rcu_read_finish();

out:
	return err;
}

error_t
vgic_its_cmd_movall(vgic_its_t *vgic_its, vgic_its_rdbase_t rdbase_src,
		    vgic_its_rdbase_t rdbase_dst)
{
	assert(vgic_its != NULL);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "MOVALL {:d} -> {:d}", rdbase_src,
		       rdbase_dst);

	// Virtual MOVALL is a no-op. In cases where it would need to do
	// something on a physical ITS, the virtual MAPC command that must
	// precede it will (once it is implemented) issue VMOVI for every mapped
	// event in the collection, which moves the pending states between the
	// vPEs as MOVALL normally would, so there is nothing left to do.
	//
	// We don't even need to range-check the rdbase arguments, because
	// invalid values have UNPREDICTABLE behaviour (not a defined error).
	(void)vgic_its;
	(void)rdbase_src;
	(void)rdbase_dst;
	return OK;
}

error_t
vgic_its_cmd_movi(vgic_its_t *vgic_its, platform_msi_device_id_t device,
		  platform_msi_event_id_t event, vgic_its_ic_id_t icid)
{
	assert(vgic_its != NULL);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "MOVI {:#x}/{:#x} -> {:#x}", device,
		       event, icid);

	error_t err;

	vgic_its_dte_result_t dte_r = vgic_its_copy_in_dte(vgic_its, device);
	if (dte_r.e != OK) {
		// Device is out of range or table access faulted
		err = dte_r.e;
		goto out;
	}
	if (!vgic_its_dte_get_valid(&dte_r.r)) {
		// Device is not mapped
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	vgic_its_itte_result_t itte_r =
		vgic_its_copy_in_itte(vgic_its, dte_r.r, event);
	if (itte_r.e != OK) {
		// Event is out of range or table access faulted
		err = itte_r.e;
		goto out;
	}
	if (!vgic_its_itte_get_valid(&itte_r.r)) {
		// Event is not mapped
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (vgic_its_itte_get_icid(&itte_r.r) == icid) {
		// No change in ICID; nothing to do
		err = OK;
		goto out;
	}

	vgic_its_cte_t cte1 =
		vgic_its->collection_table[vgic_its_itte_get_icid(&itte_r.r)];
	if (!vgic_its_cte_get_valid(&cte1)) {
		// Collection is not mapped
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (icid >= util_array_size(vgic_its->collection_table)) {
		// Collection is out of range
		err = ERROR_ARGUMENT_SIZE;
		goto out;
	}
	vgic_its_cte_t cte2 = vgic_its->collection_table[icid];
	if (!vgic_its_cte_get_valid(&cte2)) {
		// Collection is not mapped
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (vgic_its_cte_get_rd_index(&cte1) ==
	    vgic_its_cte_get_rd_index(&cte2)) {
		// No change in RD; nothing to do
		err = OK;
		goto out;
	}

	rcu_read_start();
	vic_t		   *vic = atomic_load_consume(&vgic_its->owner);
	thread_ptr_result_t vcpu_r =
		vgic_its_lookup_vcpu(vic, vgic_its_cte_get_rd_index(&cte2));
	if (vcpu_r.e != OK) {
		err = vcpu_r.e;
		goto out_rcu;
	}

	platform_msi_device_t *phys_device =
		vgic_its_lookup_phys_device(vgic_its, device, false);
	if (phys_device == NULL) {
		platform_msi_id_result_t msi_id_r = vgic_its_map_dummy_event(
			vgic_its, vgic_its_cte_get_rd_index(&cte1),
			vgic_its_itte_get_lpi(&itte_r.r));
		if (msi_id_r.e == OK) {
			err = gicv3_its_vmove(msi_id_r.r, vcpu_r.r);
		} else {
			err = msi_id_r.e;
		}
	} else if (!bitmap_isset(phys_device->vgic_its_mapped_events, event)) {
		// The physical event is unmapped; the VM has been messing with
		// the translation tables.
		err = ERROR_IDLE;
	} else {
		platform_msi_controller_id_t phys_index =
			atomic_load_consume(&vgic_its->phys_index);

		platform_msi_id_t msi_id = platform_msi_id_default();
		platform_msi_id_set_its_index(&msi_id, phys_index);
		platform_msi_id_set_device_id(&msi_id, device);
		platform_msi_id_set_event_id(&msi_id, event);
		err = gicv3_its_vmove(msi_id, vcpu_r.r);
	}

out_rcu:
	rcu_read_finish();

	if (err == OK) {
		vgic_its_itte_set_icid(&itte_r.r, icid);
		err = vgic_its_copy_out_itte(vgic_its, dte_r.r, event,
					     itte_r.r);
	}

out:
	return err;
}

error_t
vgic_its_cmd_sync(vgic_its_t *vgic_its, vgic_its_rdbase_t rdbase)
{
	assert(vgic_its != NULL);

	platform_msi_controller_id_t phys_index =
		atomic_load_consume(&vgic_its->phys_index);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "SYNC {:d}", rdbase);

	error_t err;

	rcu_read_start();

	vic_t		   *vic	   = atomic_load_consume(&vgic_its->owner);
	thread_ptr_result_t vcpu_r = vgic_its_lookup_vcpu(vic, rdbase);
	if (vcpu_r.e != OK) {
		err = vcpu_r.e;
		goto out_rcu;
	}

	while (vgic_gicr_get_inv_pending(vic, vcpu_r.r)) {
		// Wait for completion of any direct LPI invalidates previously
		// issued by INV or INVALL commands.
	}

	count_result_t count_r = gicv3_its_vsync(phys_index, vcpu_r.r);
	if (count_r.e != OK) {
		err = count_r.e;
		goto out_rcu;
	}

	// TODO: Don't wait for the physical ITS while holding the virtual ITS
	// lock; instead do it asynchronously by registering a private LPI and
	// submitting an INT to raise it after the VSYNC completes. The virtual
	// command queue must be blocked until that happens.
	// FIXME: QC Gunyah issue #146
	err = gicv3_its_wait(phys_index, count_r.r);

out_rcu:
	rcu_read_finish();
	return err;
}
