// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>
#include <hypregisters.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <cpulocal.h>
#include <ipi.h>
#include <irq.h>
#include <log.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <platform_cpu.h>
#include <preempt.h>
#include <rcu.h>
#include <scheduler.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <trace_helpers.h>
#include <util.h>
#include <vcpu.h>
#include <virq.h>

#include <events/virq.h>

#include <asm/barrier.h>

#include "event_handlers.h"
#include "gich_lrs.h"
#include "gicv3.h"
#include "internal.h"

static hwirq_t *vgic_maintenance_hwirq;

static struct asm_ordering_dummy gich_lr_ordering;

// Set to 1 to boot enable the virtual GIC delivery tracepoints
#if defined(VERBOSE_TRACE) && VERBOSE_TRACE
#define DEBUG_VGIC_TRACES 1
#else
#define DEBUG_VGIC_TRACES 0
#endif

void
vgic_handle_boot_hypervisor_start(void)
{
#if !defined(NDEBUG) && DEBUG_VGIC_TRACES
	register_t flags = 0U;
	TRACE_SET_CLASS(flags, VGIC);
	trace_set_class_flags(flags);
#endif

	hwirq_create_t hwirq_args = {
		.irq	= PLATFORM_GICH_IRQ,
		.action = HWIRQ_ACTION_VGIC_MAINTENANCE,
	};
	hwirq_ptr_result_t hwirq_r =
		partition_allocate_hwirq(partition_get_private(), hwirq_args);
	if (hwirq_r.e != OK) {
		panic("Unable to register GICH HWIRQ");
	}
	vgic_maintenance_hwirq = hwirq_r.r;

	irq_enable_local(vgic_maintenance_hwirq);
}

void
vgic_handle_boot_cpu_warm_init(void)
{
	if (vgic_maintenance_hwirq != NULL) {
		irq_enable_local(vgic_maintenance_hwirq);
	}

	// Ensure that EL1 has SRE=1 set (this is hardwired to 1 on most ARMv8
	// platforms, but there's no harm in trying to set it anyway)
	ICC_SRE_EL1_t icc_sre = ICC_SRE_EL1_default();
	// Disable IRQ and FIQ bypass
	ICC_SRE_EL1_set_DIB(&icc_sre, true);
	ICC_SRE_EL1_set_DFB(&icc_sre, true);
	// Enable system register accesses
	ICC_SRE_EL1_set_SRE(&icc_sre, true);
	register_ICC_SRE_EL1_write(icc_sre);
}

void
vgic_read_lr_state(index_t i)
{
	assert(i < CPU_GICH_LR_COUNT);
	thread_t *current = thread_get_self();
	assert((current != NULL) && (current->kind == THREAD_KIND_VCPU));

	vgic_lr_status_t *status = &current->vgic_lrs[i];

	// Read back the hardware register if necessary
	if (ICH_LR_EL2_base_get_State(&status->lr.base) !=
	    ICH_LR_EL2_STATE_INVALID) {
		status->lr = gicv3_read_ich_lr(i, &gich_lr_ordering);
	}
}

static void
vgic_write_lr(index_t i)
{
	assert(i < CPU_GICH_LR_COUNT);
	thread_t *current = thread_get_self();
	assert((current != NULL) && (current->kind == THREAD_KIND_VCPU));

	vgic_lr_status_t *status = &current->vgic_lrs[i];
	assert(status != NULL);

	gicv3_write_ich_lr(i, status->lr, &gich_lr_ordering);
}

// Update the activation state of a HW IRQ which is being delisted, manually
// deactivated, detached from a VIRQ, or replaced with a SW IRQ due to manual
// triggering.
//
// The caller is responsible for reading back the LR state from the ICH first.
//
// After this function returns, the HW IRQ is no longer marked as being listed.
static void
vgic_delist_hwirq_source(vic_t *vic, thread_t *vcpu, virq_source_t *source,
			 const vgic_lr_status_t *status)
{
	assert(ICH_LR_EL2_base_get_HW(&status->lr.base));
	assert(ICH_LR_EL2_base_get_State(&status->lr.base) !=
	       ICH_LR_EL2_STATE_PENDING_ACTIVE);
	assert((source == NULL) ||
	       (source->trigger == VIRQ_TRIGGER_VGIC_FORWARDED_SPI));

	if (compiler_unexpected(source == NULL)) {
		// The source has been detached. If it is still active in
		// hardware we need to deactivate it so it will be usable if it
		// is re-attached.
		hwirq_t *hwirq = irq_lookup_hwirq(
			ICH_LR_EL2_HW1_get_pINTID(&status->lr.hw));

		VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
			   "delist {:d}: detached hw {:d} (lr state {:d})",
			   ICH_LR_EL2_base_get_vINTID(&status->lr.base),
			   ICH_LR_EL2_HW1_get_pINTID(&status->lr.hw),
			   ICH_LR_EL2_base_get_State(&status->lr.base));
		vgic_hwirq_state_t old_hstate = atomic_exchange_explicit(
			&hwirq->vgic_state, VGIC_HWIRQ_STATE_INACTIVE,
			memory_order_acquire);

		// The IRQ should have been disabled when it was detached, but
		// that might have raced with a new delivery, so a redelivery at
		// this point is still a possibility.

		if (old_hstate == VGIC_HWIRQ_STATE_INACTIVE) {
			// Not active in hardware; there may have been a forced
			// reassert or a software assertion that put it in the
			// LRs. Nothing to do here.
		} else if ((old_hstate == VGIC_HWIRQ_STATE_ACTIVE) ||
			   (ICH_LR_EL2_base_get_State(&status->lr.base) !=
			    ICH_LR_EL2_STATE_INVALID)) {
			irq_deactivate(hwirq);
		}
	} else {
		hwirq_t *hwirq = hwirq_from_virq_source(source);

		if (ICH_LR_EL2_base_get_State(&status->lr.base) !=
		    ICH_LR_EL2_STATE_INVALID) {
			atomic_store_relaxed(&hwirq->vgic_state,
					     VGIC_HWIRQ_STATE_ACTIVE);
			VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
				   "delist {:d}: hw ? -> {:d} (lr state {:d})",
				   ICH_LR_EL2_base_get_vINTID(&status->lr.base),
				   VGIC_HWIRQ_STATE_ACTIVE,
				   ICH_LR_EL2_base_get_State(&status->lr.base));
		} else {
			vgic_hwirq_state_t old_hstate = VGIC_HWIRQ_STATE_LISTED;
			if (atomic_compare_exchange_strong_explicit(
				    &hwirq->vgic_state, &old_hstate,
				    VGIC_HWIRQ_STATE_INACTIVE,
				    memory_order_relaxed,
				    memory_order_relaxed)) {
				VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
					   "delist {:d}: hw {:d} -> {:d}",
					   ICH_LR_EL2_base_get_vINTID(
						   &status->lr.base),
					   old_hstate,
					   VGIC_HWIRQ_STATE_INACTIVE);
			} else {
				VGIC_TRACE(
					HWSTATE_CHANGED, vic, vcpu,
					"delist {:d}: hw {:d} (remote reassert?)",
					ICH_LR_EL2_base_get_vINTID(
						&status->lr.base),
					old_hstate);
			}
		}
	}
}

// Wrapper for vgic_delist_hwirq_source() that looks up the source.
static void
vgic_delist_hwirq(vic_t *vic, thread_t *vcpu, const vgic_lr_status_t *status)
{
	virq_source_t *hw_source = vgic_find_source(
		vic, vcpu, ICH_LR_EL2_base_get_vINTID(&status->lr.base));
	vgic_delist_hwirq_source(vic, vcpu, hw_source, status);
}

// Deactivate a hardware IRQ that is in unlisted active state.
//
// This function may be called at any time on an unlisted active hwirq to mark
// it inactive and deactivate it in hardware. This is used to cause the hardware
// to re-check the state of a level-triggered IRQ, so we don't need to do it
// ourselves by reading from the GICD. It is likely to have higher latency
// than reading the GICD, but does not block the hypervisor until the GICD
// responds, and should never give an inaccurate result.
//
// This function has no effect on a hwirq that is marked as listed or inactive.
static void
vgic_hwirq_trigger_reassert(vic_t *vic, thread_t *vcpu, hwirq_t *hwirq)
{
	vgic_hwirq_state_t old_hstate = VGIC_HWIRQ_STATE_ACTIVE;

	if (atomic_compare_exchange_strong_explicit(
		    &hwirq->vgic_state, &old_hstate, VGIC_HWIRQ_STATE_INACTIVE,
		    memory_order_acquire, memory_order_acquire)) {
		VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
			   "hwirq_trigger_reassert {:d} (phys {:d})",
			   hwirq->vgic_spi_source.virq, hwirq->irq);
		irq_deactivate(hwirq);
	}
}

// Clear pending bits from a given LR, and delist it if necessary.
//
// This is used when disabling, clearing the pending bit, rerouting, or
// releasing the source of a VIRQ. If the reclaim argument is true, it must be
// removed from the LR regardless of the pending state of the interrupt. If
// the hw_detach argument is true, the HW bit of the LR will be cleared even if
// it is left listed.
//
// The specified VCPU must either be the current thread, or LR-locked by
// the caller and known not to be running remotely. If the VCPU is the current
// thread, the caller is responsible for syncing and updating the physical LR.
//
// The pending flags in clear_dstate will be cleared in the delivery state.
// This value must not have any flags set other than the four pending flags and
// the enabled flag. For hardware interrupts, the level_src flag in clear_dstate
// will be ignored, and the actual pending state obtained from the HW delivery
// state.
//
// This function does not attempt to re-route the interrupt if it is still
// pending. If that is necessary, the caller must call vgic_deliver().
//
// The result is the updated delivery state.
static vgic_delivery_state_t
vgic_clear_pending_and_delist(vic_t *vic, thread_t *vcpu,
			      vgic_lr_status_t *    status,
			      vgic_delivery_state_t clear_dstate,
			      bool hw_detach, bool reclaim)
{
	assert(status->dstate != NULL);

	bool lr_pending = (ICH_LR_EL2_base_get_State(&status->lr.base) ==
			   ICH_LR_EL2_STATE_PENDING) ||
			  (ICH_LR_EL2_base_get_State(&status->lr.base) ==
			   ICH_LR_EL2_STATE_PENDING_ACTIVE);
	bool lr_active = (ICH_LR_EL2_base_get_State(&status->lr.base) ==
			  ICH_LR_EL2_STATE_ACTIVE) ||
			 (ICH_LR_EL2_base_get_State(&status->lr.base) ==
			  ICH_LR_EL2_STATE_PENDING_ACTIVE);

	vgic_delivery_state_t safe_clear_dstate = clear_dstate;
	virq_source_t *	      hw_source		= NULL;

	if (ICH_LR_EL2_base_get_HW(&status->lr.base)) {
		// Locate the source and update its HW IRQ state.
		hw_source = vgic_find_source(
			vic, vcpu,
			ICH_LR_EL2_base_get_vINTID(&status->lr.base));
		vgic_delist_hwirq_source(vic, vcpu, hw_source, status);

		// Always clear the level_src flag on HW IRQs. If this makes the
		// VIRQ leave its pending state and the LR was previously still
		// valid, we will deactivate the HW IRQ after the delivery state
		// is updated. This forces the HW to re-check the level pending
		// state for us and reassert it once it has been rerouted and
		// enabled.
		vgic_delivery_state_set_level_src(&safe_clear_dstate, true);

		// Clear the LR's HW bit if the source is being detached
		if (hw_detach || (hw_source == NULL)) {
			ICH_LR_EL2_base_set_HW(&status->lr.base, false);
			// If HW was 1 there must be no SW level assertion, so
			// we don't need to trap EOI
			ICH_LR_EL2_HW0_set_EOI(&status->lr.sw, false);
		}
	}

	vgic_delivery_state_t old_dstate = atomic_load_acquire(status->dstate);

	vgic_delivery_state_t new_dstate;
	bool		      pending;
	bool		      reassert_edge = lr_pending &&
			     !vgic_delivery_state_get_edge(&clear_dstate);
	do {
		assert(vgic_delivery_state_get_listed(&old_dstate));
		new_dstate = vgic_delivery_state_difference(old_dstate,
							    safe_clear_dstate);

		pending =
			vgic_delivery_state_get_cfg_is_edge(&new_dstate)
				? (vgic_delivery_state_get_edge(&new_dstate) ||
				   reassert_edge)
				: vgic_delivery_state_is_level_asserted(
					  &new_dstate);

		if (!lr_active && (reclaim || !pending)) {
			vgic_delivery_state_set_listed(&new_dstate, false);
			vgic_delivery_state_set_need_sync(&new_dstate, false);
			vgic_delivery_state_set_hw_detached(&new_dstate, false);
			vgic_delivery_state_set_active(&new_dstate, lr_active);
			if (reassert_edge) {
				vgic_delivery_state_set_edge(&new_dstate, true);
			}
		}
	} while (!atomic_compare_exchange_strong_explicit(
		status->dstate, &old_dstate, new_dstate, memory_order_release,
		memory_order_acquire));

	VGIC_TRACE(DSTATE_CHANGED, vic, vcpu,
		   "clr_pend_delist {:d}: {:#x} -> {:#x}",
		   ICH_LR_EL2_base_get_vINTID(&status->lr.base),
		   vgic_delivery_state_raw(old_dstate),
		   vgic_delivery_state_raw(new_dstate));

	if (!vgic_delivery_state_get_listed(&new_dstate)) {
		ICH_LR_EL2_base_set_State(&status->lr.base,
					  ICH_LR_EL2_STATE_INVALID);
		status->dstate = NULL;
		ICH_LR_EL2_base_set_HW(&status->lr.base, false);
		ICH_LR_EL2_HW0_set_EOI(&status->lr.sw, false);

		if (hw_source != NULL) {
			// If the VIRQ is HW-sourced, we delisted it, and the HW
			// IRQ is not in unlisted active state (either because
			// it was previously pending in the LR, or because
			// another CPU received it and failed to deliver it
			// in between our HWIRQ and VIRQ state updates), then
			// deactivate it so the hardware can decide
			// whether/where to reassert it.
			hwirq_t *hwirq = hwirq_from_virq_source(hw_source);

			vgic_hwirq_trigger_reassert(vic, vcpu, hwirq);
		}

	} else if (ICH_LR_EL2_base_get_HW(&status->lr.base)) {
		assert(pending || lr_active);
		assert(hw_source != NULL);
		// We're leaving the HW IRQ listed locally, so flip it back to
		// listed state.
		vgic_hwirq_state_t old_hstate;

		old_hstate = atomic_exchange_explicit(
			&hwirq_from_virq_source(hw_source)->vgic_state,
			VGIC_HWIRQ_STATE_LISTED, memory_order_relaxed);

		VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
			   "clr_pending {:d}: hw {:d} -> 2 (lr state {:d})",
			   ICH_LR_EL2_base_get_vINTID(&status->lr.base),
			   old_hstate,
			   ICH_LR_EL2_base_get_State(&status->lr.base));

		assert(old_hstate == VGIC_HWIRQ_STATE_ACTIVE);
	} else if (pending) {
		// No need to change the state in this case
	} else {
		assert(lr_active);
		ICH_LR_EL2_base_set_State(&status->lr.base,
					  ICH_LR_EL2_STATE_ACTIVE);
	}

	return new_dstate;
}

