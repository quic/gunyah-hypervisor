// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(ARCH_ARM_FEAT_SPE)
#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>
#include <panic.h>

#include <asm/barrier.h>
#include <asm/system_registers.h>

#include "event_handlers.h"

// Initialize MDCR_EL2.E2PB to EL2 mode and disable EL2 SPE profiling to
// prevent spurious interrupts before VCPUs are activated.

void
arm_vm_spe_handle_boot_cpu_warm_init(void)
{
#if !defined(ARCH_ARM_FEAT_SPE_FnE)
	// Hardware must not have FEAT_SPE_FnE without compile-time support
	PMSIDR_EL1_t pmsidr = register_PMSIDR_EL1_read();
	if (PMSIDR_EL1_get_FnE(&pmsidr)) {
		panic("SPE CPU config invalid");
	}
#endif

	MDCR_EL2_t  mdcr_el2  = register_MDCR_EL2_read();
	PMSCR_EL2_t pmscr_el2 = PMSCR_EL2_default();

	MDCR_EL2_set_E2PB(&mdcr_el2, MDCR_EL2_E2PB_EL2);
	register_MDCR_EL2_write(mdcr_el2);

	PMSCR_EL2_set_E2SPE(&pmscr_el2, false);
	register_PMSCR_EL2_write_ordered(pmscr_el2, &asm_ordering);

#if defined(ARCH_ARM_FEAT_FGT) && defined(ARCH_ARM_FEAT_SPE_FnE)
	// Set nPMSNEVFR_EL1 once at boot time. PMSNEVFR_EL1 is context-switched
	// and trapped by MDCR_EL2.TPMS; these bits prevent spurious FGT traps.
	HDFGWTR_EL2_t hdfgwtr = register_HDFGWTR_EL2_read();
	HDFGWTR_EL2_set_nPMSNEVFR_EL1(&hdfgwtr, true);
	register_HDFGWTR_EL2_write(hdfgwtr);

	HDFGRTR_EL2_t hdfgrtr = register_HDFGRTR_EL2_read();
	HDFGRTR_EL2_set_nPMSNEVFR_EL1(&hdfgrtr, true);
	register_HDFGRTR_EL2_write(hdfgrtr);
#endif
}
#else
// Dummy declaration to avoid empty translation unit warning
typedef int arm_vm_spe_dummy_t;
#endif
