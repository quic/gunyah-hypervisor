// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>
#include <hyprights.h>

#include <atomic.h>
#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <object.h>
#include <rcu.h>
#include <util.h>
#include <virtio.h>
#include <vpci.h>

#include <asm/nospec_checks.h>

#include "event_handlers.h"

static void
virtio_pci_set_device_id(virtio_device_type_t device_type, vpci_function_t *fn)
{
	fn->product_id	    = (uint16_t)device_type + VIRTIO_PCI_DEVICE_ID_BASE;
	fn->vendor_id	    = VIRTIO_PCI_VENDOR_ID;
	fn->device_revision = 1U; // Virtio 1.1 non-transitional
	fn->subsys_vendor_id  = PCI_VENDOR_ID_QUALCOMM;
	fn->subsys_product_id = 0x6001U; // Gunyah Virtio PCI frontend

	switch (device_type) {
	case VIRTIO_DEVICE_TYPE_NETWORK:
		fn->device_class    = PCI_DEVICE_CLASS_NETWORK;
		fn->device_subclass = PCI_DEVICE_SUBCLASS_NETWORK_ETHERNET;
		break;
	case VIRTIO_DEVICE_TYPE_BLOCK:
		fn->device_class    = PCI_DEVICE_CLASS_MASS_STORAGE;
		fn->device_subclass = PCI_DEVICE_SUBCLASS_MASS_STORAGE_OTHER;
		break;
	case VIRTIO_DEVICE_TYPE_CONSOLE:
		fn->device_class    = PCI_DEVICE_CLASS_CONSOLE;
		fn->device_subclass = PCI_DEVICE_SUBCLASS_CONSOLE_OTHER;
		break;
	case VIRTIO_DEVICE_TYPE_BALLOON:
	case VIRTIO_DEVICE_TYPE_MEMORY:
		fn->device_class    = PCI_DEVICE_CLASS_MEMORY;
		fn->device_subclass = PCI_DEVICE_SUBCLASS_MEMORY_RAM;
		break;
	case VIRTIO_DEVICE_TYPE_GPU:
		fn->device_class    = PCI_DEVICE_CLASS_DISPLAY;
		fn->device_subclass = PCI_DEVICE_SUBCLASS_DISPLAY_3D;
		break;
	case VIRTIO_DEVICE_TYPE_INPUT:
		fn->device_class    = PCI_DEVICE_CLASS_INPUT;
		fn->device_subclass = PCI_DEVICE_SUBCLASS_INPUT_OTHER;
		break;
	case VIRTIO_DEVICE_TYPE_SOCKET:
		fn->device_class    = PCI_DEVICE_CLASS_NETWORK;
		fn->device_subclass = PCI_DEVICE_SUBCLASS_NETWORK_OTHER;
		break;
	case VIRTIO_DEVICE_TYPE_IOMMU:
		fn->device_class    = PCI_DEVICE_CLASS_SYSTEM;
		fn->device_subclass = PCI_DEVICE_SUBCLASS_SYSTEM_IOMMU;
		break;
	case VIRTIO_DEVICE_TYPE_INVALID:
	default:
		fn->device_class = PCI_DEVICE_CLASS_OTHER;
		break;
	}
}

