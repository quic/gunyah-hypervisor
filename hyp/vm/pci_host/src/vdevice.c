// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
// All Rights Reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include "event_handlers.h"

vcpu_trap_result_t
pci_host_vdevice_access_cam(vdevice_t *vdevice, size_t offset,
			    size_t access_size, register_t *value,
			    bool is_write) REQUIRE_READ((rcu_read))
{
	(void)vdevice;
	(void)offset;
	(void)access_size;
	(void)value;
	(void)is_write;

	return VCPU_TRAP_RESULT_UNHANDLED;
}

vcpu_trap_result_t
pci_host_vdevice_access_mem_aperture(vdevice_t *vdevice, size_t offset,
				     size_t access_size, register_t *value,
				     bool is_write)
{
	(void)vdevice;
	(void)offset;
	(void)access_size;
	(void)value;
	(void)is_write;

	return VCPU_TRAP_RESULT_UNHANDLED;
}
