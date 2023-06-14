// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <atomic.h>
#include <scheduler.h>
#include <vcpu.h>

#include "event_handlers.h"
#include "gicv3.h"
#include "internal.h"

#if VGIC_HAS_LPI && GICV3_HAS_VLPI

error_t
vgic_handle_thread_context_switch_pre(void)
{
	thread_t *current = thread_get_self();

	if (current->vgic_vic != NULL) {
		bool expects_wakeup = vcpu_expects_wakeup(current);
		if (gicv3_vpe_deschedule(expects_wakeup)) {
			scheduler_lock_nopreempt(current);
			vcpu_wakeup(current);
			scheduler_unlock_nopreempt(current);
		}
	}

	return OK;
}

void
vgic_handle_thread_load_state_vpe(void)
{
	thread_t *current = thread_get_self();

	if (current->vgic_vic != NULL) {
		vgic_vpe_schedule_current();
	}
}

// Deschedule the vPE while blocked in EL2 / EL3.
//
// Note that vgic_vpe_schedule_current() is directly registered as both the
// unwinder for this event and the handler for vcpu_block_finish.
bool
vgic_handle_vcpu_block_start(void)
{
	bool wakeup = false;

	if (gicv3_vpe_deschedule(true)) {
		wakeup = true;
		vgic_vpe_schedule_current();
	}

	return wakeup;
}

void
vgic_vpe_schedule_current(void)
{
	thread_t *current = thread_get_self();
	assert(current->kind == THREAD_KIND_VCPU);

	assert(current->vgic_vic != NULL);

	// While it is not especially clear from the spec, it seems that
	// these two enable bits must be set specifically to the GICD_CTLR
	// enable bits, without being masked by the ICV bits.
	//
	// This is because GIC-700 has been observed dropping any
	// vSGI targeted to a disabled group on a scheduled vPE, and
	// might do so for vLPIs too. This is allowed for a group
	// disabled by GICD_CTLR, but not for a group disabled by
	// ICV_IGRPEN*.
	GICD_CTLR_DS_t gicd_ctlr =
		atomic_load_acquire(&current->vgic_vic->gicd_ctlr);
	gicv3_vpe_schedule(GICD_CTLR_DS_get_EnableGrp0(&gicd_ctlr),
			   GICD_CTLR_DS_get_EnableGrp1(&gicd_ctlr));
}

vcpu_trap_result_t
vgic_vpe_handle_vcpu_trap_wfi(void)
{
	// FIXME:
	return gicv3_vpe_check_wakeup(true) ? VCPU_TRAP_RESULT_RETRY
					    : VCPU_TRAP_RESULT_UNHANDLED;
}

bool
vgic_vpe_handle_vcpu_pending_wakeup(void)
{
	return gicv3_vpe_check_wakeup(false);
}

#endif // VGIC_HAS_LPI && GICV3_HAS_VLPI