error_t
virtio_pci_handle_virtio_startup(virtio_t *virtio)
{
	error_t err = OK;

	uint8_t			cap_offset = 0x40;
	vpci_capability_data_t *cap_list   = NULL;

	virtio_pci_t *pci = &virtio->frontend_data.pci;

	pci->device.functions[0] = &pci->fn0;
	pci->fn0.type		 = VPCI_FUNCTION_TYPE_VIRTIO;

	virtio_pci_set_device_id(virtio->device_type, &pci->fn0);

	pci->fn0.interrupt_pin = (uint8_t)PCI_INTERRUPT_PIN_IRQA;

	// BAR for common config, queue notify, and interrupt status.
	pci->fn0.bars[VIRTIO_PCI_BAR_COMMON].size_bits =
		compiler_msb(sizeof(virtio_pci_bar0_regs_t) - 1U) + 1U;
	pci->fn0.bars[VIRTIO_PCI_BAR_COMMON].address_type =
		VPCI_BAR_ADDRESS_TYPE_MEM_NP_32;
	pci->fn0.bars[VIRTIO_PCI_BAR_COMMON].access_type =
		VPCI_BAR_ACCESS_TYPE_TRAPPED;
	pci->fn0.bars[VIRTIO_PCI_BAR_COMMON].memextent = NULL;

	pci->cap_common_cfg.base.next	= cap_list;
	pci->cap_common_cfg.base.cap_id = (uint8_t)PCI_CAPABILITY_ID_VENDOR;
	pci->cap_common_cfg.base.size =
		(uint8_t)sizeof(virtio_pci_capability_t);
	pci->cap_common_cfg.base.offset = cap_offset;
	pci->cap_common_cfg.type	= VIRTIO_PCI_CAP_TYPE_COMMON_CFG;
	pci->cap_common_cfg.bar_index	= (index_t)VIRTIO_PCI_BAR_COMMON;
	pci->cap_common_cfg.bar_offset	= 0U;
	pci->cap_common_cfg.bar_length	= 0x38U;
	cap_offset += pci->cap_common_cfg.base.size;
	cap_list = &pci->cap_common_cfg.base;

	pci->cap_notify_cfg.base.next	= cap_list;
	pci->cap_notify_cfg.base.cap_id = (uint8_t)PCI_CAPABILITY_ID_VENDOR;
	pci->cap_notify_cfg.base.size =
		(uint8_t)sizeof(virtio_pci_notify_capability_t);
	pci->cap_notify_cfg.base.offset = cap_offset;
	pci->cap_notify_cfg.type	= VIRTIO_PCI_CAP_TYPE_NOTIFY_CFG;
	pci->cap_notify_cfg.bar_index	= (index_t)VIRTIO_PCI_BAR_COMMON;
	pci->cap_notify_cfg.bar_offset =
		offsetof(virtio_pci_bar0_regs_t, queue_notify);
	pci->cap_notify_cfg.bar_length = sizeof(uint32_t);
	cap_offset += pci->cap_notify_cfg.base.size;
	cap_list = &pci->cap_notify_cfg.base;

	pci->cap_isr_cfg.base.next   = cap_list;
	pci->cap_isr_cfg.base.cap_id = (uint8_t)PCI_CAPABILITY_ID_VENDOR;
	pci->cap_isr_cfg.base.size   = (uint8_t)sizeof(virtio_pci_capability_t);
	pci->cap_isr_cfg.base.offset = cap_offset;
	pci->cap_isr_cfg.type	     = VIRTIO_PCI_CAP_TYPE_ISR_CFG;
	pci->cap_isr_cfg.bar_index   = (index_t)VIRTIO_PCI_BAR_COMMON;
	pci->cap_isr_cfg.bar_offset =
		offsetof(virtio_pci_bar0_regs_t, interrupt_status);
	pci->cap_isr_cfg.bar_length = 1U;
	cap_offset += pci->cap_isr_cfg.base.size;
	cap_list = &pci->cap_isr_cfg.base;

	if (virtio->config_size > 0U) {
		// BAR for device config.
		pci->fn0.bars[VIRTIO_PCI_BAR_DEVCFG].size_bits =
			compiler_msb(virtio->config_offset +
				     virtio->config_size - 1U) +
			1U;
		pci->fn0.bars[VIRTIO_PCI_BAR_DEVCFG].address_type =
			VPCI_BAR_ADDRESS_TYPE_MEM_NP_32;
		pci->fn0.bars[VIRTIO_PCI_BAR_DEVCFG].access_type =
			(virtio->config_cache_me != NULL)
				? VPCI_BAR_ACCESS_TYPE_TRAPPED_W
				: VPCI_BAR_ACCESS_TYPE_TRAPPED;
		pci->fn0.bars[VIRTIO_PCI_BAR_DEVCFG].memextent =
			virtio->config_cache_me;

		pci->cap_device_cfg.base.next = cap_list;
		pci->cap_device_cfg.base.cap_id =
			(uint8_t)PCI_CAPABILITY_ID_VENDOR;
		pci->cap_device_cfg.base.size =
			(uint8_t)sizeof(virtio_pci_capability_t);
		pci->cap_device_cfg.base.offset = cap_offset;
		pci->cap_device_cfg.type       = VIRTIO_PCI_CAP_TYPE_DEVICE_CFG;
		pci->cap_device_cfg.bar_index  = (index_t)VIRTIO_PCI_BAR_DEVCFG;
		pci->cap_device_cfg.bar_offset = virtio->config_offset;
		pci->cap_device_cfg.bar_length = virtio->config_size;
		cap_offset += pci->cap_device_cfg.base.size;
		cap_list = &pci->cap_device_cfg.base;
	}

	// Virtio 1.1 requires devices to provide a BAR-access capability, which
	// provides an alternate way to access any BAR without having to map it.
	// This is the only writable capability.
	pci->cap_pci_cfg.base.next   = cap_list;
	pci->cap_pci_cfg.base.cap_id = (uint8_t)PCI_CAPABILITY_ID_VENDOR;
	pci->cap_pci_cfg.base.size   = (uint8_t)sizeof(virtio_pci_capability_t);
	pci->cap_pci_cfg.base.offset = cap_offset;
	pci->cap_pci_cfg.type	     = VIRTIO_PCI_CAP_TYPE_PCI_CFG;
	cap_offset += pci->cap_pci_cfg.base.size;
	cap_list = &pci->cap_pci_cfg.base;

	if (virtio_supports_per_queue_notify(virtio)) {
		count_t vectors = util_min(virtio->vqs + 1U, 2048U);

		// BAR for the MSI-X Table
		size_t table_size = vectors * sizeof(pci_msix_vector_t);

		pci->fn0.bars[VIRTIO_PCI_BAR_MSIX_TABLE].size_bits =
			util_max(compiler_msb(table_size - 1U) + 1U, 4U);
		pci->fn0.bars[VIRTIO_PCI_BAR_MSIX_TABLE].address_type =
			VPCI_BAR_ADDRESS_TYPE_MEM_NP_32;
		pci->fn0.bars[VIRTIO_PCI_BAR_MSIX_TABLE].access_type =
			VPCI_BAR_ACCESS_TYPE_TRAPPED;

		// BAR for the MSI-X PBA
		size_t pba_size =
			util_balign_up(vectors, 64U) / sizeof(uint64_t);
		pci->fn0.bars[VIRTIO_PCI_BAR_MSIX_PBA].size_bits =
			util_max(compiler_msb(pba_size - 1U) + 1U, 4U);
		pci->fn0.bars[VIRTIO_PCI_BAR_MSIX_PBA].address_type =
			VPCI_BAR_ADDRESS_TYPE_MEM_NP_32;
		pci->fn0.bars[VIRTIO_PCI_BAR_MSIX_PBA].access_type =
			VPCI_BAR_ACCESS_TYPE_TRAPPED;

		pci->msix_data.base.next   = cap_list;
		pci->msix_data.base.cap_id = (uint8_t)PCI_CAPABILITY_ID_MSIX;
		pci->msix_data.base.size =
			(uint8_t)sizeof(pci_msix_capability_t);
		pci->msix_data.base.offset  = cap_offset;
		pci->msix_data.table_offset = 0U;
		pci->msix_data.table_bar_index =
			(index_t)VIRTIO_PCI_BAR_MSIX_TABLE;
		pci->msix_data.pba_offset    = 0U;
		pci->msix_data.pba_bar_index = (index_t)VIRTIO_PCI_BAR_MSIX_PBA;
		pci->msix_data.vector_count  = vectors;
		cap_offset += pci->msix_data.base.size;
		cap_list = &pci->msix_data.base;

		err = vpci_msix_init(virtio->partition, &pci->msix_data);
	}

	pci->cap_pm_cfg.next   = cap_list;
	pci->cap_pm_cfg.cap_id = (uint8_t)PCI_CAPABILITY_ID_PM;
	pci->cap_pm_cfg.size   = (uint8_t)sizeof(pci_pm_capability_t);
	pci->cap_pm_cfg.offset = cap_offset;

	cap_offset += pci->cap_pm_cfg.size;
	cap_list = &pci->cap_pm_cfg;

	(void)cap_offset;
	pci->fn0.capabilities = cap_list;

	return err;
}

