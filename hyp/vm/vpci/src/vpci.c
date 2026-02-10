// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hyprights.h>

#include <atomic.h>
#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <memextent.h>
#include <object.h>
#include <panic.h>
#include <rcu.h>
#include <spinlock.h>
#include <util.h>
#include <vdevice.h>
#include <vic.h>
#include <virq.h>
#include <vpci.h>

#include "event_handlers.h"
#include "host_bridge.h"
#include "mapping.h"

static error_t
vpci_validate_bars(vpci_function_t *function)
{
	error_t ret;

	// Errors here are reported as ERROR_FAILURE rather than invalid
	// argument, etc, because they represent an internal failure in the
	// device object, rather than a problem with the bind call.
	for (index_t j = 0U; j < util_array_size(function->bars); j++) {
		vpci_bar_data_t *bar = &function->bars[j];

		bar->index = (uint8_t)j;

		vdevice_init(&bar->vdevice);
		bar->pci_addr = 0U;
		bar->ipa_addr = 0U;
		if (bar->size_bits == 0U) {
			continue;
		}

		if (bar->size_bits <
		    ((bar->address_type == VPCI_BAR_ADDRESS_TYPE_IO) ? 2U
								     : 4U)) {
			// Address bits will conflict with type bits
			ret = ERROR_FAILURE;
			goto out;
		}
		if ((bar->access_type != VPCI_BAR_ACCESS_TYPE_TRAPPED) &&
		    (bar->memextent == NULL)) {
			// A memextent is needed, but none is present
			ret = ERROR_FAILURE;
			goto out;
		}
		if ((bar->address_type == VPCI_BAR_ADDRESS_TYPE_MEM_64) &&
		    (((j + 1U) == (count_t)util_array_size(function->bars)) ||
		     (function->bars[j + 1U].address_type !=
		      VPCI_BAR_ADDRESS_TYPE_MEM_64_HIGH))) {
			// A 64-bit BAR must be followed by an empty BAR
			// that is marked as having the high bits of the
			// address.
			ret = ERROR_FAILURE;
			goto out;
		}
		if ((bar->address_type == VPCI_BAR_ADDRESS_TYPE_MEM_64_HIGH) &&
		    ((j == 0U) || (function->bars[j - 1U].address_type !=
				   VPCI_BAR_ADDRESS_TYPE_MEM_64))) {
			ret = ERROR_FAILURE;
			goto out;
		}

		if (bar->memextent != NULL) {
			size_result_t size_r =
				memextent_get_mapped_size(bar->memextent);
			if ((size_r.e != OK) ||
			    (size_r.r > util_bit(bar->size_bits))) {
				// Memextent will occupy more address
				// space than is declared in the BAR
				ret = ERROR_FAILURE;
				goto out;
			}
		}
	}

	ret = OK;
out:
	return ret;
}

