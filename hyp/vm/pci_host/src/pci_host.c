// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
// All Rights Reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcall_def.h>
#include <hypcontainers.h>
#include <hyprights.h>

#include <events/pci_host.h>

#include "event_handlers.h"
#include "pci_host.h"

error_t
pci_host_handle_object_create_pci_host(pci_host_create_t pci_host_create)
{
	// Initialize the sub objects in pci_host
	(void)pci_host_create;
	return OK;
}

error_t
pci_host_handle_object_activate_pci_host(pci_host_t *pci_host)
{
	error_t err = OK;

	assert(pci_host != NULL);

	return err;
}

void
pci_host_handle_object_deactivate_pci_host(pci_host_t *pci_host)
{
	(void)pci_host;
}

void
pci_host_handle_object_cleanup_pci_host(pci_host_t *pci_host)
{
	(void)pci_host;
}