static void
vgic_route_and_flag(vic_t *vic, thread_t *vcpu, virq_t virq);

// Clear pending bits from a given VIRQ, and abort its delivery if necessary.
//
// This is used when disabling, rerouting, manually clearing, or releasing the
// source of a VIRQ. If we must remove the VIRQ's pending state from its LR even
// if it is still pending, the reclaim argument should be true. If it is
// necessary to check for a removed HW IRQ source, the hw_detach argument should
// be true.
//
// The specified VCPU is the current route of the VIRQ if it is shared (in which
// case it may be NULL), or the owner of the VIRQ if it is private.
//
// The pending flags in clear_dstate will be cleared in the delivery state.
// This value must not have any flags set other than the four pending flags and
// the enabled flag.
//
// If this function returns true, the interrupt is known not to have been listed
// anywhere at the time the pending flags were cleared. If it returns false, the
// interrupt may still be listed on remotely running VCPUs.
bool
vgic_undeliver(vic_t *vic, thread_t *vcpu,
	       _Atomic vgic_delivery_state_t *dstate, virq_t virq,
	       bool hw_detach, vgic_delivery_state_t clear_dstate, bool reclaim)
{
	bool from_self = vcpu == thread_get_self();

	cpu_index_t remote_cpu;
	if ((vcpu != NULL) && !from_self) {
		spinlock_acquire(&vcpu->vgic_lr_lock);
		remote_cpu = atomic_load_relaxed(&vcpu->vgic_lr_owner);
	} else {
		preempt_disable();
		remote_cpu = CPU_INDEX_INVALID;
	}

	vgic_delivery_state_t old_dstate = atomic_load_acquire(dstate);
	vgic_delivery_state_t new_dstate;

	// If the VIRQ is not listed, just update its flags.
	do {
		new_dstate = vgic_delivery_state_difference(old_dstate,
							    clear_dstate);
	} while (!vgic_delivery_state_get_listed(&old_dstate) &&
		 !atomic_compare_exchange_strong_explicit(
			 dstate, &old_dstate, new_dstate, memory_order_release,
			 memory_order_acquire));

	bool unlisted = false;
	if (!vgic_delivery_state_get_listed(&old_dstate)) {
		// Ensure the HW IRQ, if any, is inactive
		virq_source_t *source = vgic_find_source(vic, vcpu, virq);
		bool	       is_hw =
			(source != NULL) &&
			(source->trigger == VIRQ_TRIGGER_VGIC_FORWARDED_SPI);
		if (is_hw) {
			hwirq_t *hwirq = hwirq_from_virq_source(source);
			vgic_hwirq_trigger_reassert(vic, vcpu, hwirq);
		}

		unlisted = true;
		goto out;
	}

	// If the VCPU we were given is not running or is ourselves, try to
	// directly undeliver the VIRQ. This may fail for shared VIRQs if the
	// route is out of date.
	if ((vcpu != NULL) && !cpulocal_index_valid(remote_cpu)) {
		for (index_t i = 0; i < CPU_GICH_LR_COUNT; i++) {
			if (vcpu->vgic_lrs[i].dstate != dstate) {
				continue;
			}

			if (from_self) {
				vgic_read_lr_state(i);
			}

			new_dstate = vgic_clear_pending_and_delist(
				vic, vcpu, &vcpu->vgic_lrs[i], clear_dstate,
				hw_detach, reclaim);
			unlisted = !vgic_delivery_state_get_listed(&new_dstate);

			if (vgic_delivery_state_is_pending(&new_dstate) &&
			    vgic_delivery_state_get_enabled(&new_dstate)) {
				vgic_route_and_flag(vic, vcpu, virq);
			}

			if (from_self) {
				vgic_write_lr(i);
			}

			goto out;
		}
	}

	// Fall back to requesting a sync.
	//
	// Note that this can't clear the pending state of an edge triggered
	// interrupt, so in that case we log a warning.
#if !defined(NDEBUG)
	if (vgic_delivery_state_get_edge(&clear_dstate)) {
		static _Thread_local bool warned_about_ignored_icpendr = false;
		if (!warned_about_ignored_icpendr) {
			TRACE_AND_LOG(
				DEBUG, INFO,
				"vcpu {:#x}: trapped GIC[DR]_ICPENDR write "
				"was cross-CPU; vIRQ {:d} may be left pending",
				(uintptr_t)(thread_t *)thread_get_self(), virq);
			warned_about_ignored_icpendr = true;
		}
	}
#endif
	do {
		new_dstate = vgic_delivery_state_difference(old_dstate,
							    clear_dstate);

		if (!vgic_delivery_state_get_listed(&old_dstate)) {
			// Delisted by another thread; no sync needed.
		} else if (reclaim) {
			// Force a sync regardless of pending state.
			vgic_delivery_state_set_need_sync(&new_dstate, true);
		} else if (!vgic_delivery_state_get_enabled(&new_dstate)) {
			// No longer enabled; a sync is required.
			vgic_delivery_state_set_need_sync(&new_dstate, true);
		} else if (!vgic_delivery_state_get_cfg_is_edge(&new_dstate) &&
			   !vgic_delivery_state_is_level_asserted(
				   &new_dstate)) {
			// No longer pending; a sync is required.
			vgic_delivery_state_set_need_sync(&new_dstate, true);
		} else {
			// Still pending and not reclaimed; no sync needed.
		}

		if (hw_detach && vgic_delivery_state_get_listed(&old_dstate)) {
			vgic_delivery_state_set_hw_detached(&new_dstate, true);
		}
	} while (!atomic_compare_exchange_strong_explicit(
		dstate, &old_dstate, new_dstate, memory_order_release,
		memory_order_acquire));

	VGIC_TRACE(DSTATE_CHANGED, vic, vcpu, "undeliver {:d}: {:#x} -> {:#x}",
		   virq, vgic_delivery_state_raw(old_dstate),
		   vgic_delivery_state_raw(new_dstate));

	unlisted = !vgic_delivery_state_get_listed(&old_dstate);

out:
	if ((vcpu != NULL) && !from_self) {
		spinlock_release(&vcpu->vgic_lr_lock);
	} else {
		preempt_enable();
	}

	return unlisted;
}

// Return the configured priority of a specified VIRQ.
//
// Since we emulate a GICv3 supporting only one security state (GICD_CTLR.DS=1),
// there is no priority shifting for nonsecure accesses.
static uint8_t
vgic_get_priority(vic_t *vic, thread_t *vcpu, virq_t virq)
{
	uint8_t priority;
	switch (vgic_get_irq_type(virq)) {
	case VGIC_IRQ_TYPE_SPI:
		assert(vic != NULL);
		assert((virq >= GIC_SPI_BASE) &&
		       (virq < util_array_size(vic->gicd->ipriorityr)));
		priority = atomic_load_relaxed(&vic->gicd->ipriorityr[virq]);
		break;
	case VGIC_IRQ_TYPE_PPI:
	case VGIC_IRQ_TYPE_SGI:
		assert(vcpu != NULL);
		assert(virq < util_array_size(vcpu->vgic_gicr_sgi->ipriorityr));
		priority = atomic_load_relaxed(
			&vcpu->vgic_gicr_sgi->ipriorityr[virq]);
		break;
	case VGIC_IRQ_TYPE_RESERVED:
	default:
		// Invalid IRQ number
		priority = GIC_PRIORITY_LOWEST;
		break;
	}
	return priority;
}

static bool
vgic_redeliver_lr(vic_t *vic, thread_t *vcpu, virq_source_t *source,
		  _Atomic vgic_delivery_state_t *dstate,
		  vgic_delivery_state_t *	 old_dstate,
		  vgic_delivery_state_t assert_dstate, bool is_hw, index_t lr)
{
	bool need_wakeup;

	assert(lr < CPU_GICH_LR_COUNT);

	// Merge the old and new LR states.
	vgic_lr_status_t * status	  = &vcpu->vgic_lrs[lr];
	bool		   force_eoi_trap = false;
	ICH_LR_EL2_State_t old_state =
		ICH_LR_EL2_base_get_State(&status->lr.base);
	if (compiler_expected(old_state == ICH_LR_EL2_STATE_INVALID)) {
		// Previous interrupt is gone; take the new one. Don't
		// bother to recheck level triggering yet; that will be
		// done when this interrupt ends.
		vgic_hwirq_state_t old_hstate = VGIC_HWIRQ_STATE_ACTIVE;
		ICH_LR_EL2_base_set_HW(
			&status->lr.base,
			is_hw && atomic_compare_exchange_strong_explicit(
					 &hwirq_from_virq_source(source)
						  ->vgic_state,
					 &old_hstate, VGIC_HWIRQ_STATE_LISTED,
					 memory_order_relaxed,
					 memory_order_relaxed));
		if (ICH_LR_EL2_base_get_HW(&status->lr.base)) {
			VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
				   "redeliver {:d}: hw {:d} -> {:d}",
				   ICH_LR_EL2_base_get_vINTID(&status->lr.base),
				   old_hstate, VGIC_HWIRQ_STATE_LISTED);
			ICH_LR_EL2_HW1_set_pINTID(
				&status->lr.hw,
				hwirq_from_virq_source(source)->irq);
		} else if (is_hw) {
			// We have failed to update the HW state for some
			// reason, so we must list as SW.
			VGIC_TRACE(HWSTATE_UNCHANGED, vic, vcpu,
				   "redeliver {:d}: hw {:d}, listing as sw",
				   ICH_LR_EL2_base_get_vINTID(&status->lr.base),
				   old_hstate);
			assert(old_hstate == VGIC_HWIRQ_STATE_INACTIVE);
		} else {
			// SW IRQ, nothing to do here
		}
		ICH_LR_EL2_base_set_State(&status->lr.base,
					  ICH_LR_EL2_STATE_PENDING);

		// Interrupt is newly pending; we need to wake the VCPU.
		need_wakeup = true;
	} else if (compiler_unexpected(
			   is_hw != ICH_LR_EL2_base_get_HW(&status->lr.base))) {
		// If we have both a SW and a HW source, deliver the SW
		// assertion first, and request an EOI maintenance interrupt to
		// deliver (or trigger reassertion of) the HW source afterwards.

		// If this is a SW assertion, it can be missed here.
		if (ICH_LR_EL2_base_get_HW(&status->lr.base)) {
			VGIC_TRACE(
				HWSTATE_UNCHANGED, vic, vcpu,
				"redeliver {:d}: hw + sw; relisting as sw",
				ICH_LR_EL2_base_get_vINTID(&status->lr.base));

			// Note that we don't use the _source variant here
			// because the source we've provided may be NULL for a
			// software assertion.
			vgic_delist_hwirq(vic, vcpu, status);
			ICH_LR_EL2_base_set_HW(&status->lr.base, false);
		} else {
			VGIC_TRACE(
				HWSTATE_UNCHANGED, vic, vcpu,
				"redeliver {:d}: sw + hw; forcing eoi trap",
				ICH_LR_EL2_base_get_vINTID(&status->lr.base));
		}
		force_eoi_trap = true;

		// Interrupt is either already pending (so the VCPU should be
		// awake) or is active (so not deliverable, and the VCPU should
		// not be woken).
		need_wakeup = false;
	} else if (old_state == ICH_LR_EL2_STATE_ACTIVE) {
		// If the LR is not in hardware mode, we can set it pending +
		// active here. We should never get here for a hardware-mode
		// LR, since it would mean that we were risking a double
		// deactivate.

		// If this is a SW assertion, it can be assert here.
		assert(!is_hw && !ICH_LR_EL2_base_get_HW(&status->lr.base));
		ICH_LR_EL2_base_set_State(&status->lr.base,
					  ICH_LR_EL2_STATE_PENDING_ACTIVE);

		// Interrupt is active, so it is not currently deliverable.
		need_wakeup = false;
	} else {
		// We should never get here for a hardware-mode LR, since it
		// would mean that we were risking a double deactivate.

		// If this is a SW assertion, it can be assert here.
		assert(!is_hw && !ICH_LR_EL2_base_get_HW(&status->lr.base));
		VGIC_TRACE(HWSTATE_UNCHANGED, vic, vcpu,
			   "redeliver {:d}: redundant assertions merged",
			   ICH_LR_EL2_base_get_vINTID(&status->lr.base));

		// Interrupt is already pending, so the VCPU should be awake.
		need_wakeup = false;
	}

	VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
		   "redeliver {:d}: lr {:d} -> {:d}",
		   ICH_LR_EL2_base_get_vINTID(&status->lr.base), old_state,
		   ICH_LR_EL2_base_get_State(&status->lr.base));

	// Update the delivery state.
	vgic_delivery_state_t new_dstate;
	do {
		assert(vgic_delivery_state_get_listed(old_dstate));
		new_dstate =
			vgic_delivery_state_union(*old_dstate, assert_dstate);
		vgic_delivery_state_set_edge(&new_dstate, force_eoi_trap);
	} while (!atomic_compare_exchange_strong_explicit(
		dstate, old_dstate, new_dstate, memory_order_relaxed,
		memory_order_relaxed));

	VGIC_TRACE(DSTATE_CHANGED, vic, vcpu, "redeliver {:d}: {:#x} -> {:#x}",
		   ICH_LR_EL2_base_get_vINTID(&status->lr.base),
		   vgic_delivery_state_raw(*old_dstate),
		   vgic_delivery_state_raw(new_dstate));

	if (!ICH_LR_EL2_base_get_HW(&status->lr.base)) {
		ICH_LR_EL2_HW0_set_EOI(
			&status->lr.sw,
			force_eoi_trap ||
				(!vgic_delivery_state_get_cfg_is_edge(
					 &new_dstate) &&
				 vgic_delivery_state_is_level_asserted(
					 &new_dstate)));
	}

	return need_wakeup;
}

