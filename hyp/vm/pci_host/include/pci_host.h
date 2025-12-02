// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
// All Rights Reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Internal pci_host module functions that are called by pci_host

bool
pci_host_search_function(pci_host_t	      *pci_host,
			 const pci_function_t *pci_function);

error_t
pci_host_function_prepare(uintptr_t ecam_base, size_t ecam_size,
			  pci_function_t *pci_function);

memextent_t *
pci_host_search_aperture(pci_host_t *pci_host, paddr_t addr);
