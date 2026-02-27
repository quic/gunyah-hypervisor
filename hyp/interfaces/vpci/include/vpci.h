// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

// Query whether a VPCI bus implements PCIe.
//
// If this returns true, every device on the bus must satisfy the requirements
// of a PCIe Root Complex Integrated Endpoint: implementing MSI and/or MSI-X,
// not requiring I/O BARs to operate, implementing memory BARs as prefetchable
// if possible, etc.
//
// Note that bridges other than the host bridge are not implemented, so a PCIe
// bus is always the Root Complex.
bool
vpci_is_pcie(vpci_t *vpci);

// Bind a VPCI device to a bus.
//
// Before calling this function, the caller must fill in those fields of the
// device structure that are defined in this interface (as opposed to the
// implementation module). This includes the contents of the vpci_function
// structure(s), and the capabilities list and bars array within. The items
// in the capabilities list must have the same storage lifetime as the
// device structure itself.
//
// The contents of the device structure must not be modified once this function
// has been called.
//
// If the specified slot index is -1, the bus will allocate a slot. Otherwise,
// the slot index must be an unused slot between 0 and 31 inclusive. In either
// case, the allocated slot index will be returned on success.
//
// Currently this can only be called while holding the activation lock for the
// VPCI object, and only if the object is not active yet.
index_result_t
vpci_bind_device(vpci_t *vpci, index_t slot_index, vpci_device_t *device)
	REQUIRE_LOCK(vpci -> header.lock);

// Unbind a VPCI device from a bus.
//
// This function must be called if the object containing the device is
// deactivated. The structure must not be reused until vpci_cleanup_device()
// has been called, which must only be done after an RCU grace period has
// elapsed.
void
vpci_unbind_device(vpci_device_t *device);

// Clean up an unbound VPCI device.
//
// This function must be called by the cleanup handler for the object containing
// the device. If the device has been unbound earlier, it must be called one RCU
// grace period after vpci_unbind_device().
//
// If the device structure will be reused, the reuse argument must be true.
void
vpci_cleanup_device(vpci_device_t *device, bool reuse);

// Obtain the legacy IRQ source for a specific slot and interrupt pin.
//
// If the specified interrupt pin is tied to some other pin instead of having
// its own source, this function returns ERROR_NORESOURCES. If the pin has no
// source object because there is no device in the slot it services, this
// function returns ERROR_IDLE.
//
// This function must be called from within an RCU critical section.
virq_source_ptr_result_t
vpci_get_irq_source(vpci_t *vpci, index_t slot_index,
		    pci_interrupt_pin_t interrupt_pin,
		    virq_trigger_t     *virq_trigger) REQUIRE_RCU_READ;

// Assert a PCI function's legacy IRQ source.
//
// This has similar behaviour to virq_assert(), but the edge_only argument is
// not available because PCI legacy IRQs are always level-triggered.
//
// There is no matching vpci_irq_clear() because the line might be shared by
// multiple functions, so it would have to trigger vpci_irq_check_pending on the
// other functions; we might as well wait for the vic to do that itself.
void
vpci_irq_assert(vpci_function_t *function);

// Dispatch an MSI from a PCI function.
//
// This function checks that the given PCI function is permitted to act as a
// bus master, calculates its device ID for the VIC, and forwards the MSI to the
// VIC. If the MSI write aborts, a failed PCI transaction from the function will
// be reported to the VM using PCI error handling mechanisms, if they are
// enabled; otherwise any failures will be silent.
//
// This is a low-level function that is intended to be called by the MSI-X and
// MSI helpers. It is exposed here for use by devices that implement MSI or
// MSI-X without the helpers, such as passthrough devices.
void
vpci_dispatch_msi(vpci_function_t *function, vmaddr_t mailbox,
		  uint32_t message);

// Report failure of a PCI transaction requested by the host.
//
// The mapping from error_t to PCIe and PCI errors is:
//
// - OK: No error reported
// - ERROR_UNIMPLEMENTED: PCIe Unsupported Transaction, PCI Master Abort
// - ERROR_ADDR_INVALID: PCIe Unsupported Transaction, PCI Master Abort
// - ERROR_FAILURE: PCIe Uncorrectable Internal Error (no PCI error reported)
// - ERROR_RETRY: PCIe Corrected Internal Error (no PCI error reported)
// - All others: PCIe Completer Abort, PCI Target Abort
//
// This is a special case of vpci_error_from_requester() where the requester is
// the bus bridge.
void
vpci_error(vpci_function_t *function, error_t error);

// Report failure of a PCI transaction from the host that received no response.
//
// This is a special case of vpci_error() where the function is the bus bridge
// and the error is ERROR_ADDR_INVALID.
void
vpci_bus_error(vpci_t *bus);

// Report a failed PCI transaction requested by a specified function.
//
// This is used if a simulated bus master transaction fails; for example, if an
// MSI-X write is made to the wrong address. In most cases the completer will be
// the bus bridge.
void
vpci_error_from_requester(vpci_function_t *completer,
			  vpci_function_t *requester, error_t error);

//
// Helpers for implementing MSI-X.
//