static bool_result_t
vgic_redeliver(vic_t *vic, thread_t *vcpu, virq_source_t *source,
	       _Atomic vgic_delivery_state_t *dstate,
	       vgic_delivery_state_t *	      old_dstate,
	       vgic_delivery_state_t assert_dstate, bool is_hw)
{
	const bool    to_self  = vcpu == thread_get_self();
	bool	      found_lr = false;
	index_t	      i;
	bool_result_t ret = bool_result_error(ERROR_BUSY);

	for (i = 0; i < CPU_GICH_LR_COUNT; i++) {
		if (dstate == vcpu->vgic_lrs[i].dstate) {
			found_lr = true;
			break;
		}
	}

	if (found_lr) {
		// If we are targeting ourselves, read the current state.
		if (to_self) {
			vgic_read_lr_state(i);
		}

		ret = bool_result_ok(
			vgic_redeliver_lr(vic, vcpu, source, dstate, old_dstate,
					  assert_dstate, is_hw, i));

		// Update the affected list register.
		if (to_self) {
			vgic_write_lr(i);
		}
	}

	return ret;
}

// Select an LR to deliver to, given the priority of the IRQ to deliver.
//
// The specified VCPU must either be the current thread, or LR-locked by
// the caller and known not to be running remotely.
//
// The caller must not assume that the selected LR is empty. Before using the
// LR it must check for and kick out any currently listed VIRQ, and update that
// VIRQ's state appropriately.
//
// On successful return, the value of *lr_priority is set to the priority of
// the pending interrupt listed in the selected LR, if any, or else to
// GIC_PRIORITY_LOWEST.
//
// The spec leaves it IMPLEMENTATION DEFINED whether priority decisions take the
// group bits and ICC group enable bits into account for directly routed
// interrupts (though 1-of-N interrupts, if supported must be delisted on ICC
// group disable, and all interrupts must be delisted on GICD group disable).
// See section 4.7.2 (page 64) in revision E. To keep this function simpler,
// we do not consider the ICC group enable bits.
static index_result_t
vgic_select_lr(thread_t *vcpu, uint8_t priority, uint8_t *lr_priority)
{
	bool	       to_self = vcpu == thread_get_self();
	index_result_t result  = index_result_error(ERROR_BUSY);

	// If delivery is disabled on this VCPU, don't allocate any new LRs.
	if (vcpu->vgic_sleep) {
		goto out;
	}

	// First look for an LR that has no associated IRQ at all.
	for (index_t i = 0; i < CPU_GICH_LR_COUNT; i++) {
		if (vcpu->vgic_lrs[i].dstate == NULL) {
			result	     = index_result_ok(i);
			*lr_priority = GIC_PRIORITY_LOWEST;
			goto out;
		}
	}

	// If the VCPU is the current thread, check for LRs that have become
	// empty since we last wrote to them; ELRSR is a hardware-generated
	// bitmap of these.
	if (to_self) {
		asm_context_sync_ordered(&gich_lr_ordering);
		register_t elrsr =
			register_ICH_ELRSR_EL2_read_ordered(&gich_lr_ordering);
		if (elrsr != 0U) {
			result	     = index_result_ok(compiler_ctz(elrsr));
			*lr_priority = GIC_PRIORITY_LOWEST;
			goto out;
		}
	}

	// Finally, check all the LRs, looking for (in order of preference):
	// - an inactive LR with no EOI trap enabled, or
	// - the lowest-priority LR that is not currently in pending state, or
	// - the lowest-priority pending LR, if it has lower priority than the
	//   VIRQ we're delivering.
	index_result_t result_pending	       = index_result_error(ERROR_BUSY);
	uint8_t	       priority_result_active  = 0U;
	uint8_t	       priority_result_pending = 0U;

	for (index_t i = 0; i < CPU_GICH_LR_COUNT; i++) {
		const vgic_lr_status_t *status = &vcpu->vgic_lrs[i];
		uint8_t			this_priority =
			ICH_LR_EL2_base_get_Priority(&status->lr.base);

		if (to_self) {
			vgic_read_lr_state(i);
		}

		if ((ICH_LR_EL2_base_get_State(&status->lr.base) ==
		     ICH_LR_EL2_STATE_INVALID) &&
		    (ICH_LR_EL2_base_get_HW(&status->lr.base) ||
		     !ICH_LR_EL2_HW0_get_EOI(&status->lr.sw))) {
			// LR is inactive; use it immediately
			result	     = index_result_ok(i);
			*lr_priority = GIC_PRIORITY_LOWEST;
			goto out;
		} else if (ICH_LR_EL2_base_get_State(&status->lr.base) !=
			   ICH_LR_EL2_STATE_PENDING) {
			if (this_priority >= priority_result_active) {
				result		       = index_result_ok(i);
				*lr_priority	       = GIC_PRIORITY_LOWEST;
				priority_result_active = this_priority;
			}
		} else {
			// pending, pending+active, or inactive + EOI
			// trapped
			if ((this_priority >= priority_result_pending) &&
			    (this_priority > priority)) {
				result_pending		= index_result_ok(i);
				priority_result_pending = this_priority;
			}
		}
	}

	if (priority_result_active == 0U) {
		// There were no active LRs; use the lowest-priority pending
		// one, if possible. Otherwise we have failed to find an LR.
		result = result_pending;
		if (result.e == OK) {
			*lr_priority = priority_result_pending;
		}
	}

out:
	return result;
}

// The number of VIRQs in each low (SPI + PPI) range.
#define VGIC_LOW_RANGE_SIZE                                                    \
	(count_t)((GIC_SPI_BASE + GIC_SPI_NUM + VGIC_LOW_RANGES - 1) /         \
		  VGIC_LOW_RANGES)
static_assert(util_is_p2(VGIC_LOW_RANGE_SIZE),
	      "VGIC search ranges must have power-of-two sizes");

// Mark an unlisted interrupt as pending on a VCPU.
//
// This is called when an interrupt is pending on a VCPU but cannot be listed
// immediately, either because there are no free LRs and none of the occupied
// LRs have lower pending priority, or because the VCPU is running remotely.
//
// This function requires the targeted VCPU's LR lock to be held, and the remote
// CPU (if any) on which the VCPU is currently running to be specified. If the
// VCPU is not locked (e.g. because another VCPU is already locked), use
// vgic_flag_unlocked() instead.
static void
vgic_flag_locked(virq_t virq, thread_t *vcpu, uint8_t priority,
		 cpu_index_t remote_cpu)
{
	assert_preempt_disabled();

	count_t priority_shifted = priority >> VGIC_PRIO_SHIFT;

	bitmap_atomic_set(vcpu->vgic_search_ranges_low[priority_shifted],
			  virq / VGIC_LOW_RANGE_SIZE, memory_order_release);

	bitmap_atomic_set(vcpu->vgic_search_prios, priority_shifted,
			  memory_order_release);

	if (vcpu->vgic_sleep) {
		// VCPU's GICR is asleep; nothing more to do.
	} else if (thread_get_self() == vcpu) {
		// We know that all LRs are occupied and not lower priority,
		// so sending an IPI here is not useful; enable NPIE instead
		if (!ICH_HCR_EL2_get_NPIE(&vcpu->vgic_ich_hcr)) {
			vcpu->vgic_ich_hcr = register_ICH_HCR_EL2_read();
			ICH_HCR_EL2_set_NPIE(&vcpu->vgic_ich_hcr, true);
			register_ICH_HCR_EL2_write(vcpu->vgic_ich_hcr);
		}
	} else if (cpulocal_index_valid(remote_cpu)) {
		ipi_one(IPI_REASON_VGIC_DELIVER, remote_cpu);
	} else {
		// NPIE being set will trigger a redeliver when switching
		ICH_HCR_EL2_set_NPIE(&vcpu->vgic_ich_hcr, true);
	}
}

// Mark an unlisted interrupt as pending on a VCPU.
//
// This is called when an interrupt is pending on a VCPU but cannot be listed
// immediately, either because:
//
// - another aperation is already being performed on one of the VCPU's LRs and
//   an immediate delivery would recurse (which is prohibited by MISRA because
//   it might overflow the stack), or
//
// - the specified VCPU might be running remotely, and its LRs can't be locked
//   because another VCPU's LR lock is already held.
//
// This function must not assume that the targeted VCPU's LR lock is or is not
// held. It uses explicitly ordered accesses to ensure that the correct
// signalling is performed without having to acquire the LR lock.
static void
vgic_flag_unlocked(virq_t virq, thread_t *vcpu, uint8_t priority)
{
	count_t priority_shifted = priority >> VGIC_PRIO_SHIFT;

	if (!bitmap_atomic_test_and_set(
		    vcpu->vgic_search_ranges_low[priority_shifted],
		    virq / VGIC_LOW_RANGE_SIZE, memory_order_release)) {
		if (!bitmap_atomic_test_and_set(vcpu->vgic_search_prios,
						priority_shifted,
						memory_order_release)) {
			if (thread_get_self() == vcpu) {
				ipi_one_relaxed(IPI_REASON_VGIC_DELIVER,
						cpulocal_get_index());
				vcpu_wakeup_self();
			} else {
				// Match the seq_cst fences when the owner is
				// changed during the context switch.
				atomic_thread_fence(memory_order_seq_cst);

				cpu_index_t lr_owner = atomic_load_relaxed(
					&vcpu->vgic_lr_owner);

				if (cpulocal_index_valid(lr_owner)) {
					ipi_one(IPI_REASON_VGIC_DELIVER,
						lr_owner);
				} else {
					scheduler_lock(vcpu);
					vcpu_wakeup(vcpu);
					scheduler_unlock(vcpu);
				}
			}
		}
	}
}

// Choose a VCPU to receive an unlisted interrupt, and mark it pending.
//
// This is called when rerouting a pending interrupt after delisting it. This
// may occur in a few different cases which are not clearly distinguished by the
// VGIC's data structures:
//
// 1. a pending and delivered VIRQ is delisted by sync after being rerouted
// 2. a pending and delivered VIRQ is delisted by local delivery of a
//    higher-priority unlisted VIRQ
// 3. a pending and undelivered VIRQ (which was previously asserted remotely)
//    is delisted when its LR is chosen by another VIRQ prior to its sync being
//    handled
// 4. a pending 1-of-N routed VIRQ is undelivered by a VCPU group disable due
//    to a GICR_CTLR write or destruction of the VCPU
//
// In most of these cases, we need to check the current route register and
// priority register for the interrupt, and reroute it based on those values.
static void
vgic_route_and_flag(vic_t *vic, thread_t *vcpu, virq_t virq)
{
	thread_t *target = vgic_get_route_or_owner(vic, vcpu, virq);

	if (target != NULL) {
		uint8_t priority = vgic_get_priority(vic, target, virq);
		vgic_flag_unlocked(virq, target, priority);
	}
}

