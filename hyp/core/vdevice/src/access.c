// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <addrspace.h>
#include <atomic.h>
#include <gpt.h>
#include <memdb.h>
#include <memextent.h>
#include <rcu.h>

#include <events/vdevice.h>

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
	return ret;
}

vcpu_trap_result_t
vdevice_access_ipa(vmaddr_t ipa, size_t size, register_t *val, bool is_write)
{
	vcpu_trap_result_t ret;

	addrspace_t *addrspace = addrspace_get_self();
	assert(addrspace != NULL);

	rcu_read_start();

	gpt_lookup_result_t lookup_ret =
		gpt_lookup(&addrspace->vdevice_gpt, ipa, size);

	if (lookup_ret.size != size) {
		ret = VCPU_TRAP_RESULT_UNHANDLED;
	} else if (lookup_ret.entry.type == GPT_TYPE_VDEVICE) {
		vdevice_t *vdevice = lookup_ret.entry.value.vdevice;
		assert(vdevice != NULL);
		assert((ipa >= vdevice->ipa) &&
		       ((ipa + size - 1U) <=
			(vdevice->ipa + vdevice->size - 1U)));

		ret = trigger_vdevice_access_event(vdevice->type, vdevice,
						   ipa - vdevice->ipa, size,
						   val, is_write);
	} else {
		assert(lookup_ret.entry.type == GPT_TYPE_EMPTY);

		// FIXME:
		ret = trigger_vdevice_access_fixed_addr_event(ipa, size, val,
							      is_write);
	}

	rcu_read_finish();

	return ret;
}
