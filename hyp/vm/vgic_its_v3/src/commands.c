// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <platform_irq.h>
#include <rcu.h>
#include <trace.h>
#include <util.h>
#include <vgic.h>
#include <vgic_its.h>

#include "gicv3_its.h"
#include "vgic_gicr.h"
#include "vgic_its_base.h"

// Given a virtual ITS device and event, read the virtual ITS tables to
// translate it into a physical ITS device and event. This is used by the CLEAR,
// DISCARD, INT and INV commands.
//
// If the specified device / event pair is not correctly mapped to a valid
// collection and LPI number, an error other than ERROR_IDLE will be returned.
// If the mapping is valid, then this function will return either a physical
// event ID to use in a physical ITS command implementing the operation, or
// ERROR_IDLE if no physical command is needed.
//
// Otherwise, the specified physical event ID is returned unchanged.
static platform_msi_id_result_t
vgic_its_lookup_event(vgic_its_t *vgic_its, platform_msi_device_id_t device,
		      platform_msi_event_id_t event, bool discard)
	REQUIRE_RCU_READ REQUIRE_SPINLOCK(vgic_its -> lock)
{
	platform_msi_id_result_t ret;

	assert(vgic_its != NULL);

	assert(event < util_bit(VGIC_ITS_EVENT_BITS));

	platform_msi_device_t *phys_device =
		vgic_its_lookup_phys_device(vgic_its, device, discard);
	if (phys_device == NULL) {
		// There is no corresponding physical device, which is the case
		// for mappings created by the guest for non-physical devices
		// that can be used to trigger LPIs and manage their state via
		// LPI commands (e.g. INT). Return ERROR_IDLE to avoid any
		// commands being sent to the physical ITS.
		ret = platform_msi_id_result_error(ERROR_IDLE);
	} else if (!bitmap_isset(phys_device->vgic_its_mapped_events, event)) {
		// The physical event is unmapped; either the VM has been
		// messing with the translation tables, or the physical device
		// has been concurrently unbound. There is no need to issue an
		// ITS command.
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

	return ret;
}

// Based on the supplied arguments, looks up the vLPI mapped to the supplied
// device event and invalidates its configuration. If there is a physical
// device for the event, updates the configuration of the corresponding pLPI
// and forwards the INV command to the physical ITS.
static error_t
vgic_its_inv(vgic_its_t *vgic_its, platform_msi_device_id_t device,
	     platform_msi_event_id_t event, vgic_its_itte_t itte)
	REQUIRE_SPINLOCK(vgic_its -> lock)
{
	error_t			 ret;
	vic_t			*vic;
	thread_ptr_result_t	 vcpu_r;
	vgic_its_ic_id_t	 icid = vgic_its_itte_get_icid(&itte);
	vgic_its_cte_t		 cte;
	platform_msi_id_result_t msi_r;
	irq_t			 vlpi;
	bool_result_t		 sync_needed_r;

	if (icid >= util_array_size(vgic_its->collection_table)) {
		// Collection is out of range
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	cte = vgic_its->collection_table[icid];
	if (!vgic_its_cte_get_valid(&cte)) {
		// Collection is not mapped
		TRACE(VGIC, WARN,
		      "VITS {:#x}: IC {:d} invalid for event {:d}/{:d} ({:#x})",
		      (uintptr_t)vgic_its, icid, device, event, cte.bf[0]);
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	// Invalidate the cached LPI configuration, if it is currently
	// mapped to a valid GICR. If not, there is nothing to do here.
	rcu_read_start();
	vic = atomic_load_consume(&vgic_its->owner);

	vcpu_r = vgic_its_lookup_vcpu(vic, vgic_its_cte_get_rd_index(&cte));
	if (vcpu_r.e != OK) {
		ret = vcpu_r.e;
		goto out_rcu;
	}

	vlpi = vgic_its_itte_get_lpi(&itte);
	if (vlpi >= util_bit(VGIC_LPI_BITS)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out_rcu;
	}

	sync_needed_r = vgic_inv_vlpi(vic, vcpu_r.r, vlpi, NULL);
	if (sync_needed_r.e != OK) {
		ret = sync_needed_r.e;
		goto out_rcu;
	}

	if (sync_needed_r.r) {
		// Update cached redistributor state on next SYNC.
		atomic_store_relaxed(&vgic_its->pending_sync_needed, true);
	}

	msi_r = vgic_its_lookup_event(vgic_its, device, event, false);

	// If there is a physical device forward the invalidate command.
	if (msi_r.e == OK) {
		cpu_index_t affinity =
			atomic_load_relaxed(&vcpu_r.r->vgic_lpi_affinity);

		// If the vCPU has no LPI affinity assigned yet, assign it now.
		if (affinity == PLATFORM_MAX_CORES) {
			affinity = vgic_try_set_vcpu_lpi_affinity(vcpu_r.r);
		}

		ret = gicv3_its_invalidate(msi_r.r);
		assert(ret == OK);
		vgic_its_sync(vgic_its, affinity);
		assert(ret == OK);

		goto out_rcu;
	}

	ret = OK;
out_rcu:
	rcu_read_finish();
out:

	return ret;
}

// Invalidate all vLPIs targeted to the supplied vCPU.
static error_t
vgic_its_invall(vgic_its_t *vgic_its, thread_t *vcpu)
{
	platform_msi_controller_id_t phys_index;
	cpu_index_t		     affinity;
	error_t			     ret = OK;

	assert(vgic_its != NULL);
	assert(vcpu != NULL);

	phys_index = atomic_load_consume(&vgic_its->phys_index);
	affinity   = atomic_load_relaxed(&vcpu->vgic_lpi_affinity);

	// If the vCPU has no LPI affinity assigned yet, assign it now.
	if (affinity == PLATFORM_MAX_CORES) {
		affinity = vgic_try_set_vcpu_lpi_affinity(vcpu);
	}

	ret = gicv3_its_invalidate_all(phys_index, affinity);
	if (ret != OK) {
		goto out;
	}

	vgic_its_sync(vgic_its, affinity);
out:

	return ret;
}

// Remove an existing vLPI mapping. This is used by the MAPTI handler in case a
// MAPTI command targets an event for which a valid ITTE exists.
static error_t
vgic_its_unmap_existing(vgic_its_t *vgic_its, vic_t *vic,
			vgic_its_itte_t itte_old, vgic_its_itte_t itte_new,
			vgic_its_rdbase_t rdbase_new) REQUIRE_RCU_READ
{
	error_t		    err;
	vgic_its_rdbase_t   rdbase_old;
	thread_ptr_result_t vcpu_old_r;
	virq_t		    vlpi_old = vgic_its_itte_get_lpi(&itte_old);
	irq_result_t	    plpi_r;

	if (vlpi_old >= util_bit(VGIC_LPI_BITS)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (vgic_its_itte_get_icid(&itte_old) ==
	    vgic_its_itte_get_icid(&itte_new)) {
		rdbase_old = rdbase_new;
	} else {
		vgic_its_ic_id_t icid_old = vgic_its_itte_get_icid(&itte_old);
		vgic_its_cte_t	*cte_old;

		if (icid_old >= util_array_size(vgic_its->collection_table)) {
			err = ERROR_ARGUMENT_INVALID;
			goto out;
		}

		cte_old = &vgic_its->collection_table[icid_old];

		// FIXME: As long as MAPC does not iterate over all
		// ITTEs and unmaps them, we ignore that the CTE may be
		// invalid and rely on the fact that
		// vgic_its_lookup_vcpu() won't find a valid vCPU if
		// the rdbase is really invalid. Once MAPC is
		// implemented correctly, this should change.
		rdbase_old = vgic_its_cte_get_rd_index(cte_old);
	}

	vcpu_old_r = vgic_its_lookup_vcpu(vic, rdbase_old);
	if (vcpu_old_r.e != OK) {
		err = vcpu_old_r.e;
		goto out;
	}

	plpi_r = vgic_unmap_vlpi(vcpu_old_r.r, vlpi_old);
	if (plpi_r.e == ERROR_LPI_NOT_ASSIGNED) {
		// No pLPI was mapped for the old vLPI, so we don't need to
		// unmap anything.
		err = OK;
	} else {
		err = plpi_r.e;
	}

out:

	return err;
}

error_t
vgic_its_cmd_clear(vgic_its_t *vgic_its, platform_msi_device_id_t device,
		   platform_msi_event_id_t event)
{
	error_t			 err;
	vgic_its_dte_result_t	 dte_r;
	vgic_its_itte_result_t	 itte_r;
	vgic_its_itte_t		 itte;
	virq_t			 vlpi;
	vgic_its_cte_result_t	 cte_r;
	vgic_its_cte_t		 cte;
	vic_t			*vic;
	thread_ptr_result_t	 vcpu_r;
	platform_msi_id_result_t msi_r;

	assert(vgic_its != NULL);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "CLEAR {:#x}/{:#x}", device, event);

	dte_r = vgic_its_lookup_dte(vgic_its, device);
	if (OK != dte_r.e) {
		err = dte_r.e;
		goto out;
	}

	itte_r = vgic_its_lookup_itte(vgic_its, device, dte_r.r, event, true);
	if (OK != itte_r.e) {
		err = itte_r.e;
		goto out;
	}

	vlpi = vgic_its_itte_get_lpi(&itte_r.r);
	if (vlpi >= util_bit(VGIC_LPI_BITS)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	itte = itte_r.r;

	cte_r = vgic_its_lookup_cte(vgic_its, itte, device, event);
	if (cte_r.e != OK) {
		err = cte_r.e;
		goto out;
	}

	cte = cte_r.r;

	rcu_read_start();
	vic    = atomic_load_consume(&vgic_its->owner);
	vcpu_r = vgic_its_lookup_vcpu(vic, vgic_its_cte_get_rd_index(&cte));
	if (vcpu_r.e != OK) {
		err = vcpu_r.e;
		goto out_rcu;
	}

	// Clear the vLPI pending state on the target vCPU. The guest is
	// expected to send a SYNC command following that to update the
	// redistributor cache.
	vgic_clear_vlpi_pending(vcpu_r.r, vlpi);
	vgic_set_vlpi_pending_sync(vcpu_r.r, vlpi);
	atomic_store_relaxed(&vgic_its->pending_sync_needed, true);

	msi_r = vgic_its_lookup_event(vgic_its, device, event, false);

	if (msi_r.e == OK) {
		cpu_index_t affinity =
			atomic_load_relaxed(&vcpu_r.r->vgic_lpi_affinity);

		// If the vCPU has no LPI affinity assigned yet, assign it now.
		if (affinity == PLATFORM_MAX_CORES) {
			affinity = vgic_try_set_vcpu_lpi_affinity(vcpu_r.r);
		}

		err = gicv3_its_clear(msi_r.r);
		if (err != OK) {
			goto out_rcu;
		}

		vgic_its_sync(vgic_its, affinity);
	} else if (ERROR_IDLE == msi_r.e) {
		err = OK;
	} else {
		err = msi_r.e;
	}

out_rcu:
	rcu_read_finish();
out:

	return err;
}

error_t
vgic_its_cmd_discard(vgic_its_t *vgic_its, platform_msi_device_id_t device,
		     platform_msi_event_id_t event)
{
	error_t			 err;
	vgic_its_dte_result_t	 dte_r;
	vgic_its_dte_t		 dte;
	vgic_its_itte_result_t	 itte_r;
	vgic_its_itte_t		 itte;
	vgic_its_cte_result_t	 cte_r;
	vgic_its_cte_t		 cte;
	virq_t			 vlpi;
	irq_result_t		 plpi_r;
	thread_ptr_result_t	 vcpu_r;
	vic_t			*vic;
	platform_msi_id_result_t msi_r;
	platform_msi_device_t	*phys_device;

	assert(vgic_its != NULL);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "DISCARD {:#x}/{:#x}", device, event);

	dte_r = vgic_its_lookup_dte(vgic_its, device);
	if (OK != dte_r.e) {
		err = dte_r.e;
		goto out;
	}

	dte = dte_r.r;

	itte_r = vgic_its_lookup_itte(vgic_its, device, dte_r.r, event, true);
	if (OK != itte_r.e) {
		err = itte_r.e;
		goto out;
	}

	itte = itte_r.r;

	cte_r = vgic_its_lookup_cte(vgic_its, itte, device, event);
	if (cte_r.e != OK) {
		err = cte_r.e;
		goto out;
	}

	cte = cte_r.r;

	vlpi = vgic_its_itte_get_lpi(&itte);
	if (vlpi >= util_bit(VGIC_LPI_BITS)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	err = vgic_its_copy_out_itte(vgic_its, dte, event,
				     vgic_its_itte_default());
	if (err != OK) {
		goto out;
	}

	rcu_read_start();
	vic    = atomic_load_consume(&vgic_its->owner);
	vcpu_r = vgic_its_lookup_vcpu(vic, vgic_its_cte_get_rd_index(&cte));
	if (vcpu_r.e != OK) {
		err = vcpu_r.e;
		goto out_rcu;
	}

	// Clear the vLPI pending state on the target vCPU.
	vgic_clear_vlpi_pending(vcpu_r.r, vlpi);

	phys_device = vgic_its_lookup_phys_device(vgic_its, device, true);
	if (phys_device == NULL) {
		goto out_rcu;
	}

	// Lower the reference count and if this is the last event
	// assigned to this pLPI, mark it as unused.
	plpi_r = vgic_unmap_vlpi(vcpu_r.r, vlpi);
	if (plpi_r.e != OK) {
		err = plpi_r.e;
		TRACE(VGIC, WARN,
		      "VITS {:#x}: Unmap failed for vLPI {:d}: {:d}",
		      (uintptr_t)vgic_its, vlpi, (register_t)err);
		goto out_rcu;
	}

	// Note that ERROR_IDLE might be returned by this lookup if the mapping
	// was already discarded by a concurrent unbind. In that case we still
	// need to sync (because the unbind might not have done so yet) but we
	// can't issue a DISCARD.
	msi_r = vgic_its_lookup_event(vgic_its, device, event, true);
	if ((msi_r.e != OK) && (msi_r.e != ERROR_IDLE)) {
		err = msi_r.e;
		goto out_rcu;
	}

	cpu_index_t affinity =
		atomic_load_relaxed(&vcpu_r.r->vgic_lpi_affinity);

	// If the vCPU has no LPI affinity assigned yet, assign it now.
	if (affinity == PLATFORM_MAX_CORES) {
		affinity = vgic_try_set_vcpu_lpi_affinity(vcpu_r.r);
	}

	if (msi_r.e == OK) {
		err = gicv3_its_discard(msi_r.r);
		if (err != OK) {
			goto out_rcu;
		}
	}

	// If we reach here, a pLPI was assigned and thus the LPI
	// affinity must have been changed from the initial value.
	assert(affinity != PLATFORM_MAX_CORES);
	vgic_its_sync(vgic_its, affinity);

out_rcu:
	// If the vLPI was previously listed as pending, evict it.
	vgic_sync_all(vic, false);

	rcu_read_finish();

out:

	return err;
}

error_t
vgic_its_cmd_int(vgic_its_t *vgic_its, platform_msi_device_id_t device,
		 platform_msi_event_id_t event)
{
	error_t		       err;
	vgic_its_dte_result_t  dte_r;
	vgic_its_itte_result_t itte_r;
	vgic_its_itte_t	       itte;
	vgic_its_cte_result_t  cte_r;
	vgic_its_cte_t	       cte;
	thread_ptr_result_t    vcpu_r;
	vic_t		      *vic;
	virq_t		       vlpi;

	assert(vgic_its != NULL);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "INT {:#x}/{:#x}", device, event);

	dte_r = vgic_its_lookup_dte(vgic_its, device);
	if (dte_r.e != OK) {
		err = dte_r.e;
		goto out;
	}

	itte_r = vgic_its_lookup_itte(vgic_its, device, dte_r.r, event, true);
	if (itte_r.e != OK) {
		err = itte_r.e;
		goto out;
	}

	itte = itte_r.r;
	vlpi = vgic_its_itte_get_lpi(&itte);
	if (vlpi >= util_bit(VGIC_LPI_BITS)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	cte_r = vgic_its_lookup_cte(vgic_its, itte, device, event);
	if (cte_r.e != OK) {
		err = cte_r.e;
		goto out;
	}

	cte = cte_r.r;

	rcu_read_start();
	vic = atomic_load_consume(&vgic_its->owner);

	vcpu_r = vgic_its_lookup_vcpu(vic, vgic_its_cte_get_rd_index(&cte));
	if (vcpu_r.e != OK) {
		err = vcpu_r.e;
		goto out_rcu;
	}

	// Ensure that the cached LPI configuration is up to date. Ignore the
	// return value as we don't need to trigger a SYNC for a physical LPI
	// here.
	(void)vgic_inv_vlpi(vic, vcpu_r.r, vlpi, NULL);

	err = vlpi_assert(vcpu_r.r, vlpi);
out_rcu:
	rcu_read_finish();
out:

	return err;
}

error_t
vgic_its_cmd_inv(vgic_its_t *vgic_its, platform_msi_device_id_t device,
		 platform_msi_event_id_t event)
{
	error_t		       err;
	vgic_its_dte_result_t  dte_r;
	vgic_its_itte_result_t itte_r;

	assert(vgic_its != NULL);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "INV {:#x}/{:#x}", device, event);

	dte_r = vgic_its_lookup_dte(vgic_its, device);
	if (OK != dte_r.e) {
		err = dte_r.e;
		goto out;
	}

	itte_r = vgic_its_lookup_itte(vgic_its, device, dte_r.r, event, true);
	if (OK != itte_r.e) {
		err = itte_r.e;
		goto out;
	}

	err = vgic_its_inv(vgic_its, device, event, itte_r.r);
out:

	return err;
}

error_t
vgic_its_cmd_invall(vgic_its_t *vgic_its, vgic_its_ic_id_t icid)
{
	vgic_its_cte_t	    cte;
	vgic_its_rdbase_t   rdbase;
	vic_t		   *vic;
	thread_ptr_result_t vcpu_r;

	assert(vgic_its != NULL);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "INVALL {:#x}", icid);

	error_t err;

	if (icid >= util_array_size(vgic_its->collection_table)) {
		// Collection is out of range
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	cte = vgic_its->collection_table[icid];
	if (!vgic_its_cte_get_valid(&cte)) {
		// Collection is not mapped
		TRACE(VGIC, WARN,
		      "VITS {:#x}: IC {:d} invalid for INVALL ({:#x})",
		      (uintptr_t)vgic_its, icid, cte.bf[0]);
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	rdbase = vgic_its_cte_get_rd_index(&cte);
	rcu_read_start();
	vic    = atomic_load_consume(&vgic_its->owner);
	vcpu_r = vgic_its_lookup_vcpu(vic, rdbase);
	if (vcpu_r.e != OK) {
		err = vcpu_r.e;
		goto out_rcu;
	}

	if (!vic->vlpi_config_valid) {
		err = ERROR_OBJECT_CONFIG;
		goto out_rcu;
	}

	if (vgic_gicr_copy_propbase_all(vic, vcpu_r.r, false)) {
		atomic_store_relaxed(&vgic_its->pending_sync_needed, true);
	}

	err = vgic_its_invall(vgic_its, vcpu_r.r);
	vgic_its_sync_all(vgic_its);
	if (err != OK) {
		goto out_rcu;
	}

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
	// The physical collection tables are configured at boot time by
	// gicv3_its_init_cpu_one and do not change during runtime.
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
	// The physical collection tables are configured at boot time by
	// gicv3_its_init_cpu_one and do not change during runtime.
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
	// The physical ITTs are already allocated and assigned to DTEs by
	// by gicv3_its_init via gicv3_its_allocate_itt.
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

	error_t err;

	// If we own the physical device, we must ensure that all events are
	// unmapped from it.
	rcu_read_start();
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
		vgic_its_sync_all(vgic_its);
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
	error_t			       err;
	vgic_its_rdbase_t	       rdbase;
	thread_ptr_result_t	       vcpu_r;
	vic_t			      *vic;
	platform_msi_device_t	      *phys_device;
	vgic_its_dte_result_t	       dte_r;
	vgic_its_itte_result_t	       itte_r;
	vgic_its_itte_t		       itte_old;
	vgic_its_itte_t		       itte_new;
	_Atomic vgic_delivery_state_t *dstate;

	assert(NULL != vgic_its);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "MAPI/MAPTI: {#:x} {:#x} -> {:d}",
		       ((uint64_t)device << 32U) | event, icid, vlpi);

	if (icid >= util_array_size(vgic_its->collection_table)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (vlpi >= util_bit(VGIC_LPI_BITS)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	dte_r = vgic_its_lookup_dte(vgic_its, device);
	if (OK != dte_r.e) {
		err = dte_r.e;
		goto out;
	}

	itte_r = vgic_its_lookup_itte(vgic_its, device, dte_r.r, event, false);
	if (OK != itte_r.e) {
		err = itte_r.e;
		goto out;
	}

	itte_old = itte_r.r;

	itte_new = vgic_its_itte_default();
	vgic_its_itte_set_valid(&itte_new, true);
	vgic_its_itte_set_lpi(&itte_new, vlpi);
	vgic_its_itte_set_icid(&itte_new, icid);

	// If we already have a valid itte and it is equivalent to the new one,
	// there is nothing to do. This catches double MAPTI calls with the
	// same arguments which would otherwise increase the pLPI refcount. In
	// this case, the equal number of DISCARD commands would be necessary
	// to eventually lower it to 0 and allow the pLPI to be released.
	if (vgic_its_itte_get_valid(&itte_old) &&
	    vgic_its_itte_is_equal(itte_old, itte_new)) {
		err = OK;
		goto out;
	}

	err = vgic_its_copy_out_itte(vgic_its, dte_r.r, event, itte_new);
	if (err != OK) {
		goto out;
	}

	// Collection is no longer empty; MAPC can't skip ITT iteration
	vgic_its_cte_t *cte = &vgic_its->collection_table[icid];
	vgic_its_cte_set_known_empty(cte, false);

	// If there is a physical device and the collection is mapped to a valid
	// virtual GICR, we must establish a pLPI to vLPI mapping.
	if (!vgic_its_cte_get_valid(cte)) {
		// The collection is not mapped; there's nothing to do.
		err = OK;
		goto out;
	}

	rdbase = vgic_its_cte_get_rd_index(cte);

	rcu_read_start();

	vic    = atomic_load_consume(&vgic_its->owner);
	vcpu_r = vgic_its_lookup_vcpu(vic, rdbase);
	if (vcpu_r.e != OK) {
		// The mapped GICR is not valid; there's nothing to do.
		err = OK;
		goto out_rcu;
	}

	// Allocate vLPI range if it doesn't exist and initialize the dstate if
	// it has not been done yet. If the vLPI has been mapped before, its
	// state is left untouched.
	dstate = vgic_allocate_vlpi_dstate(vcpu_r.r, vlpi);
	if (dstate == NULL) {
		err = ERROR_NORESOURCES;
		goto out_rcu;
	}

	// Ensure that the cached LPI configuration is up to date. Ignore the
	// return value as we force-sync after the MAPTI command anyway.
	(void)vgic_inv_vlpi(vic, vcpu_r.r, vlpi, dstate);

	// FIXME: If the vLPI is already allocated and is mapped to a different
	// vCPU, the new mapping should be tracked by a separate dstate.
	// With only a single dstate, vLPIs for hardware events previously
	// mapped to this vLPI cannot be injected anymore once the route of
	// the dstate is changed.
	vgic_vlpi_update_route(dstate, vlpi, vcpu_r.r);

	// Find the physical device, if any.
	phys_device = vgic_its_lookup_phys_device(vgic_its, device, false);
	if (phys_device == NULL) {
		// Virtual device, skip mapping a pLPI.
		goto out_rcu;
	}

	// If we had a previous valid mapping, there are 3 possible cases in
	// which we reach here:
	// * Only the vLPI number changed.
	// * Only the ICID changed.
	// * Both changed.
	// The case where nothing changed is already handled above. In any of
	// these other 3 cases, unmap the old vLPI before establishing a new
	// mapping. If the redistributor changes, an entirely new pLPi needs
	// to be selected anyway, as the pLPI ranges are bound to vCPUs.
	// FIXME: It would be faster to give vgic_its_map() a hint that this
	// vLPI is already mapped to a pLPI in the 1st case.
	if (vgic_its_itte_get_valid(&itte_old)) {
		err = vgic_its_unmap_existing(vgic_its, vic, itte_old, itte_new,
					      rdbase);
		if (err != OK) {
			TRACE(VGIC, WARN,
			      "VITS {:#x}: Unmap failed for vLPI {:d}: {:d}",
			      (uintptr_t)vgic_its,
			      vgic_its_itte_get_lpi(&itte_old),
			      (register_t)err);
		}
	}

	err = vgic_its_map(vgic_its, phys_device, event, vcpu_r.r, vlpi, true);
	if (err != OK) {
		goto out_rcu;
	}

	bitmap_set(phys_device->vgic_its_mapped_events, event);

out_rcu:
	rcu_read_finish();

out:
	return err;
}

error_t
vgic_its_cmd_movall(vgic_its_t *vgic_its, vgic_its_rdbase_t rdbase_src,
		    vgic_its_rdbase_t rdbase_dst)
{
	(void)vgic_its;
	(void)rdbase_src;
	(void)rdbase_dst;

	return ERROR_UNIMPLEMENTED;
}

error_t
vgic_its_cmd_movi(vgic_its_t *vgic_its, platform_msi_device_id_t device,
		  platform_msi_event_id_t event, vgic_its_ic_id_t icid)
{
	error_t		       err;
	vgic_its_dte_result_t  dte_r;
	vgic_its_itte_result_t itte_r;
	vgic_its_itte_t	       itte;
	vgic_its_ic_id_t       icid_source;
	vgic_its_cte_result_t  cte_source_r;
	vgic_its_cte_t	       cte_source;
	vgic_its_cte_t	       cte_target;
	vgic_its_rdbase_t      rdbase_target;
	vgic_its_rdbase_t      rdbase_source;
	thread_ptr_result_t    target_vcpu_r;
	thread_ptr_result_t    source_vcpu_r;
	thread_t	      *target_vcpu;
	thread_t	      *source_vcpu;
	vic_t		      *vic;
	platform_msi_device_t *phys_device;

	assert(vgic_its != NULL);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "MOVI {:#x}/{:#x} -> {:#x}", device,
		       event, icid);

	dte_r = vgic_its_lookup_dte(vgic_its, device);
	if (OK != dte_r.e) {
		err = dte_r.e;
		goto out;
	}

	itte_r = vgic_its_lookup_itte(vgic_its, device, dte_r.r, event, false);
	if (OK != itte_r.e) {
		err = itte_r.e;
		goto out;
	}

	itte	    = itte_r.r;
	icid_source = vgic_its_itte_get_icid(&itte);

	if (icid_source == icid) {
		// No change in ICID; nothing to do
		err = OK;
		goto out;
	}

	cte_source_r = vgic_its_lookup_cte(vgic_its, itte, device, event);
	if (cte_source_r.e != OK) {
		err = cte_source_r.e;
		goto out;
	}

	cte_source    = cte_source_r.r;
	rdbase_source = vgic_its_cte_get_rd_index(&cte_source);

	if (icid >= util_array_size(vgic_its->collection_table)) {
		// Collection is out of range
		err = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	cte_target = vgic_its->collection_table[icid];
	if (!vgic_its_cte_get_valid(&cte_target)) {
		// Collection is not mapped
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	rdbase_target = vgic_its_cte_get_rd_index(&cte_target);

	rcu_read_start();
	vic	      = atomic_load_consume(&vgic_its->owner);
	target_vcpu_r = vgic_its_lookup_vcpu(vic, rdbase_target);
	if (target_vcpu_r.e != OK) {
		err = target_vcpu_r.e;
		goto out_rcu;
	}

	target_vcpu = target_vcpu_r.r;

	source_vcpu_r = vgic_its_lookup_vcpu(vic, rdbase_source);
	if (source_vcpu_r.e != OK) {
		err = source_vcpu_r.e;
		goto out_rcu;
	}

	source_vcpu = source_vcpu_r.r;

	phys_device = vgic_its_lookup_phys_device(vgic_its, device, false);
	if ((phys_device != NULL) &&
	    !bitmap_isset(phys_device->vgic_its_mapped_events, event)) {
		// The physical event is unmapped; the VM has been messing with
		// the translation tables.
		err = ERROR_ARGUMENT_INVALID;
	} else {
		bool	     purely_virtual = (NULL == phys_device);
		virq_t	     vlpi	    = vgic_its_itte_get_lpi(&itte);
		irq_result_t plpi_r;
		cpu_index_t  lpi_aff_src;
		cpu_index_t  lpi_aff_tgt;
		platform_msi_controller_id_t phys_index;
		platform_msi_id_t	     msi_id;

		if (vlpi >= util_bit(VGIC_LPI_BITS)) {
			err = ERROR_ARGUMENT_INVALID;
			goto out_rcu;
		}

		plpi_r = vgic_move_vlpi(source_vcpu, target_vcpu, vlpi,
					purely_virtual);

		if (OK != plpi_r.e) {
			err = plpi_r.e;
			goto out_rcu;
		}

		if (purely_virtual) {
			err = OK;
			// Update cached redistributor state on next SYNC.
			atomic_store_relaxed(&vgic_its->pending_sync_needed,
					     true);
			goto out_rcu;
		}

		lpi_aff_src =
			atomic_load_relaxed(&source_vcpu->vgic_lpi_affinity);
		// If a pLPI was assigned, the affinty must also have been
		// changed from its initial value.
		assert(lpi_aff_src != PLATFORM_MAX_CORES);

		// If the LPI affinity of the vCPU is not set yet, set it to its
		// current affinity and use it for the entire lifetime of the
		// vCPU.
		lpi_aff_tgt =
			atomic_load_relaxed(&target_vcpu->vgic_lpi_affinity);
		if (lpi_aff_tgt == PLATFORM_MAX_CORES) {
			lpi_aff_tgt =
				vgic_try_set_vcpu_lpi_affinity(target_vcpu);
		}

		phys_index = atomic_load_consume(&vgic_its->phys_index);

		// We cannot simply use MOVI here because moving a vLPI to a
		// different vCPU causes the old pLPI to be unmapped and a new
		// one to be chosen from a range tied to the new vCPU.
		msi_id = platform_msi_id_default();
		platform_msi_id_set_its_index(&msi_id, phys_index);
		platform_msi_id_set_device_id(&msi_id, device);
		platform_msi_id_set_event_id(&msi_id, event);

		// FIXME: To handle errors here, a rollback mechanism
		// would be needed.

		// The first invalidate ensures that the configuration update
		// for the pLPI disabled by vgic_move_vlpi is visible.
		err = gicv3_its_invalidate(msi_id);
		assert(OK == err);

		// Discard the old mapping.
		err = gicv3_its_discard(msi_id);
		assert(OK == err);

		// Map  the new pLPI using the LPI affinity of the target vCPU.
		err = gicv3_its_map(msi_id, lpi_aff_tgt, plpi_r.r);
		assert(OK == err);

		// The second invalidate ensures that the configuration update
		// for the new pLPI performed by vgic_move_vlpi is visible.
		err = gicv3_its_invalidate(msi_id);
		assert(OK == err);

		// Sync both redistributors.
		vgic_its_sync(vgic_its, lpi_aff_src);
		vgic_its_sync(vgic_its, lpi_aff_tgt);

		// Update cached redistributor state on next SYNC.
		atomic_store_relaxed(&vgic_its->pending_sync_needed, true);
	}

out_rcu:
	rcu_read_finish();
out:
	if (err == OK) {
		vgic_its_itte_set_icid(&itte_r.r, icid);
		err = vgic_its_copy_out_itte(vgic_its, dte_r.r, event,
					     itte_r.r);
	}

	return err;
}

error_t
vgic_its_cmd_sync(vgic_its_t *vgic_its, vgic_its_rdbase_t rdbase)
{
	vic_t		   *vic;
	size_t		    max_lpi;
	thread_ptr_result_t vcpu_r;
	bool		    sync_needed;

	assert(vgic_its != NULL);

	VGIC_ITS_TRACE(vgic_its, COMMAND, "SYNC {:d}", rdbase);

	error_t err = OK;

	sync_needed = atomic_load_relaxed(&vgic_its->pending_sync_needed);
	if (!sync_needed) {
		goto out;
	}

	rcu_read_start();
	vic = atomic_load_consume(&vgic_its->owner);

	vcpu_r = vgic_its_lookup_vcpu(vic, rdbase);
	if (vcpu_r.e == OK) {
		goto out_rcu;
	}

	max_lpi = util_bit(vic->gicd_idbits);

	assert_debug(vcpu_r.r != NULL);

	vgic_vlpi_deliver_pending_sync(vcpu_r.r, max_lpi);
	vgic_sync_all(vic, false);

	sync_needed = false;
	(void)atomic_compare_exchange_strong_explicit(
		&vgic_its->pending_sync_needed, &sync_needed, false,
		memory_order_relaxed, memory_order_relaxed);
out_rcu:
	rcu_read_finish();

out:

	return err;
}
