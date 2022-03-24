// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <hypregisters.h>

#include <asm/barrier.h>

#include "event_handlers.h"

void
aarch64_handle_boot_runtime_init(void)
{
#if defined(ARCH_ARM_8_1_VHE)
	CPTR_EL2_E2H1_t cptr = CPTR_EL2_E2H1_default();
	register_CPTR_EL2_E2H1_write_ordered(cptr, &asm_ordering);
#else
	CPTR_EL2_E2H0_t cptr = CPTR_EL2_E2H0_default();
	register_CPTR_EL2_E2H0_write_ordered(cptr, &asm_ordering);
#endif
}
