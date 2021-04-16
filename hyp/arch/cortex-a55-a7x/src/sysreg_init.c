// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <hypregisters.h>

#include "event_handlers.h"

void
arch_cortex_a55_a7x_handle_boot_cpu_warm_init(void)
{
	// ACTLR_EL2 controls EL1 access to some important features such as
	// CLUSTERPMU and power control registers, which can be exploited to
	// perform security/DoS attacks. Therefore, we deny all these accesses
	// by writing 0 to this register.
	ACTLR_EL2_t val = ACTLR_EL2_default();
	register_ACTLR_EL2_write(val);
}