// Clear out a VIRQ from a specified LR and flag it to be delivered later.
//
// This is used when there are no empty LRs available to deliver an IRQ, but
// an LR is occupied by an IRQ that is either lower-priority, or already
// acknowledged, or (in the current thread) already deactivated. It is also used
// when tearing down a VCPU entirely, or disabling a group when 1-of-N routing
// is implemented (VGIC_HAS_1N); in these cases the reroute argument should be
// set to true to force rerouting, which is usually only done if the sync flag
// is set.
//
// The specified VCPU must either be the current thread, or LR-locked by the
// caller and known not to be running remotely. If the specified VCPU is the
// current thread, the caller must rewrite the LR after calling this function.
//
// The specified LR must be occupied. If it contains an active interrupt
// (regardless of its pending state), it must be the lowest-priority listed
// active interrupt on the VCPU, to ensure that the active_unlisted stack is
// correctly ordered.
void
vgic_defer(vic_t *vic, thread_t *vcpu, index_t lr, bool reroute)
{
	const bool	  from_self = vcpu == thread_get_self();
	vgic_lr_status_t *status    = &vcpu->vgic_lrs[lr];
	assert(status->dstate != NULL);

	if (from_self) {
		vgic_read_lr_state(lr);
	}

	bool lr_pending = (ICH_LR_EL2_base_get_State(&status->lr.base) ==
			   ICH_LR_EL2_STATE_PENDING) ||
			  (ICH_LR_EL2_base_get_State(&status->lr.base) ==
			   ICH_LR_EL2_STATE_PENDING_ACTIVE);
	bool lr_active = (ICH_LR_EL2_base_get_State(&status->lr.base) ==
			  ICH_LR_EL2_STATE_ACTIVE) ||
			 (ICH_LR_EL2_base_get_State(&status->lr.base) ==
			  ICH_LR_EL2_STATE_PENDING_ACTIVE);

	if (lr_active) {
		index_t i = vcpu->vgic_active_unlisted_count % VGIC_PRIORITIES;
		vcpu->vgic_active_unlisted[i] =
			ICH_LR_EL2_base_get_vINTID(&status->lr.base);
		vcpu->vgic_active_unlisted_count++;
	}

	virq_source_t *source = vgic_find_source(
		vic, vcpu, ICH_LR_EL2_base_get_vINTID(&status->lr.base));
	virq_source_t *hw_source = NULL;
	if (ICH_LR_EL2_base_get_HW(&status->lr.base)) {
		hw_source = source;
		vgic_delist_hwirq_source(vic, vcpu, source, status);
	}

	vgic_delivery_state_t old_dstate = atomic_load_acquire(status->dstate);
	vgic_delivery_state_t new_dstate;
	do {
		bool level_src = vgic_delivery_state_get_level_src(&old_dstate);
		assert((source != NULL) || !level_src);

		new_dstate = old_dstate;
		vgic_delivery_state_set_active(&new_dstate, lr_active);
		vgic_delivery_state_set_listed(&new_dstate, false);
		vgic_delivery_state_set_need_sync(&new_dstate, false);
		vgic_delivery_state_set_hw_detached(&new_dstate, false);
		if (lr_pending) {
			vgic_delivery_state_set_edge(&new_dstate, true);
		}

		if (ICH_LR_EL2_base_get_HW(&status->lr.base) &&
		    (!lr_pending ||
		     vgic_delivery_state_get_need_sync(&old_dstate))) {
			// If it's a hardware interrupt, and has either been
			// acknowledged locally or marked for sync, then clear
			// the pending bit. If it is level-triggered, this will
			// lead to a reassert after the dstate update.
			vgic_delivery_state_set_level_src(&new_dstate, false);
		} else if (!ICH_LR_EL2_base_get_HW(&status->lr.base) &&
			   level_src &&
			   !trigger_virq_check_pending_event(
				   source->trigger, source,
				   vgic_delivery_state_get_edge(&old_dstate))) {
			vgic_delivery_state_set_level_src(&new_dstate, false);
		}
	} while (!atomic_compare_exchange_strong_explicit(
		status->dstate, &old_dstate, new_dstate, memory_order_release,
		memory_order_acquire));

	VGIC_TRACE(DSTATE_CHANGED, vic, vcpu, "defer {:d}: {:#x} -> {:#x}",
		   ICH_LR_EL2_base_get_vINTID(&status->lr.base),
		   vgic_delivery_state_raw(old_dstate),
		   vgic_delivery_state_raw(new_dstate));

	// The LR is no longer in use; clear out the status structure.
	ICH_LR_EL2_base_set_State(&status->lr.base, ICH_LR_EL2_STATE_INVALID);
	status->dstate = NULL;
	ICH_LR_EL2_base_set_HW(&status->lr.base, false);
	ICH_LR_EL2_HW0_set_EOI(&status->lr.sw, false);

	// Determine how this IRQ will be delivered, if necessary.
	if (lr_active) {
		// Delivery is not possible yet; it will be done on deactivate.
	} else if (vgic_delivery_state_get_enabled(&new_dstate) &&
		   vgic_delivery_state_is_pending(&new_dstate)) {
		// Enabled and pending; flag for delivery as soon as possible.
		if (reroute || vgic_delivery_state_get_need_sync(&old_dstate)) {
			vgic_route_and_flag(
				vic, vcpu,
				ICH_LR_EL2_base_get_vINTID(&status->lr.base));
		} else {
			// Note: CPU_INDEX_INVALID because this VCPU is always
			// either current or not running.
			vgic_flag_locked(
				ICH_LR_EL2_base_get_vINTID(&status->lr.base),
				vcpu,
				ICH_LR_EL2_base_get_Priority(&status->lr.base),
				CPU_INDEX_INVALID);
		}
	} else if ((hw_source != NULL) &&
		   !vgic_delivery_state_is_pending(&new_dstate)) {
		// HW interrupt which is no longer pending, either because it
		// was handled or because we cleared its level_src bit to force
		// a reassertion. If it is active, deactivate it so the HW will
		// deliver it again.
		hwirq_t *hwirq = hwirq_from_virq_source(hw_source);

		vgic_hwirq_trigger_reassert(vic, vcpu, hwirq);
	}
}

static void
vgic_sync_vcpu(thread_t *vcpu, bool hw_access);

void
vgic_handle_scheduler_affinity_changed(thread_t *vcpu, bool *need_sync)
{
	vic_t *vic = vcpu->vgic_vic;
	if (vic != NULL) {
		*need_sync = true;
	}
}

// When changing a vcpu's affinity, we kick out all the LRs as a work around
// for a race where an invalid LR is delisted on the new cpu in parallel to the
// HW irq asserting on the previous cpu.
//
// A race still exists where an irq arrives on a remote CPU at the same
// time that affinity is changed, however it will be very unlikely in practise.
// A different fix is required to split HW and SW irq flags.
void
vgic_handle_scheduler_affinity_changed_sync(thread_t *vcpu)
{
#if VGIC_HAS_1N
#error Unimplemented
#endif
	vic_t *vic = vcpu->vgic_vic;
	if (vic != NULL) {
		const bool self = vcpu == thread_get_self();

		if (!self) {
			spinlock_acquire(&vcpu->vgic_lr_lock);
		} else {
			preempt_disable();
		}

		// Clear out all of the LRs.
		for (index_t i = 0U; i < CPU_GICH_LR_COUNT; i++) {
			if (vcpu->vgic_lrs[i].dstate != NULL) {
				vgic_defer(vic, vcpu, i, false);
				assert(vcpu->vgic_lrs[i].dstate == NULL);
			}
		}

		if (!self) {
			spinlock_release(&vcpu->vgic_lr_lock);
		} else {
			preempt_enable();
		}
	}
}

// Try to deliver a VIRQ to a specified target for a specified reason.
//
// The specified VCPU is the current route of the VIRQ if it is shared (in which
// case it may be NULL), or the owner of the VIRQ if it is private.
//
// The pending flags in assert_dstate will be asserted in the delivery state.
// This may be 0 if pending flags have already been set by the caller. This
// value must not have any flags set other than the four pending flags and the
// enabled flag.
//
// The is_hw flag should be set if the delivered interrupt _may_ have had a
// hardware source. It should be false only if the interrupt was definitely not
// hardware-sourced (e.g. on a write to ISPENDR or SGIR).
//
// If the level_src pending bit is being set or is_hw is true, the VIRQ
// source must be specified. Otherwise, the source may be NULL, even if a
// registered source exists for the VIRQ.
//
// The is_private flag should be set if the delivered interrupt cannot possibly
// be rerouted. This is used to reduce the set of VCPUs that receive IPIs when a
// currently listed interrupt is redelivered, e.g. on an SGI to a busy VCPU.
//
// If it is not possible to immediately list the VIRQ, the target's
// pending-check flags will be updated so it will find the VIRQ next time it
// goes looking for pending interrupts to assert.
//
// This function returns the previous delivery state.
vgic_delivery_state_t
vgic_deliver(virq_t virq, vic_t *vic, thread_t *vcpu, virq_source_t *source,
	     _Atomic vgic_delivery_state_t *dstate,
	     vgic_delivery_state_t assert_dstate, bool is_hw, bool is_private)
{
	bool to_self	   = vcpu == thread_get_self();
	bool need_wakeup   = true;
	bool need_sync_all = false;

	assert((source != NULL) ||
	       !vgic_delivery_state_get_level_src(&assert_dstate));
	assert((source == NULL) ||
	       (vgic_get_irq_type(source->virq) == VGIC_IRQ_TYPE_PPI) ||
	       (vgic_get_irq_type(source->virq) == VGIC_IRQ_TYPE_SPI));

	cpu_index_t remote_cpu;
	if ((vcpu != NULL) && !to_self) {
		spinlock_acquire(&vcpu->vgic_lr_lock);
		remote_cpu = atomic_load_relaxed(&vcpu->vgic_lr_owner);
	} else {
		preempt_disable();
		remote_cpu = CPU_INDEX_INVALID;
	}

	vgic_delivery_state_t old_dstate = atomic_load_acquire(dstate);
	vgic_delivery_state_t new_dstate =
		vgic_delivery_state_union(old_dstate, assert_dstate);

	if (vgic_delivery_state_get_listed(&old_dstate) &&
	    vgic_delivery_state_is_pending(&new_dstate) &&
	    vgic_delivery_state_get_enabled(&new_dstate) && (vcpu != NULL) &&
	    !cpulocal_index_valid(remote_cpu)) {
		// Fast path: try to reset the pending state in the LR. This can
		// fail if the LR is not found, e.g. because the routing has
		// changed. Note that this function updates dstate and sstate if
		// it succeeds, so we can skip the updates below.
		//
		// We only need to try this once, because the listed bit can't
		// be changed by anyone else while we're holding the LR lock.
		bool_result_t redeliver_wakeup =
			vgic_redeliver(vic, vcpu, source, dstate, &old_dstate,
				       assert_dstate, is_hw);
		if (redeliver_wakeup.e == OK) {
			need_wakeup = redeliver_wakeup.r;
			goto out_delivered;
		}
	}

	// Keep track of the LR allocated for delivery (if any) and the priority
	// of the VIRQ currently in it (if any).
	index_result_t lr_r = index_result_error(ERROR_BUSY);
	uint8_t	       priority;
	uint8_t	       lr_priority	= GIC_PRIORITY_LOWEST;
	uint8_t	       checked_priority = GIC_PRIORITY_LOWEST;
	bool	       pending;

	// Clarify for the static analyser that we have not allocated an LR yet
	// at this point.
	assert(lr_r.e != OK);

	if (cpulocal_index_valid(remote_cpu) && is_hw &&
	    vgic_delivery_state_get_edge(&assert_dstate)) {
#if VGIC_HAS_1N
#error Unimplemented
#endif
		// HW IRQ needs to be routed to the correct CPU
		hwirq_t *hwirq = hwirq_from_virq_source(source);
		irq_disable_nosync(hwirq);

		psci_mpidr_t mpidr = platform_cpu_index_to_mpidr(remote_cpu);

		GICD_IROUTER_t physical_router = GICD_IROUTER_default();
		GICD_IROUTER_set_IRM(&physical_router, false);
		GICD_IROUTER_set_Aff0(&physical_router,
				      psci_mpidr_get_Aff0(&mpidr));
		GICD_IROUTER_set_Aff1(&physical_router,
				      psci_mpidr_get_Aff1(&mpidr));
		GICD_IROUTER_set_Aff2(&physical_router,
				      psci_mpidr_get_Aff2(&mpidr));
		GICD_IROUTER_set_Aff3(&physical_router,
				      psci_mpidr_get_Aff3(&mpidr));

		gicv3_spi_set_route(hwirq->irq, physical_router);

		VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
			   "lazy reroute {:d}: to cpu {:d}", hwirq->irq,
			   remote_cpu);

		irq_enable(hwirq);
	}

	do {
		priority = vgic_get_priority(vic, vcpu, virq);
		new_dstate =
			vgic_delivery_state_union(old_dstate, assert_dstate);

		pending = vgic_delivery_state_is_pending(&new_dstate);
		if (!pending) {
			// Just update the delivery state.
			continue;
		}

		// If it's enabled, not already listed, and not active, try to
		// allocate an LR for it, unless we have already done so at a
		// priority no lower than the current one. Note that we can't
		// easily relist a delisted active interrupt, especially if the
		// VM's EOImode is 0, so we just don't try.
		if ((lr_r.e != OK) && (priority < checked_priority) &&
		    vgic_delivery_state_get_enabled(&new_dstate) &&
		    !vgic_delivery_state_get_listed(&old_dstate) &&
		    !vgic_delivery_state_get_active(&old_dstate) &&
		    (vcpu != NULL) && !cpulocal_index_valid(remote_cpu)) {
			lr_r = vgic_select_lr(vcpu, priority, &lr_priority);
			checked_priority = priority;
		}

		if (vgic_delivery_state_get_listed(&old_dstate)) {
			// Already listed; request a sync.
			vgic_delivery_state_set_need_sync(&new_dstate, true);
		} else if (vgic_delivery_state_get_enabled(&new_dstate) &&
			   (lr_r.e == OK) && (priority < lr_priority)) {
			// We're newly listing the IRQ.
			vgic_delivery_state_set_listed(&new_dstate, true);
			vgic_delivery_state_set_edge(&new_dstate, false);
		} else {
			// No LR could be allocated: we will fall back
			// to setting the pending flag below. Nothing to
			// do here other than setting the pending bits
			// (above).
		}
	} while (!atomic_compare_exchange_strong_explicit(
		dstate, &old_dstate, new_dstate, memory_order_release,
		memory_order_acquire));

	VGIC_TRACE(DSTATE_CHANGED, vic, vcpu, "deliver {:d}: {:#x} -> {:#x}",
		   virq, vgic_delivery_state_raw(old_dstate),
		   vgic_delivery_state_raw(new_dstate));

	if (!pending) {
		// Not pending; nothing more to do.
		need_wakeup = false;
	} else if (vgic_delivery_state_get_listed(&old_dstate)) {
		// IRQ is already listed remotely; send a sync IPI.
		assert(vgic_delivery_state_get_need_sync(&new_dstate));
		if (!is_private) {
			need_sync_all = true;
			need_wakeup   = false;
		} else if (cpulocal_index_valid(remote_cpu)) {
			ipi_one(IPI_REASON_VGIC_SYNC, remote_cpu);
		} else {
			TRACE(DEBUG, INFO,
			      "vgic sync after failed redeliver of {:#x}: dstate {:#x} -> {:#x}",
			      virq, vgic_delivery_state_raw(old_dstate),
			      vgic_delivery_state_raw(new_dstate));

			vgic_sync_vcpu(vcpu, to_self);
		}
	} else if (!vgic_delivery_state_get_enabled(&new_dstate)) {
		// Not enabled; nothing more to do.
		need_wakeup = false;
	} else if ((lr_r.e == OK) && (priority < lr_priority)) {
		// List the IRQ immediately.
		assert(vgic_delivery_state_get_listed(&new_dstate));

		vgic_lr_status_t *status = &vcpu->vgic_lrs[lr_r.r];
		if (status->dstate != NULL) {
			vgic_defer(vic, vcpu, lr_r.r, false);
			assert(status->dstate == NULL);
		}

		status->dstate		      = dstate;
		vgic_hwirq_state_t old_hstate = VGIC_HWIRQ_STATE_ACTIVE;
		ICH_LR_EL2_base_set_HW(
			&status->lr.base,
			is_hw && atomic_compare_exchange_strong_explicit(
					 &hwirq_from_virq_source(source)
						  ->vgic_state,
					 &old_hstate, VGIC_HWIRQ_STATE_LISTED,
					 memory_order_relaxed,
					 memory_order_relaxed));
		if (ICH_LR_EL2_base_get_HW(&status->lr.base)) {
			VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
				   "deliver {:d}: hw {:d} -> {:d}", virq,
				   old_hstate, VGIC_HWIRQ_STATE_LISTED);
			ICH_LR_EL2_HW1_set_pINTID(
				&status->lr.hw,
				hwirq_from_virq_source(source)->irq);
		} else {
			if (is_hw) {
				// We must have raced with another CPU that
				// deactivated this IRQ when delisting it. The
				// deactivation has already been sent to the
				// hardware so we cannot just mark it active
				// again. We must list this as a SW IRQ.
				VGIC_TRACE(
					HWSTATE_UNCHANGED, vic, vcpu,
					"deliver {:d}: hw {:d}, listing as sw",
					virq, old_hstate);
				assert(old_hstate == VGIC_HWIRQ_STATE_INACTIVE);
			}
			ICH_LR_EL2_HW0_set_EOI(
				&status->lr.sw,
				!vgic_delivery_state_get_cfg_is_edge(
					&new_dstate) &&
					vgic_delivery_state_is_level_asserted(
						&new_dstate));
		}
		ICH_LR_EL2_base_set_vINTID(&status->lr.base, virq);
		ICH_LR_EL2_base_set_Priority(&status->lr.base, priority);
		ICH_LR_EL2_base_set_Group(
			&status->lr.base,
			vgic_delivery_state_get_group1(&new_dstate));
		ICH_LR_EL2_base_set_State(&status->lr.base,
					  ICH_LR_EL2_STATE_PENDING);

		if (to_self) {
			vgic_write_lr(lr_r.r);
		}
	} else if (vcpu != NULL) {
		// Can't immediately list; set the search flags in the target
		// VCPU so it finds this VIRQ next time it goes looking for
		// something to deliver. A delivery IPI is sent if the target is
		// currently running.
		vgic_flag_locked(virq, vcpu, priority, remote_cpu);
	} else {
		// VIRQ is unrouted. When it gains a route the pending bits in
		// the delivery state (which we set above) will be checked, so
		// there is nothing more to do.
		need_wakeup = false;
	}