// Allocate and initialise the MSI-X tables before binding the device.
error_t
vpci_msix_init(partition_t *owner, vpci_msix_data_t *msix_data);

// Free the MSI-X tables after unbinding the device.
error_t
vpci_msix_free(partition_t *owner, vpci_msix_data_t *msix_data);

// Handle MSI-X capability reads and writes.
register_result_t
vpci_msix_config_read(vpci_msix_data_t *msix_data, size_t offset,
		      size_t access_size);

error_t
vpci_msix_config_write(vpci_function_t *function, vpci_msix_data_t *msix_data,
		       size_t offset, size_t access_size, register_t data);

// Handle MSI-X BAR reads and writes.
//
// MSI-X requires two structures accessible through BARs: the vector table, and
// the pending bit array (PBA). The device is responsible for determining the
// locations within the BARs of the table and PBA, and must adjust the offset
// accordingly before calling these functions.
//
// Regardless of where these structures are allocated, each must be in a BAR
// with access_type set to trapped. Also note that the PCI 3.0 spec requires
// that these are not I/O BARs, and recommends placing the table and PBA in
// separate 4KiB pages, preferably in two dedicated BARs.
//
// Writes to the PBA have undefined behaviour, so there is only a read handler.
register_result_t
vpci_msix_table_read(vpci_msix_data_t *msix_data, size_t offset,
		     size_t access_size);

error_t
vpci_msix_table_write(vpci_function_t *function, vpci_msix_data_t *msix_data,
		      size_t offset, size_t access_size, register_t data);

register_result_t
vpci_msix_pba_read(vpci_msix_data_t *msix_data, size_t offset,
		   size_t access_size);

// Send an interrupt to an MSI-X vector.
//
// If the specified MSI-X vector has a valid destination and is unmasked in both
// the MSI-X table and the message control register, and MSI-X delivery and bus
// master transactions are enabled for the function, this call will be forwarded
// to the interrupt controller.
//
// If the specified vector is masked in the MSI-X table or the message control
// register, or MSI-X delivery is disabled, the vector will be marked pending in
// the PBA.
//
// If the specified vector is unmasked but has an invalid destination, or if bus
// master transactions have not been enabled in the function's command register,
// the interrupt will be lost. In the former case, the function may report a
// PCI Master-Abort and/or PCIe Unsupported Transaction.
error_t
vpci_msix_send(vpci_function_t *function, vpci_msix_data_t *msix_data,
	       index_t vector_index);

// Cancel a pending MSI-X interrupt.
//
// This clears the vector's pending bit in the PBA, and therefore is only
// effective if the vector has been masked in the MSI-X table or the message
// control register at all times since it was sent, and therefore has not been
// passed to the interrupt controller or lost. It has no effect otherwise.
//
// Note that the PCI MSI-X specification requires this function to be called
// when the source of the interrupt is cleared, to support drivers that keep the
// vectors masked and poll the pending bits.
error_t
vpci_msix_cancel(vpci_function_t *function, vpci_msix_data_t *msix_data,
		 index_t vector_index);

// Check whether MSI-X is enabled.
//
// A function that implements MSI-X using the helpers must ensure that its
// legacy IRQ is not asserted if this function returns true. This means it must
// be called before calling vpci_irq_assert(), and also in the handler for
// vpci_irq_check_pending.
bool
vpci_msix_is_enabled(vpci_msix_data_t *msix_data);

//
// Helpers for implementing PCIe.
//

// Initialise the PCIe Capability Structure before binding the device.
error_t
vpci_pcie_init(partition_t *owner, vpci_function_t *function,
	       vpci_pcie_data_t *pcie_data);

// Clean up the PCIe Capability Structure after unbinding the device.
error_t
vpci_pcie_free(partition_t *owner, vpci_function_t *function,
	       vpci_pcie_data_t *pcie_data);

// Handle PCIe Capability Structure reads and writes.
register_result_t
vpci_pcie_config_read(vpci_pcie_data_t *pcie_data, size_t offset,
		      size_t access_size);

error_t
vpci_pcie_config_write(vpci_pcie_data_t *pcie_data, size_t offset,
		       size_t access_size, register_t data);

// Handle PCIe extended configuration space reads and writes. A device using the
// PCIe helpers should call these to handle config offsets >= 256.
register_result_t
vpci_pcie_ext_config_read(vpci_pcie_data_t *pcie_data, size_t offset,
			  size_t *access_size);

error_t
vpci_pcie_ext_config_write(vpci_pcie_data_t *pcie_data, size_t offset,
			   size_t *access_size, register_t data);

// Report a PCIe error on a function, if it is using the PCIe helpers.
//
// This does nothing if the function is not using the PCIe helpers; in that case
// the function must report PCIe errors on its own instead of relying on
// vpci_error*() to do so.
//
// See vpci_error() above for the mapping from error_t to PCIe error types.
void
vpci_pcie_error(vpci_function_t *reporter, error_t error);

// Handle Power Management Capability Structure reads
// Writes are ignored
register_result_t
vpci_pm_config_read(size_t offset, size_t access_size);
