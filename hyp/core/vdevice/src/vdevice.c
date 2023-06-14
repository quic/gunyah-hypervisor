// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <atomic.h>
#include <gpt.h>
#include <object.h>
#include <spinlock.h>
#include <util.h>
#include <vdevice.h>

#include "event_handlers.h"

error_t
vdevice_attach_phys(vdevice_t *vdevice, memextent_t *memextent)
{
	assert(vdevice != NULL);
	assert(memextent != NULL);
	assert(vdevice->type != VDEVICE_TYPE_NONE);

	vdevice_t *null_vdevice = NULL;
	return atomic_compare_exchange_strong_explicit(&memextent->vdevice,
						       &null_vdevice, vdevice,
						       memory_order_release,
						       memory_order_release)
		       ? OK
		       : ERROR_BUSY;
}

void
vdevice_detach_phys(vdevice_t *vdevice, memextent_t *memextent)
{
	vdevice_t *old_vdevice = atomic_exchange_explicit(
		&memextent->vdevice, NULL, memory_order_relaxed);
	assert(old_vdevice == vdevice);
}

bool
vdevice_handle_gpt_values_equal(gpt_type_t type, gpt_value_t x, gpt_value_t y)
{
	assert(type == GPT_TYPE_VDEVICE);

	return x.vdevice == y.vdevice;
}

error_t
vdevice_handle_object_create_addrspace(addrspace_create_t params)
{
	addrspace_t *addrspace = params.addrspace;
	assert(addrspace != NULL);

	spinlock_init(&addrspace->vdevice_lock);

	gpt_config_t config = gpt_config_default();
	gpt_config_set_max_bits(&config, VDEVICE_MAX_GPT_BITS);
	gpt_config_set_rcu_read(&config, true);

	return gpt_init(&addrspace->vdevice_gpt, addrspace->header.partition,
			config, util_bit((index_t)GPT_TYPE_VDEVICE));
}

void
vdevice_handle_object_cleanup_addrspace(addrspace_t *addrspace)
{
	assert(addrspace != NULL);

	gpt_destroy(&addrspace->vdevice_gpt);
}

error_t
vdevice_attach_vmaddr(vdevice_t *vdevice, addrspace_t *addrspace, vmaddr_t ipa,
		      size_t size)
{
	error_t err;

	assert(vdevice != NULL);
	assert(addrspace != NULL);
	assert(vdevice->type != VDEVICE_TYPE_NONE);

	if (vdevice->addrspace != NULL) {
		err = ERROR_BUSY;
		goto out;
	}

	gpt_entry_t entry = {
		.type  = GPT_TYPE_VDEVICE,
		.value = { .vdevice = vdevice },
	};

	spinlock_acquire(&addrspace->vdevice_lock);

	err = gpt_insert(&addrspace->vdevice_gpt, ipa, size, entry, true);

	spinlock_release(&addrspace->vdevice_lock);

	if (err == OK) {
		vdevice->addrspace = object_get_addrspace_additional(addrspace);
		vdevice->ipa	   = ipa;
		vdevice->size	   = size;
	}

out:
	return err;
}

void
vdevice_detach_vmaddr(vdevice_t *vdevice)
{
	assert(vdevice != NULL);
	assert(vdevice->type != VDEVICE_TYPE_NONE);

	addrspace_t *addrspace = vdevice->addrspace;
	assert(addrspace != NULL);

	gpt_entry_t entry = {
		.type  = GPT_TYPE_VDEVICE,
		.value = { .vdevice = vdevice },
	};

	spinlock_acquire(&addrspace->vdevice_lock);

	error_t err = gpt_remove(&addrspace->vdevice_gpt, vdevice->ipa,
				 vdevice->size, entry);
	assert(err == OK);

	spinlock_release(&addrspace->vdevice_lock);

	vdevice->addrspace = NULL;

	object_put_addrspace(addrspace);
}
