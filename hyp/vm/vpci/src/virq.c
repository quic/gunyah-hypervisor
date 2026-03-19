// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <addrspace.h>
#include <atomic.h>
#include <util.h>
#include <virq.h>
#include <vpci.h>

#include <events/vpci.h>

#include "event_handlers.h"

virq_source_ptr_result_t
vpci_get_irq_source(vpci_t *vpci, index_t slot_index,
		    pci_interrupt_pin_t interrupt_pin,
		    virq_trigger_t     *virq_trigger)
{
	virq_source_ptr_result_t ret;

	if (slot_index >= util_array_size(vpci->slots)) {
		ret = virq_source_ptr_result_error(ERROR_ARGUMENT_INVALID);
		goto out;
	}

	vpci_device_t *device = atomic_load_consume(&vpci->slots[slot_index]);
	if (device == NULL) {
		ret = virq_source_ptr_result_error(ERROR_IDLE);
		goto out;
	}

	switch ((pci_interrupt_pin_t)interrupt_pin) {
	case PCI_INTERRUPT_PIN_IRQA:
		*virq_trigger = VIRQ_TRIGGER_VPCI_IRQA;
		ret	      = virq_source_ptr_result_ok(&device->virq_irqa);
		break;
#if VPCI_MULTI_FUNCTION
	case PCI_INTERRUPT_PIN_IRQB:
		*virq_trigger = VIRQ_TRIGGER_VPCI_IRQB;
		ret	      = virq_source_ptr_result_ok(&device->virq_irqb);
		break;
	case PCI_INTERRUPT_PIN_IRQC:
		*virq_trigger = VIRQ_TRIGGER_VPCI_IRQC;
		ret	      = virq_source_ptr_result_ok(&device->virq_irqc);
		break;
	case PCI_INTERRUPT_PIN_IRQD:
		*virq_trigger = VIRQ_TRIGGER_VPCI_IRQD;
		ret	      = virq_source_ptr_result_ok(&device->virq_irqd);
		break;
#else
	case PCI_INTERRUPT_PIN_IRQB:
	case PCI_INTERRUPT_PIN_IRQC:
	case PCI_INTERRUPT_PIN_IRQD:
		ret = virq_source_ptr_result_error(ERROR_NORESOURCES);
		break;
#endif
	case PCI_INTERRUPT_PIN_NONE:
	default:
		ret = virq_source_ptr_result_error(ERROR_ARGUMENT_INVALID);
		break;
	}
out:
	return ret;
}

void
vpci_irq_assert(vpci_function_t *function)
{
	assert(function != NULL);
	vpci_device_t *device = function->device;
	assert(device != NULL);

	switch (function->interrupt_pin) {
	case (uint8_t)PCI_INTERRUPT_PIN_IRQA:
		(void)virq_assert(&device->virq_irqa, false);
		break;
#if VPCI_MULTI_FUNCTION
	case (uint8_t)PCI_INTERRUPT_PIN_IRQB:
		(void)virq_assert(&device->virq_irqb, false);
		break;
	case (uint8_t)PCI_INTERRUPT_PIN_IRQC:
		(void)virq_assert(&device->virq_irqc, false);
		break;
	case (uint8_t)PCI_INTERRUPT_PIN_IRQD:
		(void)virq_assert(&device->virq_irqd, false);
		break;
#else
	case (uint8_t)PCI_INTERRUPT_PIN_IRQB:
	case (uint8_t)PCI_INTERRUPT_PIN_IRQC:
	case (uint8_t)PCI_INTERRUPT_PIN_IRQD:
		// All IRQx# pins are tied together; use IRQA#
		(void)virq_assert(&device->virq_irqa, false);
		break;
#endif
	case (uint8_t)PCI_INTERRUPT_PIN_NONE:
	default:
		// No IRQ pin, or invalid IRQ pin; ignore the assertion
		break;
	}
}

bool
vpci_irq_check_pending(virq_trigger_t trigger, virq_source_t *source,
		       bool reasserted)
{
	bool pending = false;

	// We always return true if reasserted is true, to avoid ordering
	// problems. If reasserted is true, returning false might race with some
	// IRQ source becoming pending after its check-pending handler runs, and
	// incorrectly cancel the new IRQ. The VIRQ framework guarantees that
	// doesn't happen if the reasserted argument is false.
	if (reasserted) {
		pending = true;
		goto out;
	}

#if VPCI_MULTI_FUNCTION
	vpci_device_t	   *device;
	pci_interrupt_pin_t interrupt_pin;

	if (trigger == VIRQ_TRIGGER_VPCI_IRQA) {
		device	      = vpci_device_container_of_virq_irqa(source);
		interrupt_pin = PCI_INTERRUPT_PIN_IRQA;
	} else if (trigger == VIRQ_TRIGGER_VPCI_IRQB) {
		device	      = vpci_device_container_of_virq_irqb(source);
		interrupt_pin = PCI_INTERRUPT_PIN_IRQB;
	} else if (trigger == VIRQ_TRIGGER_VPCI_IRQC) {
		device	      = vpci_device_container_of_virq_irqc(source);
		interrupt_pin = PCI_INTERRUPT_PIN_IRQC;
	} else if (trigger == VIRQ_TRIGGER_VPCI_IRQD) {
		device	      = vpci_device_container_of_virq_irqd(source);
		interrupt_pin = PCI_INTERRUPT_PIN_IRQD;
	} else {
		panic("vpci_irq_check_pending: Unknown trigger");
	}
#else
	vpci_device_t *device = vpci_device_container_of_virq_irqa(source);
	assert(trigger == VIRQ_TRIGGER_VPCI_IRQA);
#endif

	for (index_t i = 0U; i < util_array_size(device->functions); i++) {
		vpci_function_t *function = device->functions[i];
		if (function == NULL) {
			continue;
		}

#if VPCI_MULTI_FUNCTION
		if (function->interrupt_pin != (uint8_t)interrupt_pin) {
			continue;
		}
#else
		// All IRQ lines are tied together
		if (function->interrupt_pin ==
		    (uint8_t)PCI_INTERRUPT_PIN_NONE) {
			continue;
		}
#endif

		if (trigger_vpci_irq_check_pending_event(function->type,
							 function)) {
			pending = true;
			break;
		}
	}

out:
	return pending;
}

static index_t
vpci_msi_device_id(const vpci_t *bus, const vpci_device_t *device,
		   const vpci_function_t *function)
{
	pci_responder_id_t rid = pci_responder_id_default();
	(void)bus; // Bridges not supported; bus index is always 0
	pci_responder_id_set_bus(&rid, 0U);
	pci_responder_id_set_slot(&rid, device->slot);
	pci_responder_id_set_function(&rid, function->function_index);

	return pci_responder_id_raw(rid);
}

void
vpci_dispatch_msi(vpci_function_t *function, vmaddr_t mailbox, uint32_t message)
{
	assert(function != NULL);

	if (!pci_function_command_get_bus_master(&function->command)) {
		// Function is not allowed to be a bus master, so it can't
		// perform MSI writes.
		goto out;
	}

	vpci_device_t *device = function->device;
	assert(device != NULL);
	vpci_t *bus = device->bus;
	assert(bus != NULL);
	index_t device_id = vpci_msi_device_id(bus, device, function);

	error_t err = addrspace_trigger_msi(bus->addrspace, mailbox, message,
					    device_id);
	if (err != OK) {
		vpci_error_from_requester(&bus->bridge_fn0, function, err);
	}

out:
	return;
}