out_delivered:
	if (to_self) {
		if (need_wakeup) {
			vcpu_wakeup_self();
		}
		preempt_enable();
	} else if (vcpu != NULL) {
		spinlock_release(&vcpu->vgic_lr_lock);
		if (need_wakeup) {
			scheduler_lock(vcpu);
			vcpu_wakeup(vcpu);
			scheduler_unlock(vcpu);
		}
	} else {
		// VIRQ is unrouted; there is nobody to wake.
		preempt_enable();
	}

	if (need_sync_all) {
		vgic_sync_all(vic, false);
	}

	return old_dstate;
}

void
vgic_sync_all(vic_t *vic, bool wakeup)
{
	rcu_read_start();

	for (index_t i = 0; i < vic->gicr_count; i++) {
		thread_t *vcpu = atomic_load_consume(&vic->gicr_vcpus[i]);
		if (thread_get_self() == vcpu) {
			vgic_sync_vcpu(vcpu, true);
			if (wakeup) {
				vcpu_wakeup_self();
			}
		} else if (vcpu != NULL) {
			spinlock_acquire(&vcpu->vgic_lr_lock);
			if (vcpu->vgic_sleep) {
				// Nothing should be listed on this CPU, so we
				// don't need to sync it.
			} else {
				cpu_index_t lr_owner = atomic_load_relaxed(
					&vcpu->vgic_lr_owner);
				if (cpulocal_index_valid(lr_owner)) {
					ipi_one(IPI_REASON_VGIC_SYNC, lr_owner);
				} else {
					vgic_sync_vcpu(vcpu, false);
				}
			}
			spinlock_release(&vcpu->vgic_lr_lock);
			if (wakeup) {
				scheduler_lock(vcpu);
				vcpu_wakeup(vcpu);
				scheduler_unlock(vcpu);
			}
		}
	}

	rcu_read_finish();
}

error_t
virq_clear(virq_source_t *source)
{
	error_t			       err;
	_Atomic vgic_delivery_state_t *dstate = NULL;

	// The source's VIC and VCPU pointers are RCU-protected.
	rcu_read_start();

	// We must have a VIC to clear from (note that a disconnected source is
	// always considered clear).
	vic_t *vic = atomic_load_acquire(&source->vic);
	if (compiler_unexpected(vic == NULL)) {
		err = ERROR_VIRQ_NOT_BOUND;
		goto out;
	}

	// Get the current target VCPU. It is possible that a shared VIRQ was
	// last delivered to some other VCPU, in which case an undeliver was
	// already started when we changed the route, and all we need to do is
	// send sync IPIs and wait for the VIRQ to sync (if strict). The VCPU
	// pointer may be NULL in that case.
	thread_t *vcpu = atomic_load_consume(&source->vgic_vcpu);
	if (vcpu == NULL) {
		if (source->is_private) {
			err = ERROR_VIRQ_NOT_BOUND;
			goto out;
		}
	}

	// At this point we can't fail.
	err = OK;

	// Clear the level_src bit in the delivery state.
	vgic_delivery_state_t clear_dstate = vgic_delivery_state_default();
	vgic_delivery_state_set_level_src(&clear_dstate, true);

	dstate = vgic_find_dstate(vic, vcpu, source->virq);
	(void)vgic_undeliver(vic, vcpu, dstate, source->virq, false,
			     clear_dstate, false);

out:
	rcu_read_finish();

	return err;
}

bool_result_t
virq_query(virq_source_t *source)
{
	bool_result_t result = bool_result_error(ERROR_VIRQ_NOT_BOUND);

	if (source == NULL) {
		goto out;
	}

	vic_t *vic = atomic_load_acquire(&source->vic);
	if (compiler_unexpected(vic == NULL)) {
		goto out;
	}

	thread_t *vcpu = atomic_load_consume(&source->vgic_vcpu);
	if (source->is_private && compiler_unexpected(vcpu == NULL)) {
		goto out;
	}

	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, vcpu, source->virq);
	assert(dstate != NULL);

	vgic_delivery_state_t cur_dstate = atomic_load_relaxed(dstate);
	result = bool_result_ok(vgic_delivery_state_get_level_src(&cur_dstate));
out:

	return result;
}

// Handle an EOI maintenance interrupt.
//
// These are enabled for all level-triggered interrupts with non-hardware
// sources; this includes registered VIRQ sources, ISPENDR writes, and SETSGI
// writes. They are also enabled when an interrupt with any triggering type is
// raised in software and hardware simultaneously.
//
// The specified VCPU must be the current thread. The specified LR must be in
// the invalid state in hardware, but have a software-asserted VIRQ associated
// with it.
static void
vgic_handle_eoi_lr(vic_t *vic, thread_t *vcpu, index_t lr)
{
	assert(thread_get_self() == vcpu);
	assert(lr < CPU_GICH_LR_COUNT);

	vgic_lr_status_t *status = &vcpu->vgic_lrs[lr];
	virq_source_t *	  source = vgic_find_source(
		   vic, vcpu, ICH_LR_EL2_base_get_vINTID(&status->lr.base));

	// The specified LR should have a software delivery listed in it
	assert(status->dstate != NULL);
	assert(!ICH_LR_EL2_base_get_HW(&status->lr.base));

	vgic_delivery_state_t old_dstate = atomic_load_acquire(status->dstate);

	bool is_hw = (source != NULL) &&
		     (source->trigger == VIRQ_TRIGGER_VGIC_FORWARDED_SPI);

	vgic_delivery_state_t new_dstate;
	do {
		assert(vgic_delivery_state_get_listed(&old_dstate));
		bool level_src = vgic_delivery_state_get_level_src(&old_dstate);
		assert((source != NULL) || !level_src);

		new_dstate = old_dstate;

		if (compiler_unexpected(is_hw)) {
			// Always clear the level_src bit here for a HW IRQ.
			// If it is level-triggered, this will cause it to be
			// delisted and then deactivated in HW; if it is still
			// pending in HW it will be reasserted at that point.
			// This has no effect if the IRQ is edge-triggered.
			vgic_delivery_state_set_level_src(&new_dstate, false);
		} else if ((source != NULL) && level_src &&
			   !trigger_virq_check_pending_event(
				   source->trigger, source,
				   vgic_delivery_state_get_edge(&new_dstate))) {
			vgic_delivery_state_set_level_src(&new_dstate, false);
		}

		if (vgic_delivery_state_is_pending(&new_dstate) &&
		    vgic_delivery_state_get_enabled(&new_dstate)) {
			vgic_delivery_state_set_edge(&new_dstate, false);
		} else {
			vgic_delivery_state_set_active(&new_dstate, false);
			vgic_delivery_state_set_listed(&new_dstate, false);
			vgic_delivery_state_set_need_sync(&new_dstate, false);
			vgic_delivery_state_set_hw_detached(&new_dstate, false);
		}
	} while (!atomic_compare_exchange_strong_explicit(
		status->dstate, &old_dstate, new_dstate, memory_order_release,
		memory_order_acquire));

	VGIC_TRACE(DSTATE_CHANGED, vic, vcpu, "eoi-lr {:d}: {:#x} -> {:#x}",
		   ICH_LR_EL2_base_get_vINTID(&status->lr.base),
		   vgic_delivery_state_raw(old_dstate),
		   vgic_delivery_state_raw(new_dstate));

	if (compiler_unexpected(vgic_delivery_state_get_listed(&new_dstate))) {
		ICH_LR_EL2_base_set_State(&status->lr.base,
					  ICH_LR_EL2_STATE_PENDING);
		vgic_hwirq_state_t old_hstate = VGIC_HWIRQ_STATE_ACTIVE;
		ICH_LR_EL2_base_set_HW(
			&status->lr.base,
			is_hw && atomic_compare_exchange_strong_explicit(
					 &hwirq_from_virq_source(source)
						  ->vgic_state,
					 &old_hstate, VGIC_HWIRQ_STATE_LISTED,
					 memory_order_relaxed,
					 memory_order_relaxed));
		if (ICH_LR_EL2_base_get_HW(&status->lr.base)) {
			VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
				   "eoi-lr {:d}: hw {:d} -> {:d}",
				   ICH_LR_EL2_base_get_vINTID(&status->lr.base),
				   old_hstate, VGIC_HWIRQ_STATE_LISTED);
			hwirq_t *hwirq = hwirq_from_virq_source(source);
			ICH_LR_EL2_HW1_set_pINTID(&status->lr.hw, hwirq->irq);
		} else {
			ICH_LR_EL2_HW0_set_EOI(
				&status->lr.sw,
				!vgic_delivery_state_get_cfg_is_edge(
					&new_dstate) &&
					vgic_delivery_state_is_level_asserted(
						&new_dstate));
		}

		vcpu_wakeup_self();
	} else {
		// LR is no longer occupied
		ICH_LR_EL2_base_set_State(&status->lr.base,
					  ICH_LR_EL2_STATE_INVALID);
		status->dstate = NULL;
		ICH_LR_EL2_base_set_HW(&status->lr.base, false);
		ICH_LR_EL2_HW0_set_EOI(&status->lr.sw, false);

		// If this is a hardware-sourced VIRQ and the HWIRQ is marked
		// active, deactivate it, allowing it to be delivered again.
		if (compiler_unexpected(is_hw)) {
			hwirq_t *hwirq = hwirq_from_virq_source(source);
			vgic_hwirq_trigger_reassert(vic, vcpu, hwirq);
		}
	}

	vgic_write_lr(lr);
}