void
virtio_pci_handle_virtio_shutdown(virtio_t *virtio)
{
	virtio_pci_t *virtio_pci = &virtio->frontend_data.pci;
	vpci_unbind_device(&virtio_pci->device);
	if (virtio_supports_per_queue_notify(virtio)) {
		error_t err = vpci_msix_free(virtio->partition,
					     &virtio_pci->msix_data);
		assert(err == OK);
	}
	rcu_enqueue(&virtio_pci->rcu, RCU_UPDATE_CLASS_VIRTIO_PCI_CLEANUP);
}

rcu_update_status_t
virtio_pci_handle_rcu_update(rcu_entry_t *entry)
{
	virtio_pci_t *virtio_pci = virtio_pci_container_of_rcu(entry);
	vpci_cleanup_device(&virtio_pci->device, false);
	return rcu_update_status_default();
}

void
virtio_pci_handle_virtio_reset_complete(virtio_t *virtio)
{
	// Reset all the queue sizes to the maximum. There is no separate
	// register for max queue size; the guest needs this to determine
	// the max size.
	rcu_read_start();
	for (index_t i = 0U; i < virtio->vqs; i++) {
		count_result_t max_size_r =
			virtio_get_queue_size_max(virtio, i);
		assert(max_size_r.e == OK);

		virtio_queue_info_ptr_result_t queue_info_r =
			virtio_get_queue_info_ptr(virtio, i);
		assert(queue_info_r.e == OK);

		queue_info_r.r->size = max_size_r.r;
	}
	rcu_read_finish();
}

