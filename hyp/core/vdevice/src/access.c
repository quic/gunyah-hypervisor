// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <addrspace.h>
#include <atomic.h>
#include <memdb.h>
#include <memextent.h>
#include <range_tree.h>
#include <rcu.h>

#include <events/vdevice.h>

#include "event_handlers.h"
#include "internal.h"

vcpu_trap_result_t
vdevice_access_phys(paddr_t pa, size_t size, register_t *val, bool is_write)
{
	vcpu_trap_result_t ret;

	memdb_obj_type_result_t res = memdb_lookup(pa);
	if ((res.e != OK) || (res.r.type != MEMDB_TYPE_EXTENT)) {
		ret = VCPU_TRAP_RESULT_UNHANDLED;
		goto out;
	}

	memextent_t *me = (memextent_t *)res.r.object;
	assert(me != NULL);
	vdevice_t *vdevice = atomic_load_consume(&me->vdevice);
	if (vdevice == NULL) {
		ret = VCPU_TRAP_RESULT_UNHANDLED;
		goto out;
	}

	size_result_t offset_r = memextent_get_offset_for_pa(me, pa, size);
	if (offset_r.e != OK) {
		ret = VCPU_TRAP_RESULT_UNHANDLED;
		goto out;
	}

	ret = trigger_vdevice_access_event(vdevice->type, vdevice, offset_r.r,
					   size, val, is_write);

out:
	// Handlers for vdevice_access might need to drop the RCU critical
	// section in order to block the calling VCPU. Currently the events
	// generator can't handle dropping the RCU critical section in the
	// default case, so the handlers that do drop it are forced to
	// re-acquire it, and we must drop it again here.
	// FIXME: QC Gunyah issue #252
	rcu_read_finish();

	return ret;
}

vcpu_trap_result_t
vdevice_access_ipa(vmaddr_t ipa, size_t size, register_t *val, bool is_write)
{
	vcpu_trap_result_t ret;

	addrspace_t *addrspace = addrspace_get_self();
	assert(addrspace != NULL);

	rcu_read_start();

	range_tree_lookup_result_t lookup_ret =
		range_tree_lookup(&addrspace->vdevice_tree, ipa, size);

	if (lookup_ret.size != size) {
		rcu_read_finish();
		ret = VCPU_TRAP_RESULT_UNHANDLED;
	} else if (lookup_ret.node != NULL) {
		vdevice_t *vdevice =
			vdevice_container_of_range(lookup_ret.node);
		assert(vdevice != NULL);
		assert((ipa >= vdevice->range.base) &&
		       ((ipa + size - 1U) <=
			(vdevice->range.base + vdevice->range.size - 1U)));

		ret = trigger_vdevice_access_event(vdevice->type, vdevice,
						   ipa - vdevice->range.base,
						   size, val, is_write);

		// Handlers for vdevice_access might need to drop the RCU
		// critical section in order to block the calling VCPU.
		// Currently the events generator can't handle dropping the RCU
		// critical section in the default case, so the handlers that do
		// drop it are forced to re-acquire it, and we must drop it
		// again here.
		// FIXME: QC Gunyah issue #252
		rcu_read_finish();
	} else {
		rcu_read_finish();

		// FIXME: QC Gunyah issue #179
		ret = trigger_vdevice_access_fixed_addr_event(ipa, size, val,
							      is_write);
	}

	return ret;
}

error_t
vdevice_handle_addrspace_trigger_msi(addrspace_t *addrspace, vmaddr_t mailbox,
				     uint32_t message, index_t device_id)
{
	error_t ret;

	rcu_read_start();
	range_tree_lookup_result_t lookup_ret = range_tree_lookup(
		&addrspace->vdevice_tree, mailbox, sizeof(uint32_t));

	if ((lookup_ret.node == NULL) ||
	    (lookup_ret.size != sizeof(uint32_t))) {
		ret = ERROR_ADDR_INVALID;
		goto out;
	}

	vdevice_t *vdevice = vdevice_container_of_range(lookup_ret.node);
	assert_debug(vdevice != NULL);

	ret = trigger_vdevice_trigger_msi_event(vdevice->type, vdevice,
						mailbox - vdevice->range.base,
						message, device_id);

out:
	rcu_read_finish();
	return ret;
}
