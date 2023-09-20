// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <atomic.h>
#include <compiler.h>
#include <cpulocal.h>
#include <hyp_aspace.h>
#include <log.h>
#include <panic.h>
#include <partition.h>
#include <platform_timer.h>
#include <trace.h>
#include <vet.h>

#include <asm/barrier.h>
#include <asm/sysregs.h>

#include "ete.h"
#include "ete_save_restore.h"
#include "event_handlers.h"

CPULOCAL_DECLARE_STATIC(ete_context_t, ete_contexts);
CPULOCAL_DECLARE_STATIC(uint64_t, ete_claim_tag);

void
ete_handle_boot_cpu_cold_init(void)
{
	TRCIDR2_t trcidr2 = register_TRCIDR2_read();
	TRCIDR4_t trcidr4 = register_TRCIDR4_read();
	TRCIDR5_t trcidr5 = register_TRCIDR5_read();

	(void)trcidr2;
	assert(TRCIDR2_get_CIDSIZE(&trcidr2) == TRCIDR2_CIDSIZE);
	assert(TRCIDR2_get_VMIDSIZE(&trcidr2) == TRCIDR2_VMIDSIZE);

	(void)trcidr4;
	assert(TRCIDR4_get_NUMPC(&trcidr4) == TRCIDR4_NUMPC);

	assert(TRCIDR4_get_NUMRSPAIR(&trcidr4) == TRCIDR4_NUMRSPAIR);
	assert(TRCIDR4_get_NUMACPAIRS(&trcidr4) == TRCIDR4_NUMACPAIRS);

	assert(TRCIDR4_get_NUMSSCC(&trcidr4) == TRCIDR4_NUMSSCC);
	assert(TRCIDR4_get_NUMCIDC(&trcidr4) == TRCIDR4_NUMCIDC);
	assert(TRCIDR4_get_NUMVMIDC(&trcidr4) == TRCIDR4_NUMVMIDC);

	(void)trcidr5;
	assert(TRCIDR5_get_NUMSEQSTATE(&trcidr5) == TRCIDR5_NUMSEQSTATE);
	assert(TRCIDR5_get_NUMEXTINSEL(&trcidr5) == TRCIDR5_NUMEXTINSEL);
	assert(TRCIDR5_get_NUMCNTR(&trcidr5) == TRCIDR5_NUMCNTR);
}

static void
ete_wait_stable(bool wait_pmstable, bool wait_idle)
{
	bool pmstable = false;
	bool idle     = false;

	// wait 100us
	ticks_t start = platform_timer_get_current_ticks();
	ticks_t timeout =
		start + platform_timer_convert_ns_to_ticks(100U * 1000U);
	while (1) {
		TRCSTATR_t trcstatr =
			register_TRCSTATR_read_ordered(&vet_ordering);

		// should be a volatile read
		pmstable = TRCSTATR_get_pmstable(&trcstatr);
		idle	 = TRCSTATR_get_idle(&trcstatr);

		// compilated for exit condition:
		// * when pmstable and idle are true
		// * when idle are true and not check pmstable
		// * when pmstable are true and not check idle
		if ((pmstable && idle) || (idle && (!wait_pmstable)) ||
		    (pmstable && (!wait_idle))) {
			break;
		}

		if (platform_timer_get_current_ticks() > timeout) {
			TRACE_AND_LOG(ERROR, INFO,
				      "ETE: programmers model is not stable");
			break;
		}
	}
}

void
ete_save_context_percpu(cpu_index_t cpu, bool may_poweroff)
{
	// Synchronise the trace unit. EL2 trace is always prohibited, so
	// we don't need to prohibit trace first.
	__asm__ volatile("tsb csync" : "+m"(vet_ordering));

	ete_context_t *ctx = &CPULOCAL_BY_INDEX(ete_contexts, cpu);

	// Save and clear TRCPRGCTLR
	ctx->TRCPRGCTLR = register_TRCPRGCTLR_read_ordered(&vet_ordering);
	register_TRCPRGCTLR_write_ordered(0U, &vet_ordering);
	asm_context_sync_ordered(&vet_ordering);

	if (may_poweroff) {
		// Wait until the programming interface is stable
		ete_wait_stable(true, false);

		// Save the remaining registers
		ete_save_registers(ctx, &vet_ordering);
		CPULOCAL_BY_INDEX(ete_claim_tag, cpu) =
			register_TRCCLAIMCLR_read_ordered(&vet_ordering);

		// Wait until all writes to the trace buffer are complete
		ete_wait_stable(false, true);
	} else {
		// Wait until all writes to the trace buffer are complete
		ete_wait_stable(true, true);
	}
}

void
ete_restore_context_percpu(cpu_index_t cpu, bool was_poweroff)
{
	ete_context_t *ctx = &CPULOCAL_BY_INDEX(ete_contexts, cpu);

	if (was_poweroff) {
		// Restore all of the registers other than TRCPRGCTLR
		ete_restore_registers(ctx, &vet_ordering);
		asm_context_sync_ordered(&vet_ordering);
	}

	register_TRCPRGCTLR_write_ordered(ctx->TRCPRGCTLR, &vet_ordering);
	asm_context_sync_ordered(&vet_ordering);

	register_TRCCLAIMSET_write_ordered(
		CPULOCAL_BY_INDEX(ete_claim_tag, cpu), &vet_ordering);
}