error_t
virtio_pci_handle_virtio_ack_features_ok(virtio_t *virtio)
{
	// Not implemented yet.
	(void)virtio;
	return ERROR_UNIMPLEMENTED;
}

static void
virtio_pci_assert_irq(virtio_t *virtio, virtio_interrupt_t set_interrupt_status)
{
	virtio_pci_t *virtio_pci = &virtio->frontend_data.pci;

	virtio_interrupt_t old_interrupt_status = virtio_interrupt_atomic_union(
		&virtio_pci->interrupt_status, set_interrupt_status,
		memory_order_relaxed);
	bool raise_virq = !virtio_interrupt_is_empty(
		virtio_interrupt_difference(set_interrupt_status,
					    old_interrupt_status));
	if (raise_virq) {
		vpci_irq_assert(&virtio_pci->fn0);
	}
}

void
virtio_pci_handle_virtio_config_update_end(virtio_t *virtio)
{
	virtio_pci_t *virtio_pci = &virtio->frontend_data.pci;

	if (virtio_supports_per_queue_notify(virtio) &&
	    vpci_msix_is_enabled(&virtio_pci->msix_data)) {
		index_t vector =
			atomic_load_relaxed(&virtio_pci->config_msix_vector);
		(void)vpci_msix_send(&virtio_pci->fn0, &virtio_pci->msix_data,
				     vector);
	}

	virtio_interrupt_t interrupt = virtio_interrupt_default();
	virtio_interrupt_set_config_update(&interrupt, true);
	virtio_pci_assert_irq(virtio, interrupt);
}

error_t
virtio_pci_handle_virtio_queue_ready(virtio_t *virtio, index_t vq)
{
	error_t	      ret;
	virtio_pci_t *virtio_pci = &virtio->frontend_data.pci;

	if (virtio_supports_per_queue_notify(virtio) &&
	    vpci_msix_is_enabled(&virtio_pci->msix_data)) {
		index_result_t vq_r = nospec_range_check(vq, virtio->vqs);
		if (vq_r.e != OK) {
			ret = vq_r.e;
			goto out;
		}
		index_t vector = atomic_load_relaxed(
			&virtio_pci->queue_msix_vector[vq_r.r]);
		(void)vpci_msix_send(&virtio_pci->fn0, &virtio_pci->msix_data,
				     vector);
	} else {
		virtio_interrupt_t interrupt = virtio_interrupt_default();
		virtio_interrupt_set_queue_ready(&interrupt, true);
		virtio_pci_assert_irq(virtio, interrupt);
	}

	ret = OK;
out:
	return ret;
}

bool
virtio_pci_handle_vpci_irq_check_pending(vpci_function_t *function)
{
	assert(function != NULL);

	// Deassert frontend's IRQ when interrupt_status is zero, meaning no
	// interrupts are pending to be handled, or if MSI-X is enabled.
	virtio_pci_t *virtio_pci = virtio_pci_container_of_fn0(function);
	return !virtio_interrupt_is_empty(
		       atomic_load_relaxed(&virtio_pci->interrupt_status)) &&
	       !vpci_msix_is_enabled(&virtio_pci->msix_data);
}

#if defined(INTERFACE_VIRTIO_BACKEND)
error_t
virtio_pci_handle_vpci_attach(vpci_t *vpci, index_t *slot, cap_id_t device_cap)
{
	error_t	  ret;
	cspace_t *cspace = cspace_get_self();

	virtio_backend_ptr_result_t p = cspace_lookup_virtio_backend(
		cspace, device_cap, CAP_RIGHTS_VIRTIO_BACKEND_BIND_VPCI);
	if (compiler_unexpected(p.e != OK)) {
		ret = p.e;
		goto out;
	}
	virtio_backend_t *virtio_backend = p.r;

	if (virtio_backend->virtio.transport_type !=
	    VIRTIO_TRANSPORT_TYPE_PCI) {
		ret = ERROR_OBJECT_CONFIG;
		goto out_ref;
	}

	index_result_t slot_r = vpci_bind_device(
		vpci, *slot, &virtio_backend->virtio.frontend_data.pci.device);
	ret = slot_r.e;
	if (ret == OK) {
		*slot = slot_r.r;
	}

out_ref:
	object_put_virtio_backend(virtio_backend);
out:
	return ret;
}
#endif