// Handle a software deactivate of a specific VIRQ.
//
// This may be called by the DIR trap handler if the VM's EOImode is 1, by the
// LRENP maintenance interrupt handler if the VM's EOImode is 0, or by the
// ICACTIVER trap handler in either case.
//
// If the interrupt is listed, the specified VCPU must be the current VCPU, and
// the list register must be known to be in active or pending+active state. In
// this case, the set_edge parameter determines whether the edge bit will be set
// during deactivation.
//
// The specified old_dstate value must have been load-acquired before checking
// the listed bit to decide whether to call this function.
void
vgic_deactivate(vic_t *vic, thread_t *vcpu, virq_t virq,
		_Atomic vgic_delivery_state_t *dstate,
		vgic_delivery_state_t old_dstate, bool set_edge)
{
	bool local_listed = vgic_delivery_state_get_listed(&old_dstate);
	assert(!local_listed || (thread_get_self() == vcpu));

	// Find the registered source, if any.
	rcu_read_start();
	virq_source_t *source = vgic_find_source(vic, vcpu, virq);
	bool	       is_hw  = (source != NULL) &&
		     (source->trigger == VIRQ_TRIGGER_VGIC_FORWARDED_SPI);

	// Clear active in the delivery state, and level_src too if necessary.
	vgic_delivery_state_t new_dstate;
	do {
		assert((source != NULL) ||
		       !vgic_delivery_state_get_level_src(&old_dstate));

		new_dstate = old_dstate;

		if (local_listed) {
			// Nobody else should delist the IRQ from under us.
			assert(vgic_delivery_state_get_listed(&old_dstate));
			vgic_delivery_state_set_listed(&new_dstate, false);
			vgic_delivery_state_set_need_sync(&new_dstate, false);
			vgic_delivery_state_set_hw_detached(&new_dstate, false);
			if (set_edge) {
				vgic_delivery_state_set_edge(&new_dstate, true);
			}
		} else {
			if (vgic_delivery_state_get_listed(&old_dstate)) {
				// Somebody else has listed the interrupt
				// already. It must have been deactivated some
				// other way, e.g. by a previous ICACTIVE write,
				// so we have nothing to do here.
				goto out;
			}
			if (!vgic_delivery_state_get_active(&old_dstate)) {
				// Interrupt is already inactive; we have
				// nothing to do.
				goto out;
			}
			assert(!set_edge);
			vgic_delivery_state_set_active(&new_dstate, false);
		}

		// If the interrupt is marked source-pending, query the source
		// to see if this should still be the case.
		if (vgic_delivery_state_get_level_src(&old_dstate) &&
		    ((source == NULL) || is_hw ||
		     !trigger_virq_check_pending_event(
			     source->trigger, source,
			     vgic_delivery_state_get_edge(&new_dstate)))) {
			vgic_delivery_state_set_level_src(&new_dstate, false);
		}
	} while (!atomic_compare_exchange_strong_explicit(
		dstate, &old_dstate, new_dstate, memory_order_release,
		memory_order_acquire));

	VGIC_TRACE(DSTATE_CHANGED, vic, vcpu, "deactivate {:d}: {:#x} -> {:#x}",
		   virq, vgic_delivery_state_raw(old_dstate),
		   vgic_delivery_state_raw(new_dstate));

	// If the interrupt is hardware-sourced then forward the deactivation to
	// the hardware (unless someone else has done it already). Note that
	// this must occur _after_ the dstate change above, in case we get a
	// delivery immediately upon deactivate.
	if (is_hw) {
		hwirq_t *	   hwirq = hwirq_from_virq_source(source);
		vgic_hwirq_state_t old_hstate =
			local_listed ? VGIC_HWIRQ_STATE_LISTED
				     : VGIC_HWIRQ_STATE_ACTIVE;
		if (atomic_compare_exchange_strong_explicit(
			    &hwirq->vgic_state, &old_hstate,
			    VGIC_HWIRQ_STATE_INACTIVE, memory_order_relaxed,
			    memory_order_relaxed)) {
			VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
				   "deactivate {:d}: hw {:d} -> {:d}", virq,
				   old_hstate, VGIC_HWIRQ_STATE_INACTIVE);
			irq_deactivate(hwirq);
		}
	}

	// If the interrupt is still pending, deliver it immediately. Note that
	// this can't be HW=1, even if the interrupt we just deactivated was,
	// because the physical IRQ is inactive (above). It might be a software
	// delivery that occurred while the physical source was active.
	if (vgic_delivery_state_is_pending(&new_dstate) &&
	    vgic_delivery_state_get_enabled(&new_dstate)) {
		thread_t *new_target = vgic_get_route_or_owner(vic, vcpu, virq);
		if (new_target != NULL) {
			(void)vgic_deliver(virq, vic, new_target, source,
					   dstate,
					   vgic_delivery_state_default(), false,
					   !vgic_irq_is_spi(virq));
		}
	}

out:
	rcu_read_finish();
	(void)0;
}

static void
vgic_deactivate_unlisted(vic_t *vic, thread_t *vcpu, virq_t virq)
{
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, vcpu, virq);
	vgic_delivery_state_t old_dstate = atomic_load_acquire(dstate);
	if (vgic_delivery_state_get_listed(&old_dstate)) {
		// Somebody else must have deactivated it already, so ignore the
		// deactivate.
	} else {
		vgic_deactivate(vic, vcpu, virq, dstate, old_dstate, false);
	}
}

// Handle an unlisted EOI signalled by an LRENP maintenance interrupt.
//
// The specified VCPU must be the current thread.
static void
vgic_handle_unlisted_eoi(vic_t *vic, thread_t *vcpu)
{
	assert(thread_get_self() == vcpu);

	vcpu->vgic_active_unlisted_count--;
	index_t i    = vcpu->vgic_active_unlisted_count % VGIC_PRIORITIES;
	virq_t	virq = vcpu->vgic_active_unlisted[i];

	// The hardware has already dropped the active priority, based on the
	// assumption that the highest active priority belongs to this IRQ. All
	// we need to do is deactivate.
	vgic_deactivate_unlisted(vic, vcpu, virq);
}

// List the given VIRQ in the given LR if it is enabled, pending, routable to
// the given VCPU, not listed elsewhere, and has priority equal or higher
// (less) than the specified limit.
//
// The VCPU must be the current owner of the LRs on the calling CPU.
//
// The specified LR must be either already empty, or occupied by a VIRQ with
// priority strictly lower (greater) than the specified mask.
//
// This function returns true if the given VIRQ was listed.
static bool
vgic_list_if_pending(vic_t *vic, thread_t *vcpu, virq_t virq,
		     uint8_t priority_limit, index_t lr)
{
	bool	listed = false;
	uint8_t priority;

	// Find the delivery state.
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, vcpu, virq);

	virq_source_t *source = vgic_find_source(vic, vcpu, virq);
	bool	       is_hw  = (source != NULL) &&
		     (source->trigger == VIRQ_TRIGGER_VGIC_FORWARDED_SPI);

	vgic_delivery_state_t old_dstate = atomic_load_acquire(dstate);
	vgic_delivery_state_t new_dstate;
	do {
		if (!vgic_delivery_state_get_enabled(&old_dstate)) {
			goto out;
		}

		priority = vgic_get_priority(vic, vcpu, virq);
		if (priority > priority_limit) {
			goto out;
		}

		if (vgic_irq_is_spi(virq)) {
			GICD_IROUTER_t route = vgic_get_router(vic, virq);
			if (!GICD_IROUTER_get_IRM(&route)) {
				if ((GICD_IROUTER_get_Aff0(&route) !=
				     MPIDR_EL1_get_Aff0(
					     &vcpu->vcpu_regs_mpidr_el1)) ||
				    (GICD_IROUTER_get_Aff1(&route) !=
				     MPIDR_EL1_get_Aff1(
					     &vcpu->vcpu_regs_mpidr_el1)) ||
				    (GICD_IROUTER_get_Aff2(&route) !=
				     MPIDR_EL1_get_Aff2(
					     &vcpu->vcpu_regs_mpidr_el1)) ||
				    (GICD_IROUTER_get_Aff3(&route) !=
				     MPIDR_EL1_get_Aff3(
					     &vcpu->vcpu_regs_mpidr_el1))) {
					goto out;
				}
			}
		}

		if (vgic_delivery_state_get_listed(&old_dstate)) {
			// already listed elsewhere
			goto out;
		}
		if (!vgic_delivery_state_get_edge(&old_dstate) &&
		    !vgic_delivery_state_is_level_asserted(&old_dstate)) {
			// not currently pending
			goto out;
		}

		new_dstate = old_dstate;
		vgic_delivery_state_set_listed(&new_dstate, true);
		vgic_delivery_state_set_edge(&new_dstate, false);
	} while (!atomic_compare_exchange_strong_explicit(
		dstate, &old_dstate, new_dstate, memory_order_release,
		memory_order_acquire));

	VGIC_TRACE(DSTATE_CHANGED, vic, vcpu,
		   "list_if_pending {:d}: {:#x} -> {:#x}", virq,
		   vgic_delivery_state_raw(old_dstate),
		   vgic_delivery_state_raw(new_dstate));

	vgic_lr_status_t *status = &vcpu->vgic_lrs[lr];
	if (status->dstate != NULL) {
		vgic_defer(vic, vcpu, lr, false);
		assert(status->dstate == NULL);
	}

	if (is_hw) {
		hwirq_t *	   hwirq      = hwirq_from_virq_source(source);
		vgic_hwirq_state_t old_hstate = VGIC_HWIRQ_STATE_ACTIVE;
		is_hw = atomic_compare_exchange_strong_explicit(
			&hwirq->vgic_state, &old_hstate,
			VGIC_HWIRQ_STATE_LISTED, memory_order_relaxed,
			memory_order_relaxed);
		if (is_hw) {
			VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
				   "list-if-pending {:d}: hw {:d} -> {:d}",
				   virq, old_hstate, VGIC_HWIRQ_STATE_LISTED);
			ICH_LR_EL2_HW1_set_pINTID(&status->lr.hw, hwirq->irq);
		}
	}

	if (!is_hw) {
		ICH_LR_EL2_HW0_set_EOI(
			&status->lr.sw,
			!vgic_delivery_state_get_cfg_is_edge(&new_dstate) &&
				vgic_delivery_state_is_level_asserted(
					&new_dstate));
	}

	status->dstate = dstate;
	ICH_LR_EL2_base_set_HW(&status->lr.base, is_hw);
	ICH_LR_EL2_base_set_vINTID(&status->lr.base, virq);
	ICH_LR_EL2_base_set_Priority(&status->lr.base, priority);
	ICH_LR_EL2_base_set_Group(&status->lr.base,
				  vgic_delivery_state_get_group1(&new_dstate));
	ICH_LR_EL2_base_set_State(&status->lr.base, ICH_LR_EL2_STATE_PENDING);
	if (vcpu == thread_get_self()) {
		vgic_write_lr(lr);
	}
	listed = true;

out:
	return listed;
}

// Search for a pending VIRQ to list in the given LR; it must have priority
// strictly higher (less) than the specified mask.
//
// This is used to handle NP maintenance interrupts and delivery IPIs. The
// specified VCPU must be the current thread. The specified LR is either
// empty, or contains a VIRQ with priority equal or lower (greater) than
// the specified mask.
//
// This function returns true if a VIRQ was listed, and false otherwise.
static bool
vgic_find_pending_and_list(vic_t *vic, thread_t *vcpu, uint8_t priority_mask,
			   index_t lr)
{
	bool	listed = false;
	index_t prio_index;
	index_t prio_mask_index = priority_mask >> VGIC_PRIO_SHIFT;

	while (!listed && bitmap_atomic_ffs(vcpu->vgic_search_prios,
					    prio_mask_index, &prio_index)) {
		if (compiler_unexpected(!bitmap_atomic_test_and_clear(
			    vcpu->vgic_search_prios, prio_index,
			    memory_order_acquire))) {
			continue;
		}

		uint8_t priority = (uint8_t)(prio_index << VGIC_PRIO_SHIFT);
		index_t range;
#if !GICV3_HAS_VLPI && VGIC_HAS_LPI
#error lpi search ranges not implemented
#endif
		while (!listed &&
		       bitmap_atomic_ffs(
			       vcpu->vgic_search_ranges_low[prio_index],
			       VGIC_LOW_RANGES, &range)) {
			if (compiler_unexpected(!bitmap_atomic_test_and_clear(
				    vcpu->vgic_search_ranges_low[prio_index],
				    range, memory_order_acquire))) {
				continue;
			}

			for (index_t i = 0; i < VGIC_LOW_RANGE_SIZE; i++) {
				virq_t virq = (virq_t)(
					(range * VGIC_LOW_RANGE_SIZE) + i);

				if (vgic_list_if_pending(vic, vcpu, virq,
							 priority, lr)) {
					listed = true;
					break;
				}
			}

			// If we found a VIRQ in this range, then we (probably)
			// did not check the entire range, so we need to reset
			// the range's search bit in case there are more VIRQs.
			if (listed) {
				bitmap_atomic_set(
					vcpu->vgic_search_ranges_low[prio_index],
					range, memory_order_relaxed);
			}
		}

		// If we found a VIRQ at this priority, then we (probably) did
		// not check every range, so we need to reset the priority's
		// search bit in case there ore more VIRQs.
		if (listed) {
			bitmap_atomic_set(vcpu->vgic_search_prios, prio_index,
					  memory_order_relaxed);
		}
	}

	return listed;
}

bool
vgic_handle_irq_received_maintenance(void)
{
	assert_preempt_disabled();

	thread_t *vcpu = thread_get_self();
	vic_t *	  vic  = vcpu->vgic_vic;

	if ((vcpu->kind != THREAD_KIND_VCPU) || (vic == NULL)) {
		// Spurious IRQ; this can happen if a maintenance interrupt
		// is asserted shortly before a context switch, and the GICR
		// hasn't yet that it is no longer asserted by the time we
		// re-enable interrupts.
		//
		// If the context switch in question is to another VCPU, we
		// won't notice that the IRQ is spurious, but that doesn't do
		// any harm.
		goto out;
	}

	ICH_MISR_EL2_t misr = register_ICH_MISR_EL2_read();

#if VGIC_HAS_1N
#error Unhandled group enable / disable maintenance interrupts
#else
	// Only 1-of-N VIRQs need to be reclaimed on group disable, so we don't
	// need group enable / disable notifications if 1-of-N is unimplemented
	assert(!ICH_MISR_EL2_get_VGrp0D(&misr) &&
	       !ICH_MISR_EL2_get_VGrp1D(&misr) &&
	       !ICH_MISR_EL2_get_VGrp0E(&misr) &&
	       !ICH_MISR_EL2_get_VGrp1E(&misr));
#endif
	// The underflow interrupt is always disabled; we don't need it because
	// we never re-list delisted active interrupts
	assert(!ICH_MISR_EL2_get_U(&misr));

	if (ICH_MISR_EL2_get_EOI(&misr)) {
		register_t eisr = register_ICH_EISR_EL2_read();
		while (eisr != 0U) {
			index_t lr = compiler_ctz(eisr);
			eisr &= ~util_bit(lr);

			vgic_handle_eoi_lr(vic, vcpu, lr);
		}
	}

	if (ICH_MISR_EL2_get_LRENP(&misr)) {
		vcpu->vgic_ich_hcr = register_ICH_HCR_EL2_read();
		count_t eoicount =
			ICH_HCR_EL2_get_EOIcount(&vcpu->vgic_ich_hcr);

		for (count_t i = 0; i < eoicount; i++) {
			vgic_handle_unlisted_eoi(vic, vcpu);
		}

		ICH_HCR_EL2_set_EOIcount(&vcpu->vgic_ich_hcr, 0U);
		register_ICH_HCR_EL2_write(vcpu->vgic_ich_hcr);
	}

	// Always try to deliver more interrupts if the NP interrupt is enabled,
	// regardless of whether it is actually asserted. Note that NP may have
	// become asserted as a result of the EOI handling above, so we would
	// have to reread MISR to get the right value anyway.
	if (ICH_HCR_EL2_get_NPIE(&vcpu->vgic_ich_hcr) && !vcpu->vgic_sleep) {
		asm_context_sync_ordered(&gich_lr_ordering);
		register_t elrsr =
			register_ICH_ELRSR_EL2_read_ordered(&gich_lr_ordering);
		elrsr &= util_mask(CPU_GICH_LR_COUNT);

		// If no LRs are empty, find the lowest priority active one.
		if (elrsr == 0U) {
			uint8_t	       lr_priority = GIC_PRIORITY_LOWEST;
			index_result_t lr_r	   = vgic_select_lr(
				       vcpu, GIC_PRIORITY_LOWEST, &lr_priority);
			if (lr_r.e == OK) {
				assert(lr_priority == GIC_PRIORITY_LOWEST);
				elrsr = util_bit(lr_r.r);
			}
		}

		// Attempt to list in all empty LRs (or in the active one we
		// selected above), until we run out of pending IRQs.
		while (elrsr != 0U) {
			index_t lr = compiler_ctz(elrsr);
			elrsr &= ~util_bit(lr);

			if (vgic_find_pending_and_list(
				    vic, vcpu, GIC_PRIORITY_LOWEST, lr)) {
				vcpu_wakeup_self();
			} else {
				// Nothing left deliverable; clear NPIE.
				vcpu->vgic_ich_hcr =
					register_ICH_HCR_EL2_read();
				ICH_HCR_EL2_set_NPIE(&vcpu->vgic_ich_hcr,
						     false);
				register_ICH_HCR_EL2_write(vcpu->vgic_ich_hcr);
				break;
			}
		}
	}

out:
	return true;
}

