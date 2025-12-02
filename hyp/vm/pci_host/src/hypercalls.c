// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
// All Rights Reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcall_def.h>
#include <hypcontainers.h>
#include <hyprights.h>

#include "pci_host.h"

error_t
hypercall_pci_host_configure(cap_id_t pci_host_cap, cap_id_t aspace_cap,
			     cap_id_t cam_me_cap, vmaddr_t cam_base,
			     pci_host_option_flags_t options)
{
	(void)pci_host_cap;
	(void)aspace_cap;
	(void)cam_me_cap;
	(void)cam_base;
	(void)options;

	return ERROR_UNIMPLEMENTED;
}

error_t
hypercall_pci_host_add_aperture(cap_id_t pci_host_cap, cap_id_t mem_me_cap,
				vmaddr_t mem_base)
{
	(void)pci_host_cap;
	(void)mem_me_cap;
	(void)mem_base;

	return ERROR_UNIMPLEMENTED;
}

error_t
hypercall_pci_host_set_lockdown(cap_id_t		  pci_host_cap,
				pci_host_lockdown_state_t lockdown_state)
{
	(void)pci_host_cap;
	(void)lockdown_state;

	return ERROR_UNIMPLEMENTED;
}

error_t
hypercall_pci_function_configure(cap_id_t		     pci_function_cap,
				 cap_id_t		     pci_host_cap,
				 pci_responder_id_t	     responder_id,
				 pci_function_option_flags_t options)
{
	(void)pci_function_cap;
	(void)pci_host_cap;
	(void)responder_id;
	(void)options;

	return ERROR_UNIMPLEMENTED;
}

error_t
hypercall_pci_function_add_capability(cap_id_t pci_function_cap, size_t offset,
				      size_t			    size,
				      pci_capability_access_flags_t flags)
{
	(void)pci_function_cap;
	(void)offset;
	(void)size;
	(void)flags;

	return ERROR_UNIMPLEMENTED;
}

error_t
hypercall_pci_function_set_passthrough(cap_id_t pci_function_cap, bool enable)
{
	(void)pci_function_cap;
	(void)enable;

	return ERROR_UNIMPLEMENTED;
}