index_result_t
vpci_bind_device(vpci_t *vpci, index_t slot_index, vpci_device_t *device)
{
	error_t err;
	index_t slot_index_ret = slot_index;

	if (device->bus != NULL) {
		// Device is already in a slot
		err = ERROR_BUSY;
		goto out;
	}

	if (device->functions[0] == NULL) {
		// Device has no primary function
		err = ERROR_FAILURE;
		goto out;
	}

	if (slot_index == ~(index_t)0U) {
		// Search for a free slot
		for (index_t i = 0U; i < util_array_size(vpci->slots); i++) {
			if (atomic_load_relaxed(&vpci->slots[i]) == NULL) {
				slot_index_ret = i;
				break;
			}
		}
		if (slot_index_ret >= util_array_size(vpci->slots)) {
			// All slots are full
			err = ERROR_NORESOURCES;
			goto out;
		}
	} else if (slot_index < util_array_size(vpci->slots)) {
		if (atomic_load_relaxed(&vpci->slots[slot_index]) != NULL) {
			// Specified slot is full
			err = ERROR_NORESOURCES;
			goto out;
		}
	} else {
		// Slot index out of range
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	for (index_t i = 0U; i < util_array_size(device->functions); i++) {
		vpci_function_t *function = device->functions[i];
		if (function == NULL) {
			continue;
		}
		function->device	 = device;
		function->function_index = i;

		// Reset the command register. This ensures that none of the
		// BARs are initially mapped, because it clears the IO and mem
		// space enable bits.
		function->command = pci_function_command_default();

		// Validate the function's BAR configuration.
		err = vpci_validate_bars(function);
		if (err != OK) {
			goto out;
		}
	}

	err = vpci_memextent_claim(device);
	if (err != OK) {
		goto out;
	}

	spinlock_init(&device->mapping_lock);
	spinlock_init(&device->irq_bind_lock);

	device->bus  = object_get_vpci_additional(vpci);
	device->slot = slot_index_ret;
	atomic_store_release(&vpci->slots[slot_index_ret], device);
	err = OK;

out:
	return (index_result_t){ .e = err, .r = slot_index_ret };
}

void
vpci_unbind_device(vpci_device_t *device)
{
	vpci_t *vpci = device->bus;
	if (vpci == NULL) {
		// Device is already unbound
		goto out;
	}

	assert(device->slot < util_array_size(vpci->slots));
	vpci_device_t *slot_device = device;
	if (!atomic_compare_exchange_strong_explicit(
		    &vpci->slots[device->slot], &slot_device, NULL,
		    memory_order_relaxed, memory_order_relaxed)) {
		panic("vpci_unbind_device: wrong device in slot!");
	}
	device->slot = ~(index_t)0U;

	spinlock_acquire(&device->irq_bind_lock);
	vic_unbind(&device->virq_irqa);
#if VPCI_MULTI_FUNCTION
	vic_unbind(&device->virq_irqb);
	vic_unbind(&device->virq_irqc);
	vic_unbind(&device->virq_irqd);
#endif
	spinlock_release(&device->irq_bind_lock);

	vpci_deactivate_device_mappings(vpci, device);
	vpci_memextent_release(device);

out:
	return;
}

void
vpci_cleanup_device(vpci_device_t *device, bool reuse)
{
	vpci_t *vpci = device->bus;
	if (vpci == NULL) {
		// Device was never bound
		goto out;
	}

	vpci_cleanup_device_mappings(vpci, device);

	if (reuse) {
		device->bus	  = NULL;
		device->virq_irqa = (virq_source_t){ 0 };
#if VPCI_MULTI_FUNCTION
		device->virq_irqb = (virq_source_t){ 0 };
		device->virq_irqc = (virq_source_t){ 0 };
		device->virq_irqd = (virq_source_t){ 0 };
#endif
	}

	// Release the device's reference to the bus
	object_put_vpci(vpci);

out:
	return;
}

error_t
vpci_handle_object_create_vpci(vpci_create_t vpci_create)
{
	vpci_host_bridge_init(vpci_create.vpci);
	vdevice_init(&vpci_create.vpci->vdevice);

	error_t err = vpci_bind_device(vpci_create.vpci, 0U,
				       &vpci_create.vpci->bridge)
			      .e;
	assert(err == OK);
	if (err == OK) {
		// vpci_bind_device() takes a reference to the bus, but for a
		// host bridge device that is a self-reference, so we need to
		// release it. We must also avoid calling vpci_cleanup_device()
		// on the host bridge for this reason.
		object_put_vpci(vpci_create.vpci);
	}

	return err;
}

bool
vpci_is_pcie(vpci_t *vpci)
{
#if VPCI_PCIE_ONLY
	(void)vpci;
	return true;
#else
#error Unimplemented
#endif
}

error_t
vpci_handle_object_activate_vpci(vpci_t *vpci)
{
	error_t ret;

	assert(vpci != NULL);

	if ((vpci->addrspace == NULL) || (vpci->vic == NULL)) {
		ret = ERROR_OBJECT_CONFIG;
		goto out;
	}

	if ((vpci->cam_size < util_bit(vpci_is_pcie(vpci) ? 21U : 17U)) ||
	    !util_is_baligned(vpci->cam_base, vpci->cam_size)) {
		ret = ERROR_OBJECT_CONFIG;
		goto out;
	}

	if ((vpci->npmem_size < PGTABLE_VM_PAGE_SIZE) ||
	    !util_is_baligned(vpci->npmem_base, vpci->npmem_size)) {
		ret = ERROR_OBJECT_CONFIG;
		goto out;
	}
	if ((vpci->pmem_size >= PGTABLE_VM_PAGE_SIZE) &&
	    !util_is_baligned(vpci->pmem_base, vpci->pmem_size)) {
		ret = ERROR_OBJECT_CONFIG;
		goto out;
	}
	if ((vpci->io_size >= PGTABLE_VM_PAGE_SIZE) &&
	    !util_is_baligned(vpci->io_base, vpci->io_size)) {
		ret = ERROR_OBJECT_CONFIG;
		goto out;
	}

	error_t err = vdevice_attach_vmaddr(VDEVICE_TYPE_VPCI_CAM,
					    &vpci->vdevice, vpci->addrspace,
					    vpci->cam_base, vpci->cam_size);
	if (err != OK) {
		ret = err;
		goto out;
	}

	ret = OK;

out:
	return ret;
}

void
vpci_handle_object_deactivate_vpci(vpci_t *vpci)
{
	vpci_unbind_device(&vpci->bridge);

	// Attached devices are supposed to hold references to the bus, so there
	// should be none left by now.
	for (index_t i = 0U; i < util_array_size(vpci->slots); i++) {
		assert(atomic_load_relaxed(&vpci->slots[i]) == NULL);
	}

	if (vpci->addrspace != NULL) {
		if (vpci->vdevice.type != VDEVICE_TYPE_NONE) {
			vdevice_detach_vmaddr(&vpci->vdevice);
		}
	}
}

void
vpci_handle_object_cleanup_vpci(vpci_t *vpci)
{
	// We can't call vpci_cleanup_device() on the host bridge, because it
	// will try to release a reference to the bus, which we already released
	// immediately after binding it. Clean up the BAR mappings directly.
	vpci_cleanup_device_mappings(vpci, &vpci->bridge);
	vpci_host_bridge_cleanup(vpci);

	if (vpci->addrspace != NULL) {
		object_put_addrspace(vpci->addrspace);
		vpci->addrspace = NULL;
	}

	if (vpci->vic != NULL) {
		object_put_vic(vpci->vic);
		vpci->vic = NULL;
	}
}

error_t
vpci_handle_vic_bind_virq(cap_id_t irq_obj_cap, vic_t *vic, index_t index,
			  virq_t virq)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	vpci_ptr_result_t vpci_r = cspace_lookup_vpci_any(cspace, irq_obj_cap,
							  CAP_RIGHTS_VPCI_BIND);

	if (compiler_unexpected(vpci_r.e != OK)) {
		err = vpci_r.e;
		goto out;
	}

	rcu_read_start();
	// Only #IRQA is supported at present
	virq_trigger_t		 virq_trigger;
	virq_source_ptr_result_t virq_source_r = vpci_get_irq_source(
		vpci_r.r, index, PCI_INTERRUPT_PIN_IRQA, &virq_trigger);
	if (virq_source_r.e == OK) {
		err = vic_bind_shared(virq_source_r.r, vic, virq, virq_trigger);
	} else {
		err = virq_source_r.e;
	}
	rcu_read_finish();

	object_put_vpci(vpci_r.r);
out:
	return err;
}

error_t
vpci_handle_vic_unbind_virq(cap_id_t irq_obj_cap, index_t index)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	vpci_ptr_result_t vpci_r = cspace_lookup_vpci_any(cspace, irq_obj_cap,
							  CAP_RIGHTS_VPCI_BIND);

	if (compiler_unexpected(vpci_r.e != OK)) {
		err = vpci_r.e;
		goto out;
	}

	rcu_read_start();
	// Only #IRQA binding is supported at present
	virq_trigger_t		 virq_trigger;
	virq_source_ptr_result_t virq_source_r = vpci_get_irq_source(
		vpci_r.r, index, PCI_INTERRUPT_PIN_IRQA, &virq_trigger);

	if (compiler_unexpected(virq_source_r.e != OK)) {
		err = virq_source_r.e;
		goto fail_get_source;
	}

	// We can only do an unsynchronised unbind here because the source
	// will become invalid when we drop the RCU critical section. So, the
	// function can't be bound to a different IRQ afterwards.
	vic_unbind(virq_source_r.r);

	err = OK;

fail_get_source:
	rcu_read_finish();
	object_put_vpci(vpci_r.r);
out:
	return err;
}