// Synchronise the delivery state of a single occupied LR in the current thread
// with the VIRQ's GICD / GICR configuration.
//
// The given LR must have an assigned VIRQ, and the hardware state of the LR
// must already have been read into status->lr.
//
// This function returns true if the LR needs to be modified.
static bool
vgic_sync_one(vic_t *vic, thread_t *vcpu, index_t lr)
{
	assert(lr < CPU_GICH_LR_COUNT);
	vgic_lr_status_t *status = &vcpu->vgic_lrs[lr];
	assert(status->dstate != NULL);
	bool need_update = false;

	vgic_delivery_state_t old_dstate = atomic_load_acquire(status->dstate);
retry:
	(void)0;
	bool hw_detach = vgic_delivery_state_get_hw_detached(&old_dstate);
	if (hw_detach || vgic_delivery_state_get_need_sync(&old_dstate)) {
		bool reclaim   = false;
		bool must_flag = false;

		// Check that the VIRQ is still deliverable to this CPU
		if (!vgic_delivery_state_get_enabled(&old_dstate)) {
			// No longer enabled
			reclaim = true;
		} else if (vgic_irq_is_spi(ICH_LR_EL2_base_get_vINTID(
				   &status->lr.base))) {
			GICD_IROUTER_t route =
				vgic_get_router(vic, ICH_LR_EL2_base_get_vINTID(
							     &status->lr.base));
			if (!GICD_IROUTER_get_IRM(&route)) {
				if ((GICD_IROUTER_get_Aff0(&route) !=
				     MPIDR_EL1_get_Aff0(
					     &vcpu->vcpu_regs_mpidr_el1)) ||
				    (GICD_IROUTER_get_Aff1(&route) !=
				     MPIDR_EL1_get_Aff1(
					     &vcpu->vcpu_regs_mpidr_el1)) ||
				    (GICD_IROUTER_get_Aff2(&route) !=
				     MPIDR_EL1_get_Aff2(
					     &vcpu->vcpu_regs_mpidr_el1)) ||
				    (GICD_IROUTER_get_Aff3(&route) !=
				     MPIDR_EL1_get_Aff3(
					     &vcpu->vcpu_regs_mpidr_el1))) {
					// No longer routed here
					reclaim = true;
					// May be deliverable elsewhere
					must_flag = true;
				}
			}
		}

		if (!reclaim && !hw_detach &&
		    vgic_delivery_state_is_pending(&old_dstate)) {
			bool		      set_lr_pending = false;
			vgic_delivery_state_t new_dstate     = old_dstate;

			if (vgic_delivery_state_get_edge(&old_dstate)) {
				set_lr_pending = true;
				vgic_delivery_state_set_edge(&new_dstate,
							     false);
			}

			vgic_delivery_state_set_need_sync(&new_dstate, false);

			if (!atomic_compare_exchange_strong_explicit(
				    status->dstate, &old_dstate, new_dstate,
				    memory_order_release,
				    memory_order_acquire)) {
				goto retry;
			}
			VGIC_TRACE(DSTATE_CHANGED, vic, vcpu,
				   "sync-pending {:d}: {:#x} -> {:#x}",
				   ICH_LR_EL2_base_get_vINTID(&status->lr.base),
				   vgic_delivery_state_raw(old_dstate),
				   vgic_delivery_state_raw(new_dstate));

			if (set_lr_pending) {
				virq_source_t *source = vgic_find_source(
					vic, vcpu,
					ICH_LR_EL2_base_get_vINTID(
						&status->lr.base));
				bool is_hw = (source != NULL) &&
					     (source->trigger ==
					      VIRQ_TRIGGER_VGIC_FORWARDED_SPI);

				bool need_wakeup = vgic_redeliver_lr(
					vic, vcpu, source, status->dstate,
					&old_dstate,
					vgic_delivery_state_default(), is_hw,
					lr);
				if (need_wakeup) {
					scheduler_lock(vcpu);
					vcpu_wakeup(vcpu);
					scheduler_unlock(vcpu);
				}
				need_update = true;
			}
		} else {
			vgic_delivery_state_t new_dstate =
				vgic_clear_pending_and_delist(
					vic, vcpu, status,
					vgic_delivery_state_default(),
					hw_detach, reclaim);
			if (vgic_delivery_state_is_pending(&new_dstate)) {
				must_flag = vgic_delivery_state_get_enabled(
					&new_dstate);
			}
			need_update = true;
		}

		if (must_flag) {
			vgic_route_and_flag(
				vic, vcpu,
				ICH_LR_EL2_base_get_vINTID(&status->lr.base));
		}
	}

	return need_update;
}

static void
vgic_gicr_enter_sleep(vic_t *vic, thread_t *gicr_vcpu);

// Check all LRs for the need-sync flag and synchronise if necessary.
//
// This is called when a sync IPI is either received, or short-circuited during
// context switch; it is also called before blocking on a sync flag. In any case
// we need to check each listed VIRQ for the need-sync bit, and when it is
// found, re-check the deliverability of the VIRQ to the specified CPU (enabled,
// routed, etc).
//
// If the hw_access argument is true, the current LR states are read back from
// hardware, and updated in hardware if necessary. Otherwise they are assumed to
// be up to date already.
//
// The specified VCPU must either be the one that owns the LRs on the physical
// CPU (i.e. either current, or the previous thread in context_switch_post),
// or else be LR-locked and not running.
static void
vgic_sync_vcpu(thread_t *vcpu, bool hw_access)
{
	assert(vcpu != NULL);
	assert((thread_get_self() == vcpu) == hw_access);

	vic_t *vic = vcpu->vgic_vic;

	if (compiler_expected(vic != NULL)) {
		if (compiler_unexpected(vcpu->vgic_sleep)) {
			vgic_gicr_enter_sleep(vic, vcpu);
		} else {
			for (index_t i = 0; i < CPU_GICH_LR_COUNT; i++) {
				vgic_lr_status_t *status = &vcpu->vgic_lrs[i];
				if (status->dstate == NULL) {
					continue;
				}
				if (hw_access) {
					assert(thread_get_self() == vcpu);
					vgic_read_lr_state(i);
				}
				if (vgic_sync_one(vic, vcpu, i) && hw_access) {
					vgic_write_lr(i);
				}
			}
		}
	}
}

void
vgic_handle_thread_save_state(void)
{
	thread_t *vcpu = thread_get_self();
	assert(vcpu != NULL);
	vic_t *vic = vcpu->vgic_vic;

	if (vic != NULL) {
		for (index_t i = 0; i < CPU_GICH_LR_COUNT; i++) {
			vgic_lr_status_t *status = &vcpu->vgic_lrs[i];
			if (status->dstate == NULL) {
				continue;
			}
			vgic_read_lr_state(i);
		}

		gicv3_read_ich_aprs(vcpu->vgic_ap0rs, vcpu->vgic_ap1rs);
		vcpu->vgic_ich_hcr  = register_ICH_HCR_EL2_read();
		vcpu->vgic_ich_vmcr = register_ICH_VMCR_EL2_read();
	}
}

static bool
vgic_do_delivery_check(vic_t *vic, thread_t *vcpu)
{
	index_t prio_index_cutoff = VGIC_PRIORITIES;
	bool	delivered	  = false;

	while (!bitmap_atomic_empty(vcpu->vgic_search_prios,
				    prio_index_cutoff)) {
		uint8_t lowest_prio    = GIC_PRIORITY_HIGHEST;
		index_t lowest_prio_lr = 0U;

		for (index_t i = 0; i < CPU_GICH_LR_COUNT; i++) {
			vgic_lr_status_t *status = &vcpu->vgic_lrs[i];
			if ((status->dstate != NULL) &&
			    (ICH_LR_EL2_base_get_State(&status->lr.base) ==
			     ICH_LR_EL2_STATE_PENDING)) {
				uint8_t this_prio =
					ICH_LR_EL2_base_get_Priority(
						&status->lr.base);
				if (this_prio >= lowest_prio) {
					lowest_prio_lr = i;
					lowest_prio    = this_prio;
				}
			} else {
				lowest_prio_lr = i;
				lowest_prio    = GIC_PRIORITY_LOWEST;
				break;
			}
		}

		if (lowest_prio > GIC_PRIORITY_HIGHEST) {
			if (vgic_find_pending_and_list(vic, vcpu, lowest_prio,
						       lowest_prio_lr)) {
				delivered = true;
			} else {
				break;
			}
		}

		prio_index_cutoff = lowest_prio >> VGIC_PRIO_SHIFT;
	}

	ICH_HCR_EL2_set_NPIE(&vcpu->vgic_ich_hcr,
			     !bitmap_atomic_empty(vcpu->vgic_search_prios,
						  VGIC_PRIORITIES));

	return delivered;
}

static void
vgic_deliver_pending_sgi(vic_t *vic, thread_t *vcpu)
{
	index_t i;

	while (bitmap_atomic_ffs(vcpu->vgic_pending_sgis, GIC_SGI_NUM, &i)) {
		virq_t virq = (virq_t)i;
		bitmap_atomic_clear(vcpu->vgic_pending_sgis, i,
				    memory_order_relaxed);

		_Atomic vgic_delivery_state_t *dstate =
			&vcpu->vgic_private_states[virq];
		vgic_delivery_state_t assert_dstate =
			vgic_delivery_state_default();
		vgic_delivery_state_set_edge(&assert_dstate, true);

		(void)vgic_deliver(virq, vic, vcpu, NULL, dstate, assert_dstate,
				   false, true);
	}
}

void
vgic_handle_thread_context_switch_post(thread_t *prev)
{
	vic_t *vic = prev->vgic_vic;

	if (vic != NULL) {
		spinlock_acquire(&prev->vgic_lr_lock);
		if (ipi_clear(IPI_REASON_VGIC_SYNC)) {
			vgic_sync_vcpu(prev, false);
		}
		atomic_store_relaxed(&prev->vgic_lr_owner, CPU_INDEX_INVALID);

		// Any deliver or SGI IPIs are no longer relevant; discard them.
		(void)ipi_clear(IPI_REASON_VGIC_DELIVER);
		(void)ipi_clear(IPI_REASON_VGIC_SGI);

		if (vcpu_expects_wakeup(prev)) {
			// The prev thread could be woken by a pending IRQ;
			// check for any that are waiting to be delivered.
			//
			// Match the seq_cst fences in vgic_flag_unlocked and
			// vgic_icc_generate_sgi. This ensures that those
			// routines either update the pending states before the
			// fence so we will see them below, or else see the
			// invalid owner after the fence and send a wakeup
			// causing prev to be rescheduled.
			atomic_thread_fence(memory_order_seq_cst);

			spinlock_release(&prev->vgic_lr_lock);

			if (!prev->vgic_sleep) {
				spinlock_acquire(&prev->vgic_lr_lock);
				bool wakeup = vgic_do_delivery_check(vic, prev);
				spinlock_release(&prev->vgic_lr_lock);

				if (wakeup) {
					scheduler_lock(prev);
					vcpu_wakeup(prev);
					scheduler_unlock(prev);
				}
			}

			vgic_deliver_pending_sgi(vic, prev);
		} else {
			spinlock_release(&prev->vgic_lr_lock);
		}
	}
}

void
vgic_handle_thread_load_state(void)
{
	thread_t *vcpu = thread_get_self();
	assert(vcpu != NULL);
	vic_t *vic = vcpu->vgic_vic;

	if (vic != NULL) {
		spinlock_acquire(&vcpu->vgic_lr_lock);
		atomic_store_relaxed(&vcpu->vgic_lr_owner,
				     cpulocal_get_index());

		// Match the seq_cst fences in vgic_flag_unlocked and
		// vgic_icc_generate_sgi. This ensures that those routines
		// either see us as the new owner and send an IPI after
		// the fence, so we will see and handle it after the context
		// switch ends, or else write the pending IRQ state before
		// the fence, so it is seen by our checks below.
		atomic_thread_fence(memory_order_seq_cst);

		spinlock_release(&vcpu->vgic_lr_lock);

		for (index_t i = 0; i < CPU_GICH_LR_COUNT; i++) {
			vgic_write_lr(i);
		}

		if (!vcpu->vgic_sleep) {
			(void)vgic_do_delivery_check(vic, vcpu);
		}

		gicv3_write_ich_aprs(vcpu->vgic_ap0rs, vcpu->vgic_ap1rs);
		register_ICH_VMCR_EL2_write(vcpu->vgic_ich_vmcr);
		register_ICH_HCR_EL2_write(vcpu->vgic_ich_hcr);

		vgic_deliver_pending_sgi(vic, vcpu);
	} else {
		register_ICH_HCR_EL2_write(ICH_HCR_EL2_default());
	}
}

static void
vgic_gicr_enter_sleep(vic_t *vic, thread_t *gicr_vcpu)
{
	bool hw_access = thread_get_self() == gicr_vcpu;

	// Clear out all of the LRs.
	for (index_t i = 0U; i < CPU_GICH_LR_COUNT; i++) {
		if (gicr_vcpu->vgic_lrs[i].dstate != NULL) {
			vgic_defer(vic, gicr_vcpu, i, true);
			assert(gicr_vcpu->vgic_lrs[i].dstate == NULL);
			if (hw_access) {
				vgic_write_lr(i);
			}
		}
	}

	// Make sure NPIE is disabled.
	if (hw_access) {
		gicr_vcpu->vgic_ich_hcr = register_ICH_HCR_EL2_read();
	}
	ICH_HCR_EL2_set_NPIE(&gicr_vcpu->vgic_ich_hcr, false);
	if (hw_access) {
		register_ICH_HCR_EL2_write(gicr_vcpu->vgic_ich_hcr);
	}

	// Set the virtual WAKER to indicate sleep.
	GICR_WAKER_t waker = GICR_WAKER_default();
	GICR_WAKER_set_ProcessorSleep(&waker, true);
	GICR_WAKER_set_ChildrenAsleep(&waker, true);
	atomic_store_release(&gicr_vcpu->vgic_gicr_rd->waker, waker);
}

void
vgic_gicr_rd_set_wake(vic_t *vic, thread_t *gicr_vcpu, bool processor_sleep)
{
	spinlock_acquire(&gicr_vcpu->vgic_lr_lock);
	bool to_self	 = thread_get_self() == gicr_vcpu;
	bool need_wakeup = false;

	GICR_WAKER_t waker =
		atomic_load_relaxed(&gicr_vcpu->vgic_gicr_rd->waker);
	assert(GICR_WAKER_get_ProcessorSleep(&waker) == gicr_vcpu->vgic_sleep);

	if (GICR_WAKER_get_ProcessorSleep(&waker) == processor_sleep) {
		// Already in the requested state; nothing to do
	} else if (GICR_WAKER_get_ChildrenAsleep(&waker) == processor_sleep) {
		// Currently in transition; specified behaviour is
		// UNPREDICTABLE. We treat it as ignoring the write.
	} else if (processor_sleep) {
		// Entering sleep. Set the sleep flag to prevent new deliveries.
		gicr_vcpu->vgic_sleep = true;

		cpu_index_t lr_owner =
			atomic_load_relaxed(&gicr_vcpu->vgic_lr_owner);

		if (to_self || !cpulocal_index_valid(lr_owner)) {
			// Concurrent deliveries are not possible, so we can
			// enter sleep immediately.
			vgic_gicr_enter_sleep(vic, gicr_vcpu);
		} else {
			// Concurrent deliveries may be occurring on the
			// targeted thread, which is running remotely; we need
			// to wait until it finishes before setting
			// ChildrenAsleep.
			GICR_WAKER_set_ProcessorSleep(&waker, true);
			atomic_store_relaxed(&gicr_vcpu->vgic_gicr_rd->waker,
					     waker);

			// Send a sync IPI to the targeted thread. The handler
			// will complete the entry into sleep.
			ipi_one(IPI_REASON_VGIC_SYNC, lr_owner);
		}
	} else {
		// Leaving sleep. Clear the sleep flag to permit new deliveries.
		// This always succeeds immediately.
		gicr_vcpu->vgic_sleep = false;
		GICR_WAKER_set_ProcessorSleep(&waker, false);
		GICR_WAKER_set_ChildrenAsleep(&waker, false);
		atomic_store_relaxed(&gicr_vcpu->vgic_gicr_rd->waker, waker);

		// Read ICH_HCR_EL2 so we can safely update NPIE
		if (to_self) {
			gicr_vcpu->vgic_ich_hcr = register_ICH_HCR_EL2_read();
		}

		// Now search for and list all deliverable VIRQs.
		if (vgic_do_delivery_check(vic, gicr_vcpu)) {
			// If we're operating on some other thread (which is not
			// common), we may need to wake it from WFI
			if (compiler_unexpected(!to_self)) {
				need_wakeup = true;
			}
		}

		// Apply the updated NPIE bit
		if (to_self) {
			register_ICH_HCR_EL2_write(gicr_vcpu->vgic_ich_hcr);
		}
	}

	spinlock_release(&gicr_vcpu->vgic_lr_lock);

	if (need_wakeup && !to_self) {
		scheduler_lock(gicr_vcpu);
		vcpu_wakeup(gicr_vcpu);
		scheduler_unlock(gicr_vcpu);
	}
}

bool
vgic_handle_vcpu_pending_wakeup(void)
{
	thread_t *vcpu = thread_get_self();
	assert(vcpu != NULL);

	bool pending =
		!bitmap_atomic_empty(vcpu->vgic_search_prios, VGIC_PRIORITIES);

	if (!pending && !vcpu->vgic_sleep) {
		// If we're not in sleep state, there might be interrupts left
		// in the LRs. This could happen at a preemption point in a
		// long-running service call, or during a suspend call into a
		// retention state.
		for (index_t i = 0U; !pending && (i < CPU_GICH_LR_COUNT); i++) {
			vgic_read_lr_state(i);
			ICH_LR_EL2_State_t state = ICH_LR_EL2_base_get_State(
				&vcpu->vgic_lrs[i].lr.base);
			// Note: not checking for PENDING_ACTIVE here, because
			// that is not deliverable and can't wake the VCPU.
			if (state == ICH_LR_EL2_STATE_PENDING) {
				pending = true;
			}
		}
	}

	return pending;
}

void
vgic_handle_vcpu_poweredoff(void)
{
	thread_t *vcpu = thread_get_self();

	if (vcpu->vgic_vic != NULL) {
		// Put the GICR into sleep state. The guest really should have
		// done this already, but PSCI_CPU_OFF is not able to fail if it
		// hasn't, so we just go ahead and do it ourselves.
		vgic_gicr_rd_set_wake(vcpu->vgic_vic, vcpu, true);
	}
}

vcpu_trap_result_t
vgic_handle_vcpu_trap_wfi(void)
{
	thread_t *vcpu = thread_get_self();
	assert(vcpu != NULL);

	if (vcpu->vgic_vic != NULL) {
		// It is possible that a maintenance interrupt is currently
		// pending but was not delivered before the WFI trap. If so,
		// handling it might make more IRQs deliverable, in which case
		// the WFI should not be allowed to sleep.
		//
		// The simplest way to deal with this possibility is to run the
		// maintenance handler directly.
		preempt_disable();
		(void)vgic_handle_irq_received_maintenance();
		preempt_enable();
	}

	// Continue to the default handler
	return VCPU_TRAP_RESULT_UNHANDLED;
}

bool
vgic_handle_ipi_received_sync(void)
{
	vgic_sync_vcpu(thread_get_self(), true);

	return false;
}

bool
vgic_handle_ipi_received_deliver(void)
{
	thread_t *current = thread_get_self();
	assert(current != NULL);
	vic_t *vic = current->vgic_vic;

	if ((vic != NULL) && !current->vgic_sleep) {
		current->vgic_ich_hcr = register_ICH_HCR_EL2_read();

		for (index_t i = 0; i < CPU_GICH_LR_COUNT; i++) {
			vgic_lr_status_t *status = &current->vgic_lrs[i];
			if (status->dstate == NULL) {
				continue;
			}
			vgic_read_lr_state(i);
		}

		if (vgic_do_delivery_check(vic, current)) {
			vcpu_wakeup_self();
		}

		register_ICH_HCR_EL2_write(current->vgic_ich_hcr);
	}

	return false;
}

bool
vgic_handle_ipi_received_sgi(void)
{
	thread_t *current = thread_get_self();
	assert(current != NULL);
	vic_t *vic = current->vgic_vic;

	VGIC_TRACE(SGI, vic, current, "sgi ipi: pending {:#x}",
		   atomic_load_relaxed(current->vgic_pending_sgis));

	if (vic != NULL) {
		vgic_deliver_pending_sgi(vic, current);
	}

	return false;
}

// GICC
void
vgic_icc_irq_deactivate(vic_t *vic, irq_t irq_num)
{
	thread_t *		       vcpu = thread_get_self();
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, vcpu, irq_num);
	assert(dstate != NULL);

	// Don't let context switches delist the VIRQ out from under us
	preempt_disable();

	// Call generic deactivation handling if not currently listed
	vgic_delivery_state_t old_dstate = atomic_load_acquire(dstate);
	if (!vgic_delivery_state_get_listed(&old_dstate)) {
		vgic_deactivate(vic, thread_get_self(), irq_num, dstate,
				old_dstate, false);
		goto out;
	}

	// Search the current CPU's list registers for the VIRQ
	for (index_t lr = 0; lr < CPU_GICH_LR_COUNT; lr++) {
		vgic_lr_status_t *status = &vcpu->vgic_lrs[lr];
		if (status->dstate != dstate) {
			continue;
		}

		vgic_read_lr_state(lr);

		if ((ICH_LR_EL2_base_get_State(&status->lr.base) ==
		     ICH_LR_EL2_STATE_PENDING) ||
		    (ICH_LR_EL2_base_get_State(&status->lr.base) ==
		     ICH_LR_EL2_STATE_INVALID)) {
			// Interrupt is not active; nothing to do.
			goto out;
		}

		// Determine whether the edge bit should be reset when
		// delisting.
		bool listed_pending =
			ICH_LR_EL2_base_get_State(&status->lr.base) ==
			ICH_LR_EL2_STATE_PENDING_ACTIVE;

		// Kick the interrupt out of the LR. We could potentially keep
		// it listed if it is still pending, but that complicates the
		// code too much and we don't care about EOImode=1 VMs anyway.
		status->lr.base = ICH_LR_EL2_base_default();
		status->dstate	= NULL;
		vgic_write_lr(lr);

		vgic_deactivate(vic, thread_get_self(), irq_num, dstate,
				old_dstate, listed_pending);

		goto out;
	}

	// If we didn't find the LR, it's listed on another CPU.
	//
	// DIR is supposed to work across CPUs so we should flag the IRQ and
	// send an IPI to deactivate it. Possibly an extra dstate bit would
	// work for this. However, few VMs will use EOImode=1 so we don't care
	// very much just yet. For now, warn and do nothing.
#if !defined(NDEBUG)
	static _Thread_local bool warned_about_ignored_dir = false;
	if (!warned_about_ignored_dir) {
		TRACE_AND_LOG(DEBUG, WARN,
			      "vcpu {:#x}: trapped ICC_DIR_EL1 write "
			      "was cross-CPU; vIRQ {:d} may be stuck active",
			      (uintptr_t)(thread_t *)thread_get_self(),
			      irq_num);
		warned_about_ignored_dir = true;
	}
#endif

out:
	preempt_enable();
}

void
vgic_icc_generate_sgi(vic_t *vic, ICC_SGIR_EL1_t sgir, bool is_group_1)
{
	register_t target_list	 = ICC_SGIR_EL1_get_TargetList(&sgir);
	index_t	   target_offset = 16U * ICC_SGIR_EL1_get_RS(&sgir);
	virq_t	   virq		 = ICC_SGIR_EL1_get_INTID(&sgir);

	assert(virq < GIC_SGI_NUM);

	if (compiler_unexpected(ICC_SGIR_EL1_get_IRM(&sgir))) {
		TRACE_AND_LOG(DEBUG, WARN,
			      "vcpu {:#x}: SGIR write with IRM set ignored",
			      (uintptr_t)(thread_t *)thread_get_self());
		goto out;
	}

	psci_mpidr_t route_id = psci_mpidr_default();
	psci_mpidr_set_Aff1(&route_id, ICC_SGIR_EL1_get_Aff1(&sgir));
	psci_mpidr_set_Aff2(&route_id, ICC_SGIR_EL1_get_Aff2(&sgir));
	psci_mpidr_set_Aff3(&route_id, ICC_SGIR_EL1_get_Aff3(&sgir));

	rcu_read_start();

	while (target_list != 0U) {
		index_t target_bit = compiler_ctz(target_list);
		target_list &= ~util_bit(target_bit);

		index_t target = target_bit + target_offset;
		psci_mpidr_set_Aff0(&route_id, (uint8_t)target);

		cpu_index_result_t cpu_r =
			platform_cpu_mpidr_to_index(route_id);
		if ((cpu_r.e != OK) || (cpu_r.r >= vic->gicr_count)) {
			// ignore invalid target
			continue;
		}

		thread_t *vcpu = atomic_load_consume(&vic->gicr_vcpus[cpu_r.r]);
		if (vcpu == NULL) {
			// ignore missing target
			continue;
		}

		_Atomic vgic_delivery_state_t *dstate =
			&vcpu->vgic_private_states[virq];
		vgic_delivery_state_t old_dstate = atomic_load_relaxed(dstate);

		if (!is_group_1 &&
		    vgic_delivery_state_get_group1(&old_dstate)) {
			// SGI0R & ASGI1R do not generate group 1 SGIs
			continue;
		}

		if ((vcpu != thread_get_self()) &&
		    vgic_delivery_state_get_enabled(&old_dstate)) {
			VGIC_TRACE(SGI, vic, vcpu, "sgi fast: {:d}", virq);

			// Mark the SGI as pending delivery, and wake
			// the target VCPU for delivery.
			bitmap_atomic_set(vcpu->vgic_pending_sgis, virq,
					  memory_order_relaxed);

			// Match the seq_cst fences when the owner is changed
			// during the context switch.
			atomic_thread_fence(memory_order_seq_cst);

			cpu_index_t lr_owner =
				atomic_load_relaxed(&vcpu->vgic_lr_owner);

			if (cpulocal_index_valid(lr_owner)) {
				ipi_one(IPI_REASON_VGIC_SGI, lr_owner);
			} else {
				scheduler_lock(vcpu);
				vcpu_wakeup(vcpu);
				scheduler_unlock(vcpu);
			}
		} else {
			// Deliver the interrupt to the target
			vgic_delivery_state_t assert_dstate =
				vgic_delivery_state_default();
			vgic_delivery_state_set_edge(&assert_dstate, true);

			(void)vgic_deliver(virq, vic, vcpu, NULL, dstate,
					   assert_dstate, false, true);
		}
	}

	rcu_read_finish();
out:
	return;
}
