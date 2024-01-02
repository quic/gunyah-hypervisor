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
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <platform_cpu.h>
#include <platform_irq.h>
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

#if defined(ARCH_ARM_FEAT_FGT) && ARCH_ARM_FEAT_FGT
#include <arm_fgt.h>
#endif

#include "event_handlers.h"
#include "gich_lrs.h"
#include "gicv3.h"
#include "internal.h"

static hwirq_t *vgic_maintenance_hwirq;

static asm_ordering_dummy_t gich_lr_ordering;

static bool
vgic_fgt_allowed(void)
{
#if defined(ARCH_ARM_FEAT_FGT) && ARCH_ARM_FEAT_FGT
	return compiler_expected(arm_fgt_is_allowed());
#else
	return false;
#endif
}

void
vgic_handle_boot_hypervisor_start(void)
{
#if !defined(NDEBUG)
	register_t flags = 0U;
	TRACE_SET_CLASS(flags, VGIC);
#if defined(VERBOSE) && VERBOSE
	TRACE_SET_CLASS(flags, VGIC_DEBUG);
#endif
	trace_set_class_flags(flags);
#endif

	hwirq_create_t hwirq_args = {
		.irq	= PLATFORM_GICH_IRQ,
		.action = HWIRQ_ACTION_VGIC_MAINTENANCE,
	};
	hwirq_ptr_result_t hwirq_r =
		partition_allocate_hwirq(partition_get_private(), hwirq_args);
	if (hwirq_r.e != OK) {
		panic("Unable to create GICH HWIRQ");
	}
	if (object_activate_hwirq(hwirq_r.r) != OK) {
		panic("Unable to activate GICH HWIRQ");
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
	thread_t *current = thread_get_self();
	assert((current != NULL) && (current->kind == THREAD_KIND_VCPU));

	assert_debug(i < CPU_GICH_LR_COUNT);
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
	assert_debug(i < CPU_GICH_LR_COUNT);
	thread_t *current = thread_get_self();
	assert_debug((current != NULL) && (current->kind == THREAD_KIND_VCPU));

	vgic_lr_status_t *status = &current->vgic_lrs[i];

	gicv3_write_ich_lr(i, status->lr, &gich_lr_ordering);
}

#if VGIC_HAS_1N
static bool
vgic_get_delivery_state_is_class0(vgic_delivery_state_t *dstate)
{
	bool ret;
#if defined(GICV3_HAS_GICD_ICLAR) && GICV3_HAS_GICD_ICLAR
	ret = !vgic_delivery_state_get_nclass0(dstate);
#else
	(void)dstate;
	ret = true;
#endif

	return ret;
}

static bool
vgic_get_delivery_state_is_class1(vgic_delivery_state_t *dstate)
{
	bool ret;
#if defined(GICV3_HAS_GICD_ICLAR) && GICV3_HAS_GICD_ICLAR
	ret = vgic_delivery_state_get_class1(dstate);
#else
	(void)dstate;
	ret = false;
#endif

	return ret;
}
#endif

// Determine whether a VCPU is a valid route for a given VIRQ.
//
// This is allowed to take the enabled groups into account, but must ignore the
// VCPU's priority mask, because ICV_CTLR_EL1[6] (the virtual ICC_CTLR_EL1.PMHE
// analogue) is RES0.
//
// This function must not have side-effects. It may be called without holding
// any locks, to assist with routing decisions, but the result is only
// guaranteed to be accurate if the LR owner lock is held.
static bool
vgic_route_allowed(vic_t *vic, thread_t *vcpu, vgic_delivery_state_t dstate)
{
	bool allowed;
	(void)vic;

	if (vgic_delivery_state_get_group1(&dstate)
		    ? !vcpu->vgic_group1_enabled
		    : !vcpu->vgic_group0_enabled) {
		allowed = false;
	}
#if VGIC_HAS_1N
	else if (vgic_delivery_state_get_route_1n(&dstate)) {
		// We don't implement DPG bits in the virtual GIC, so just
		// check the class bits.
		allowed = (platform_irq_cpu_class(
				   (cpu_index_t)vcpu->vgic_gicr_index) == 0U)
				  ? vgic_get_delivery_state_is_class0(&dstate)
				  : vgic_get_delivery_state_is_class1(&dstate);
	}
#endif
	else {
		// Is this VCPU the VIRQ's direct route?
		index_t route_index = vgic_delivery_state_get_route(&dstate);
		allowed		    = (vcpu->vgic_gicr_index == route_index);
	}

	return allowed;
}

static void
vgic_route_and_flag(vic_t *vic, virq_t virq, vgic_delivery_state_t new_dstate,
		    bool use_local_vcpu);

#if VGIC_HAS_1N
static void
vgic_spi_reset_route_1n(virq_source_t *source, vgic_delivery_state_t dstate)
{
	if ((source != NULL) &&
	    (source->trigger == VIRQ_TRIGGER_VGIC_FORWARDED_SPI)) {
		// Restore the 1-of-N route
		hwirq_t *hwirq = hwirq_from_virq_source(source);

		GICD_IROUTER_t route_1n = GICD_IROUTER_default();
		GICD_IROUTER_set_IRM(&route_1n, true);
		(void)gicv3_spi_set_route(hwirq->irq, route_1n);

#if GICV3_HAS_GICD_ICLAR
		// Set the HW IRQ's 1-of-N routing classes. Note that these are
		// reset in the hardware whenever the IRM bit is cleared.
		(void)gicv3_spi_set_classes(
			hwirq->irq, !vgic_delivery_state_get_nclass0(&dstate),
			vgic_delivery_state_get_class1(&dstate));
#else
		(void)dstate;
#endif
	}
}
#endif

// Check whether a level-triggered source is still asserting its interrupt.
static bool
vgic_virq_check_pending(virq_source_t *source, bool reasserted)
	REQUIRE_PREEMPT_DISABLED
{
	bool is_pending;

	if (compiler_unexpected(source == NULL)) {
		// Source has been detached since the IRQ was asserted.
		is_pending = false;
	} else {
		// The virq_check_pending event must guarantee that all memory
		// reads executed by the handler are ordered after the read that
		// determined (a) that the IRQ was marked level-pending, and (b)
		// the value of the reasserted argument. Since the callers of
		// this function make those determinations using relaxed atomic
		// reads of the delivery state, we need an acquire fence here to
		// enforce the correct ordering.
		atomic_thread_fence(memory_order_acquire);

		is_pending = trigger_virq_check_pending_event(
			source->trigger, source, reasserted);
	}

	return is_pending;
}

static bool
vgic_sync_lr_should_be_pending(bool lr_hw, bool lr_pending, bool lr_active,
			       bool allow_pending, bool hw_detach,
			       vgic_delivery_state_t *new_dstate)
{
	bool remove_hw, virq_pending;

	// If the IRQ is still pending, we need to deliver it again.
	virq_pending = vgic_delivery_state_get_enabled(new_dstate) &&
		       vgic_delivery_state_is_pending(new_dstate);

	// Determine whether to delist the IRQ, and whether the HW=1 bit
	// is being removed from a valid LR (whether delisted or not).
	if (!lr_active && (!virq_pending || !allow_pending)) {
		vgic_delivery_state_set_listed(new_dstate, false);
		vgic_delivery_state_set_active(new_dstate, false);
		remove_hw = lr_pending;
	} else if (virq_pending && allow_pending) {
		// We're going to leave the LR in pending state, so
		// clear the edge bit.
		vgic_delivery_state_set_edge(new_dstate, false);
		remove_hw = false;
	} else {
		// We are leaving the VIRQ listed in active state, and
		// can't set the pending state in the LR. If the VIRQ is
		// pending, we must trap EOI to deliver it elsewhere.
		remove_hw = virq_pending;
	}

	// If we're removing HW=1 from a valid LR, but not detaching
	// (and therefore deactivating) the HW IRQ, we need to reset the
	// hw_active bit so the HW IRQ will be deactivated later.
	if (lr_hw && remove_hw && !hw_detach) {
		vgic_delivery_state_set_hw_active(new_dstate, true);
	}

	return virq_pending;
}

static bool
vgic_sync_lr_check_src(vic_t *vic, thread_t *vcpu, virq_t virq,
		       vgic_delivery_state_t  old_dstate,
		       vgic_delivery_state_t  clear_dstate,
		       vgic_delivery_state_t *new_dstate, bool lr_hw,
		       bool lr_pending, bool lr_has_eoi, bool hw_detach)
	REQUIRE_PREEMPT_DISABLED
{
	virq_source_t *source	       = vgic_find_source(vic, vcpu, virq);
	bool	       need_deactivate = false;

	// If the LR is in pending state, reset the edge bit, unless it's being
	// explicitly cleared. Note that it will be cleared again later in the
	// sync_lr update if we decide to leave the LR in pending state.
	if (lr_pending && !vgic_delivery_state_get_edge(&clear_dstate)) {
		vgic_delivery_state_set_edge(new_dstate, true);
	}

	// If the IRQ is level-triggered, determine whether to leave it pending.
	if (vgic_delivery_state_get_level_src(&old_dstate) &&
	    !vgic_delivery_state_get_level_src(&clear_dstate)) {
		// level_src is set and is not being explicitly cleared.
		// Determine whether it should be cleared based on the LR's
		// pending state.
		if (lr_hw && (!lr_pending || hw_detach)) {
			// Pending state was consumed, so reset
			// level_src to hw_active (which preserves any
			// remote assertion).
			vgic_delivery_state_set_level_src(
				new_dstate,
				vgic_delivery_state_get_hw_active(&old_dstate));
		} else if (lr_has_eoi && compiler_expected(source != NULL) &&
			   (source->trigger ==
			    VIRQ_TRIGGER_VGIC_FORWARDED_SPI)) {
			// EOI occurred after a SW delivery; assume the HW
			// source is no longer pending, because the handler
			// probably cleared it. If it is still pending, then
			// the HW will re-deliver it after the deactivation.
			vgic_delivery_state_set_level_src(new_dstate, false);
		} else {
			bool reassert =
				lr_pending ||
				vgic_delivery_state_get_edge(&old_dstate);
			if (!vgic_virq_check_pending(source, reassert)) {
				vgic_delivery_state_set_level_src(new_dstate,
								  false);
			}
		}
	}

	// If the IRQ is no longer deliverable, deactivate the HW source.
	if (!vgic_delivery_state_is_pending(new_dstate) ||
	    !vgic_delivery_state_get_enabled(new_dstate)) {
		need_deactivate =
			vgic_delivery_state_get_hw_active(&old_dstate);
		vgic_delivery_state_set_hw_active(new_dstate, false);
	}

	return need_deactivate;
}

typedef struct {
	vgic_delivery_state_t new_dstate;
	bool		      virq_pending;
	bool		      hw_detach;
	bool		      allow_pending;
	bool		      deactivate_hw;
} vgic_sync_lr_update_t;

static vgic_sync_lr_update_t
vgic_sync_lr_update_delivery_state(vic_t *vic, thread_t *vcpu,
				   const vgic_lr_status_t *status,
				   vgic_delivery_state_t   clear_dstate,
				   bool lr_hw, bool lr_pending, virq_t virq,
				   bool lr_active) REQUIRE_PREEMPT_DISABLED
{
	vgic_delivery_state_t old_dstate = atomic_load_relaxed(status->dstate);
	bool hw_detach = vgic_delivery_state_get_hw_active(&clear_dstate);

	vgic_delivery_state_t new_dstate;
	bool		      virq_pending;
	bool		      allow_pending;
	bool		      deactivate_hw;

	const bool lr_has_eoi = !lr_hw && !lr_pending && !lr_active &&
				ICH_LR_EL2_HW0_get_EOI(&status->lr.sw);

	do {
		assert(vgic_delivery_state_get_listed(&old_dstate));
		new_dstate = vgic_delivery_state_difference(old_dstate,
							    clear_dstate);

		// Determine whether the LR can be left in pending state.
		allow_pending = (!lr_hw || !lr_active) &&
				vgic_delivery_state_get_enabled(&new_dstate) &&
				vgic_route_allowed(vic, vcpu, new_dstate);

		// We always handle HW detachment, even if not delisting. Note
		// that nobody can concurrently clear hw_detached, so we don't
		// need to reset the local hw_detached variable if it is false.
		if (vgic_delivery_state_get_hw_detached(&old_dstate)) {
			vgic_delivery_state_set_hw_detached(&new_dstate, false);
			hw_detach = true;
		}

		// Check the VIRQ's source and update the delivery state.
		deactivate_hw = vgic_sync_lr_check_src(
			vic, vcpu, virq, old_dstate, clear_dstate, &new_dstate,
			lr_hw, lr_pending, lr_has_eoi, hw_detach);

		// Determine the new pending state of the LR.
		virq_pending = vgic_sync_lr_should_be_pending(
			lr_hw, lr_pending, lr_active, allow_pending, hw_detach,
			&new_dstate);

		// The VIRQ should now be in sync.
		vgic_delivery_state_set_need_sync(&new_dstate, false);
	} while (!atomic_compare_exchange_strong_explicit(
		status->dstate, &old_dstate, new_dstate, memory_order_relaxed,
		memory_order_relaxed));

	VGIC_TRACE(DSTATE_CHANGED, vic, vcpu, "sync_lr {:d}: {:#x} -> {:#x}",
		   virq, vgic_delivery_state_raw(old_dstate),
		   vgic_delivery_state_raw(new_dstate));

	return (vgic_sync_lr_update_t){
		.new_dstate    = new_dstate,
		.virq_pending  = virq_pending,
		.hw_detach     = hw_detach,
		.allow_pending = allow_pending,
		.deactivate_hw = deactivate_hw,
	};
}

static void
vgic_sync_lr_update_lr(vic_t *vic, thread_t *vcpu, vgic_lr_status_t *status,
		       bool lr_pending, virq_t virq, bool lr_active,
		       bool virq_pending, bool allow_pending, bool lr_hw,
		       vgic_delivery_state_t new_dstate, bool use_local_vcpu)
	REQUIRE_PREEMPT_DISABLED
{
	if (!vgic_delivery_state_get_listed(&new_dstate)) {
		VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
			   "sync_lr {:d}: delisted (pending {:d})", virq,
			   (register_t)virq_pending);

#if VGIC_HAS_1N
		if (vgic_delivery_state_get_route_1n(&new_dstate)) {
			virq_source_t *source =
				vgic_find_source(vic, vcpu, virq);
			vgic_spi_reset_route_1n(source, new_dstate);
		}
#endif
		status->dstate	= NULL;
		status->lr.base = ICH_LR_EL2_base_default();

		if (virq_pending) {
			vgic_route_and_flag(vic, virq, new_dstate,
					    use_local_vcpu);
		}
	} else if (!allow_pending) {
		VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
			   "sync_lr {:d}: LR left active ({:s} pending)", virq,
			   (uintptr_t)(virq_pending ? "still" : "not"));

		// We may have been in pending and active state; remove the
		// pending state bit.
		assert(lr_active);
		ICH_LR_EL2_base_set_State(&status->lr.base,
					  ICH_LR_EL2_STATE_ACTIVE);

		if (virq_pending) {
			// The VIRQ is still pending. We need to set the EOI
			// trap bit in the LR to ensure that the IRQ can be
			// delivered again later. The HW=1 bit must be cleared
			// to do this; so, if it was previously set, we must
			// have reset hw_active in the dstate already.
			assert(!lr_hw ||
			       vgic_delivery_state_get_hw_active(&new_dstate));
			ICH_LR_EL2_base_set_HW(&status->lr.base, false);
			ICH_LR_EL2_HW0_set_EOI(&status->lr.sw, true);
		}
	} else if (virq_pending) {
		VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
			   "sync_lr {:d}: LR set pending ({:s} active)", virq,
			   (uintptr_t)(lr_active ? "and" : "not"));

		// We can leave the LR in a pending state.
		ICH_LR_EL2_base_set_State(
			&status->lr.base,
			lr_active ? ICH_LR_EL2_STATE_PENDING_ACTIVE
				  : ICH_LR_EL2_STATE_PENDING);

		if (!lr_pending && !lr_active) {
			// This is a new delivery; make sure the VCPU is awake.
			if (vcpu == thread_get_self()) {
				vcpu_wakeup_self();
			} else {
				scheduler_lock_nopreempt(vcpu);
				vcpu_wakeup(vcpu);
				scheduler_unlock_nopreempt(vcpu);
			}

			// The dstate update above never clears hw_active, so
			// any new delivery must be HW=0, even if it came from a
			// forwarded SPI (which is unlikely because it must have
			// been misrouted). The HW bit might still be set from
			// an earlier delivery, so clear it here.
			ICH_LR_EL2_base_set_HW(&status->lr.base, false);

			// We need to trap EOI if the IRQ is level triggered or
			// the HW source is active.
			ICH_LR_EL2_HW0_set_EOI(
				&status->lr.sw,
				!vgic_delivery_state_get_cfg_is_edge(
					&new_dstate) ||
					vgic_delivery_state_get_hw_active(
						&new_dstate));
		} else if (vgic_delivery_state_get_hw_active(&new_dstate)) {
			// If the dstate update left hw_active set, we need to
			// force HW=0 and trap EOI to deactivate the HW IRQ.
			ICH_LR_EL2_base_set_HW(&status->lr.base, false);
			ICH_LR_EL2_HW0_set_EOI(&status->lr.sw, true);
		} else if (!ICH_LR_EL2_base_get_HW(&status->lr.base)) {
			// We also need to trap EOI for SW asserted level
			// triggered IRQs.
			ICH_LR_EL2_HW0_set_EOI(
				&status->lr.sw,
				!vgic_delivery_state_get_cfg_is_edge(
					&new_dstate));
		} else {
			// Existing HW delivery; EOI handled by physical GIC
		}
	} else {
		// The IRQ is remaining listed, is allowed to remain pending,
		// and does not need to be set pending; no LR change needed.
		VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
			   "sync_lr {:d}: LR unchanged", virq);
	}
}

// Synchronise a VIRQ's delivery state with its LR.
//
// This is used for all updates to a currently listed VIRQ other than a local
// redelivery or deactivation. That includes disabling, clearing, rerouting,
// reprioritising, cross-CPU asserting or deactivating, handling an EOI trap, or
// releasing the source.
//
// Asserting a locally listed VIRQ is handled by vgic_redeliver_lr().
// Deactivating a locally listed VIRQ is handled by vgic_deactivate().
//
// The flags that are set in the clear_dstate argument, if any, will be cleared
// in the delivery state. This value must not have any flags set other than the
// four pending flags, the enabled flag, and the hardware active flag.
//
// If the current delivery state has the enable bit clear or clear_dstate has
// the enable bit set, the pending state will be removed from the LR regardless
// of the pending state of the interrupt (though the active state can remain in
// the LR).
//
// If the current delivery state has the hw_detached bit set or clear_dstate has
// the hw_active bit set, the HW bit of the LR will be cleared even if it is
// left listed. The HW bit of the LR may also be cleared if it is necessary to
// trap EOI to guarantee delivery of the IRQ.
//
// The specified VCPU must either be the current thread, or LR-locked by
// the caller and known not to be running remotely. If the VCPU is the current
// thread, the caller is responsible for syncing and updating the physical LR.
//
// For hardware interrupts, the level_src flag in clear_dstate may be overridden
// by the hw_active flag, if it has been set by a concurrent remote delivery;
// this is unnecessary for software interrupts because level_src changes are
// required to be serialised.
//
// If the VIRQ is still enabled and pending after clearing the pending and
// enable bits, it will be set pending in the LR if possible, or otherwise
// rerouted. If it is 1-of-N, the use_local_vcpu flag determines whether the
// current VCPU is given routing priority.
//
// The result is true if the VIRQ has been unlisted.
static bool
vgic_sync_lr(vic_t *vic, thread_t *vcpu, vgic_lr_status_t *status,
	     vgic_delivery_state_t clear_dstate, bool use_local_vcpu)
	REQUIRE_PREEMPT_DISABLED EXCLUDE_SCHEDULER_LOCK(vcpu)
{
	virq_t virq = ICH_LR_EL2_base_get_vINTID(&status->lr.base);

	assert(status->dstate != NULL);

	bool		   lr_hw = ICH_LR_EL2_base_get_HW(&status->lr.base);
	ICH_LR_EL2_State_t lr_state =
		ICH_LR_EL2_base_get_State(&status->lr.base);
	bool lr_pending = (lr_state == ICH_LR_EL2_STATE_PENDING) ||
			  (lr_state == ICH_LR_EL2_STATE_PENDING_ACTIVE);
	bool lr_active = (lr_state == ICH_LR_EL2_STATE_ACTIVE) ||
			 (lr_state == ICH_LR_EL2_STATE_PENDING_ACTIVE);

	vgic_delivery_state_t new_dstate;
	bool		      virq_pending;
	bool		      hw_detach;
	bool		      allow_pending;
	bool		      deactivate_hw;
	{
		vgic_sync_lr_update_t sync_lr_info =
			vgic_sync_lr_update_delivery_state(vic, vcpu, status,
							   clear_dstate, lr_hw,
							   lr_pending, virq,
							   lr_active);
		new_dstate    = sync_lr_info.new_dstate;
		virq_pending  = sync_lr_info.virq_pending;
		hw_detach     = sync_lr_info.hw_detach;
		allow_pending = sync_lr_info.allow_pending;
		deactivate_hw = sync_lr_info.deactivate_hw;
	}

	// If we're detaching a HW IRQ, clear the HW bit in the LR.
	if (compiler_unexpected(lr_hw && hw_detach)) {
		// If the LR was pending or active, the physical IRQ is still
		// active. Clearing the HW bit destroys our record that this
		// might be the case, so we have to deactivate at this point.
		if (lr_pending || lr_active) {
			assert(!vgic_delivery_state_get_hw_active(&new_dstate));
			irq_t irq = ICH_LR_EL2_HW1_get_pINTID(&status->lr.hw);
			VGIC_TRACE(
				HWSTATE_CHANGED, vic, vcpu,
				"sync_lr {:d}: deactivate HW IRQ {:d} (detach)",
				virq, irq);
			gicv3_irq_deactivate(irq);
		}

		// If the LR will remain valid, turn it into a SW IRQ.
		if (vgic_delivery_state_get_listed(&new_dstate)) {
			ICH_LR_EL2_base_set_HW(&status->lr.base, false);
			// If HW was 1 there must be no SW level assertion, so
			// we don't need to trap EOI
			ICH_LR_EL2_HW0_set_EOI(&status->lr.sw, false);
		}
	}

	// If we are clearing HW active for a SW LR, deactivate the HW IRQ.
	if (deactivate_hw) {
		virq_source_t *source = vgic_find_source(vic, vcpu, virq);
		assert((source != NULL) &&
		       (source->trigger == VIRQ_TRIGGER_VGIC_FORWARDED_SPI));
		hwirq_t *hwirq = hwirq_from_virq_source(source);

		VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
			   "sync_lr {:d}: deactivate HW IRQ {:d} (EOI)", virq,
			   hwirq->irq);
		irq_deactivate(hwirq);
	}

	// Update the LR.
	vgic_sync_lr_update_lr(vic, vcpu, status, lr_pending, virq, lr_active,
			       virq_pending, allow_pending, lr_hw, new_dstate,
			       use_local_vcpu);

	return !vgic_delivery_state_get_listed(&new_dstate);
}

static bool
vgic_undeliver_update_hw_detach_and_sync(const vic_t *vic, const thread_t *vcpu,
					 virq_t				virq,
					 _Atomic vgic_delivery_state_t *dstate,
					 vgic_delivery_state_t clear_dstate,
					 vgic_delivery_state_t old_dstate,
					 bool		       check_route)
{
	vgic_delivery_state_t new_dstate;
	bool hw_detach = vgic_delivery_state_get_hw_active(&clear_dstate);
	vgic_delivery_state_set_hw_active(&clear_dstate, false);

	do {
		new_dstate = vgic_delivery_state_difference(old_dstate,
							    clear_dstate);

		if (!vgic_delivery_state_get_listed(&old_dstate)) {
			// Delisted by another thread; no sync needed.
		} else if (check_route) {
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
		dstate, &old_dstate, new_dstate, memory_order_relaxed,
		memory_order_relaxed));

	VGIC_TRACE(DSTATE_CHANGED, vic, vcpu,
		   "undeliver-sync {:d}: {:#x} -> {:#x}", virq,
		   vgic_delivery_state_raw(old_dstate),
		   vgic_delivery_state_raw(new_dstate));

	return !vgic_delivery_state_get_listed(&old_dstate);
}

static vgic_delivery_state_t
vgic_undeliver_update_dstate(vic_t *vic, thread_t *vcpu,
			     _Atomic vgic_delivery_state_t *dstate, virq_t virq,
			     vgic_delivery_state_t  clear_dstate,
			     vgic_delivery_state_t *old_dstate)
	REQUIRE_PREEMPT_DISABLED
{
	vgic_delivery_state_t new_dstate;
	do {
		// If the VIRQ is not listed, update its flags directly.
		new_dstate = vgic_delivery_state_difference(*old_dstate,
							    clear_dstate);
		if (vgic_delivery_state_get_listed(old_dstate)) {
			break;
		}

		// If level_src is set and is not being explicitly cleared,
		// check whether we need to clear it.
		if (vgic_delivery_state_get_level_src(old_dstate) &&
		    !vgic_delivery_state_get_level_src(&clear_dstate)) {
			virq_source_t *source =
				vgic_find_source(vic, vcpu, virq);
			if (!vgic_virq_check_pending(
				    source,
				    vgic_delivery_state_get_edge(old_dstate))) {
				vgic_delivery_state_set_level_src(&new_dstate,
								  false);
			}
		}
	} while (!atomic_compare_exchange_strong_explicit(
		dstate, old_dstate, new_dstate, memory_order_relaxed,
		memory_order_relaxed));

	return new_dstate;
}

// Clear pending bits from a given VIRQ, and abort its delivery if necessary.
//
// This is used when disabling, rerouting, manually clearing, or releasing the
// source of a VIRQ.
//
// The specified VCPU is the current route of the VIRQ if it is shared (in which
// case it may be NULL), or the owner of the VIRQ if it is private.
//
// The pending flags in clear_dstate will be cleared in the delivery state.
// This value must not have any flags set other than the four pending flags,
// the enabled flag, and the hw_active flag. Also, the hw_active flag should
// always be set if the edge or level_src flags are set; this is because
// clearing a pending HW IRQ without deactivating it may make it undeliverable.
//
// If this function returns true, the interrupt is known not to have been listed
// anywhere at the time the pending flags were cleared. If it returns false, the
// interrupt may still be listed on remotely running VCPUs.
bool
vgic_undeliver(vic_t *vic, thread_t *vcpu,
	       _Atomic vgic_delivery_state_t *dstate, virq_t virq,
	       vgic_delivery_state_t clear_dstate, bool check_route)
{
	bool from_self = vcpu == thread_get_self();

	assert(vgic_delivery_state_get_hw_active(&clear_dstate) ||
	       (!vgic_delivery_state_get_edge(&clear_dstate) &&
		!vgic_delivery_state_get_level_src(&clear_dstate)));

	cpu_index_t remote_cpu = vgic_lr_owner_lock(vcpu);

	vgic_delivery_state_t old_dstate = atomic_load_relaxed(dstate);
	vgic_delivery_state_t new_dstate = vgic_undeliver_update_dstate(
		vic, vcpu, dstate, virq, clear_dstate, &old_dstate);

	bool unlisted = false;
	if (!vgic_delivery_state_get_listed(&old_dstate)) {
		VGIC_TRACE(DSTATE_CHANGED, vic, vcpu,
			   "undeliver-unlisted {:d}: {:#x} -> {:#x}", virq,
			   vgic_delivery_state_raw(old_dstate),
			   vgic_delivery_state_raw(new_dstate));

		// If we just cleared the HW active flag, deactivate the IRQ.
		if (vgic_delivery_state_get_hw_active(&old_dstate) &&
		    !vgic_delivery_state_get_hw_active(&new_dstate)) {
			virq_source_t *source =
				vgic_find_source(vic, vcpu, virq);

			hwirq_t *hwirq = hwirq_from_virq_source(source);
			VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
				   "undeliver {:d}: deactivate HW IRQ {:d}",
				   virq, hwirq->irq);
			irq_deactivate(hwirq);
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
			unlisted = vgic_sync_lr(vic, vcpu, &vcpu->vgic_lrs[i],
						clear_dstate, false);
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
	// We can't directly clear hw_active on a remote CPU; we need to use
	// the hw_detached bit to ask the remote CPU to do it.
	unlisted = vgic_undeliver_update_hw_detach_and_sync(
		vic, vcpu, virq, dstate, clear_dstate, old_dstate, check_route);

out:
	vgic_lr_owner_unlock(vcpu);

	return unlisted;
}

typedef struct {
	ICH_LR_EL2_t	      new_lr;
	vgic_delivery_state_t new_dstate;
	bool		      force_eoi_trap;
	bool		      need_wakeup;
	uint8_t		      pad[2];
} vgic_redeliver_lr_info_t;

static vgic_redeliver_lr_info_t
vgic_redeliver_lr_update_state(const vic_t *vic, const thread_t *vcpu,
			       virq_source_t *source, virq_t virq,
			       ICH_LR_EL2_State_t      old_lr_state,
			       const vgic_lr_status_t *status,
			       vgic_delivery_state_t   old_dstate,
			       vgic_delivery_state_t   assert_dstate)
{
	vgic_delivery_state_t new_dstate =
		vgic_delivery_state_union(old_dstate, assert_dstate);
	bool	     is_hw  = vgic_delivery_state_get_hw_active(&new_dstate);
	ICH_LR_EL2_t new_lr = status->lr;
	bool	     force_eoi_trap = false;
	bool	     need_wakeup    = false;

	if (compiler_expected(old_lr_state == ICH_LR_EL2_STATE_INVALID)) {
		// Previous interrupt is gone; take the new one. Don't
		// bother to recheck level triggering yet; that will be
		// done when this interrupt ends.
		ICH_LR_EL2_base_set_HW(&new_lr.base, is_hw);
		if (is_hw) {
			vgic_delivery_state_set_hw_active(&new_dstate, false);
			ICH_LR_EL2_HW1_set_pINTID(
				&new_lr.hw,
				hwirq_from_virq_source(source)->irq);
		}
		ICH_LR_EL2_base_set_State(&new_lr.base,
					  ICH_LR_EL2_STATE_PENDING);

		// Interrupt is newly pending; we need to wake the VCPU.
		need_wakeup = true;
	} else if (compiler_unexpected(is_hw !=
				       ICH_LR_EL2_base_get_HW(&new_lr.base))) {
		// If we have both a SW and a HW source, deliver the SW
		// assertion first, and request an EOI maintenance
		// interrupt to deliver (or trigger reassertion of) the
		// HW source afterwards.
		if (ICH_LR_EL2_base_get_HW(&new_lr.base)) {
			ICH_LR_EL2_base_set_HW(&new_lr.base, false);
			vgic_delivery_state_set_hw_active(&new_dstate, true);

			VGIC_DEBUG_TRACE(
				HWSTATE_UNCHANGED, vic, vcpu,
				"redeliver {:d}: hw + sw; relisting as sw",
				virq);
		}
		force_eoi_trap = true;

		// Interrupt is either already pending (so the VCPU
		// should be awake) or is active (so not deliverable,
		// and the VCPU should not be woken); no need for a
		// wakeup.
	}
#if VGIC_HAS_LPI && GICV3_HAS_VLPI_V4_1
	else if ((old_lr_state == ICH_LR_EL2_STATE_ACTIVE) &&
		 vic->vsgis_enabled &&
		 (vgic_get_irq_type(virq) == VGIC_IRQ_TYPE_SGI)) {
		// A vSGI delivered by the ITS does not have an active
		// state, because it is really a vLPI in disguise. Make
		// software-delivered SGIs behave the same way.
		assert(!is_hw && !ICH_LR_EL2_base_get_HW(&new_lr.base));
		ICH_LR_EL2_base_set_State(&new_lr.base,
					  ICH_LR_EL2_STATE_PENDING);

		// Interrupt was previously active and is now pending,
		// so it has just become deliverable and we need to wake
		// the VCPU.
		need_wakeup = true;
	}
#endif
	else {
		// We should never get here for a hardware-mode LR,
		// since it would mean that we were risking a double
		// deactivate.
		assert(!is_hw && !ICH_LR_EL2_base_get_HW(&new_lr.base));

		// A software-mode LR that is in active state can me
		// moved straight to active+pending.
		if (old_lr_state == ICH_LR_EL2_STATE_ACTIVE) {
			ICH_LR_EL2_base_set_State(
				&new_lr.base, ICH_LR_EL2_STATE_PENDING_ACTIVE);
		} else {
			VGIC_DEBUG_TRACE(
				HWSTATE_UNCHANGED, vic, vcpu,
				"redeliver {:d}: redundant assertions merged",
				virq);
		}

		// Interrupt is already pending, so the VCPU should be
		// awake; no need for a wakeup.
	}

	vgic_delivery_state_set_edge(&new_dstate, force_eoi_trap);

	return (vgic_redeliver_lr_info_t){
		.new_dstate	= new_dstate,
		.force_eoi_trap = force_eoi_trap,
		.need_wakeup	= need_wakeup,
		.new_lr		= new_lr,
	};
}

static bool
vgic_redeliver_lr(vic_t *vic, thread_t *vcpu, virq_source_t *source,
		  _Atomic vgic_delivery_state_t *dstate,
		  vgic_delivery_state_t		*old_dstate,
		  vgic_delivery_state_t assert_dstate, index_t lr)
{
	assert_debug(lr < CPU_GICH_LR_COUNT);

	// Merge the old and new LR states.
	vgic_lr_status_t *status = &vcpu->vgic_lrs[lr];
	virq_t		  virq	 = ICH_LR_EL2_base_get_vINTID(&status->lr.base);

	// Update the delivery state.
	vgic_delivery_state_t new_dstate;
	ICH_LR_EL2_t	      new_lr;
	bool		      force_eoi_trap;
	bool		      need_wakeup;

	do {
		ICH_LR_EL2_State_t trace_state;
		assert(vgic_delivery_state_get_listed(old_dstate));

		ICH_LR_EL2_State_t old_lr_state =
			ICH_LR_EL2_base_get_State(&status->lr.base);

		{
			vgic_redeliver_lr_info_t vgic_redeliver_lr_info =
				vgic_redeliver_lr_update_state(
					vic, vcpu, source, virq, old_lr_state,
					status, *old_dstate, assert_dstate);
			new_dstate     = vgic_redeliver_lr_info.new_dstate;
			force_eoi_trap = vgic_redeliver_lr_info.force_eoi_trap;
			need_wakeup    = vgic_redeliver_lr_info.need_wakeup;
			new_lr	       = vgic_redeliver_lr_info.new_lr;
		}

		trace_state = ICH_LR_EL2_base_get_State(&new_lr.base);
		VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
			   "redeliver {:d}: lr {:d} -> {:d}", virq,
			   (register_t)old_lr_state, (register_t)trace_state);
	} while (!atomic_compare_exchange_strong_explicit(
		dstate, old_dstate, new_dstate, memory_order_relaxed,
		memory_order_relaxed));

	status->lr = new_lr;

	VGIC_TRACE(DSTATE_CHANGED, vic, vcpu, "redeliver {:d}: {:#x} -> {:#x}",
		   virq, vgic_delivery_state_raw(*old_dstate),
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
	       vgic_delivery_state_t	     *old_dstate,
	       vgic_delivery_state_t	      assert_dstate)
{
	const bool    to_self = vcpu == thread_get_self();
	index_t	      i;
	bool_result_t ret = bool_result_error(ERROR_BUSY);

	for (i = 0; i < CPU_GICH_LR_COUNT; i++) {
		if (dstate == vcpu->vgic_lrs[i].dstate) {
			break;
		}
	}

	if (i < CPU_GICH_LR_COUNT) {
		// If we are targeting ourselves, read the current state.
		if (to_self) {
			vgic_read_lr_state(i);
		}

		ret = bool_result_ok(vgic_redeliver_lr(vic, vcpu, source,
						       dstate, old_dstate,
						       assert_dstate, i));

		// Update the affected list register.
		if (to_self) {
			vgic_write_lr(i);
		}
	}

	return ret;
}

// Returns true if a list register is empty: invalid, and either HW or not
// EOI-trapped. This is the same condition used by the hardware to set bits in
// ICH_ELRSR_EL2.
static inline bool
vgic_lr_is_empty(ICH_LR_EL2_t lr)
{
	return (ICH_LR_EL2_base_get_State(&lr.base) ==
		ICH_LR_EL2_STATE_INVALID) &&
	       (ICH_LR_EL2_base_get_HW(&lr.base) ||
		!ICH_LR_EL2_HW0_get_EOI(&lr.sw));
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
	// - any inactive LR with no pending EOI maintenance IRQ, or
	// - the lowest-priority active or pending-and-active LR, or
	// - the lowest-priority pending LR, if it has lower priority than the
	//   VIRQ we're delivering.
	index_result_t result_pending	       = index_result_error(ERROR_BUSY);
	uint8_t	       priority_result_active  = 0U;
	uint8_t	       priority_result_pending = 0U;

	for (index_t i = 0; i < CPU_GICH_LR_COUNT; i++) {
		const vgic_lr_status_t *status = &vcpu->vgic_lrs[i];
		uint8_t			this_priority =
			ICH_LR_EL2_base_get_Priority(&status->lr.base);

		// If the VCPU is current and the LR was written in a valid
		// state, the hardware might have changed it to a different
		// valid state, so we must read it back. (It can't have been
		// either initially invalid or changed to invalid, because we
		// would have found it in a nonzero ELRSR above.)
		if (to_self) {
			vgic_read_lr_state(i);
		}

		ICH_LR_EL2_State_t state =
			ICH_LR_EL2_base_get_State(&status->lr.base);

		if (vgic_lr_is_empty(status->lr)) {
			// LR is empty; we can reclaim it immediately.
			result	     = index_result_ok(i);
			*lr_priority = GIC_PRIORITY_LOWEST;
			goto out;
		} else if (state == ICH_LR_EL2_STATE_INVALID) {
			// LR is inactive but has pending EOI maintenance. This
			// case is not handled by vgic_reclaim_lr() so we leave
			// this LR alone for now.
		} else if (state != ICH_LR_EL2_STATE_PENDING) {
			// LR is active or pending+active, so we can use it if
			// it has the lowest priority of any such LR. Note that
			// it must strictly be the lowest priority to make sure
			// we choose the right IRQs in the unlisted EOI handler.
			if (this_priority >= priority_result_active) {
				result		       = index_result_ok(i);
				*lr_priority	       = GIC_PRIORITY_LOWEST;
				priority_result_active = this_priority;
			}
		} else {
			// LR is pending, so we can use it if it has the lowest
			// priority of any such LR and is also lower priority
			// than the priority we're trying to deliver.
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

// The number of VIRQs in each low (SPI + PPI) range other than the last SPI
// range (which has 4 fewer because of the "special" IRQ numbers 1020-1023).
#define VGIC_LOW_RANGE_SIZE                                                    \
	(count_t)((GIC_SPI_BASE + GIC_SPI_NUM + VGIC_LOW_RANGES - 1U) /        \
		  VGIC_LOW_RANGES)
static_assert(util_is_p2(VGIC_LOW_RANGE_SIZE),
	      "VGIC search ranges must have power-of-two sizes");
static_assert(VGIC_LOW_RANGE_SIZE > GIC_SPECIAL_INTIDS_NUM,
	      "VGIC search ranges must have size greater than 4");

// The number of VIRQs in a specific low range, taking into account the special
// IRQ numbers that immediately follow the SPIs.
static count_t
vgic_low_range_size(index_t range)
{
	return (range == (VGIC_LOW_RANGES - 1U))
		       ? (VGIC_LOW_RANGE_SIZE - GIC_SPECIAL_INTIDS_NUM)
		       : VGIC_LOW_RANGE_SIZE;
}

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
vgic_flag_locked(virq_t virq, thread_t *vcpu, uint8_t priority, bool group1,
		 cpu_index_t remote_cpu) REQUIRE_PREEMPT_DISABLED
{
	assert_preempt_disabled();

	count_t priority_shifted = (count_t)priority >> VGIC_PRIO_SHIFT;

	bitmap_atomic_set(vcpu->vgic_search_ranges_low[priority_shifted],
			  virq / VGIC_LOW_RANGE_SIZE, memory_order_release);

	bitmap_atomic_set(vcpu->vgic_search_prios, priority_shifted,
			  memory_order_release);

	if (group1 ? !vcpu->vgic_group1_enabled : !vcpu->vgic_group0_enabled) {
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
// - another operation is already being performed on one of the VCPU's LRs and
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
	REQUIRE_PREEMPT_DISABLED
{
	count_t priority_shifted = (count_t)priority >> VGIC_PRIO_SHIFT;

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
					&vcpu->vgic_lr_owner_lock.owner);

				if (cpulocal_index_valid(lr_owner)) {
					ipi_one(IPI_REASON_VGIC_DELIVER,
						lr_owner);
				} else {
					scheduler_lock_nopreempt(vcpu);
					vcpu_wakeup(vcpu);
					scheduler_unlock_nopreempt(vcpu);
				}
			}
		}
	}
}

// Mark an unlisted interrupt as pending without a specific target VCPU.
//
// This is called when an interrupt is pending in the virtual distributor, but
// cannot be assigned to a specific VCPU, either because:
//
// - it has a direct route that is out of range or identifies a VCPU that has
//   not been attached yet; or
//
// - it has 1-of-N routing, but is in a group that is disabled on all VCPUs.
static void
vgic_flag_unrouted(vic_t *vic, virq_t virq)
{
	bitmap_atomic_set(vic->search_ranges_low, virq / VGIC_LOW_RANGE_SIZE,
			  memory_order_release);
}

#if VGIC_HAS_1N
// The degree to which a VCPU is preferred as the route for a VIRQ, in order of
// increasing preference.
typedef enum {
	// The VCPU can't be immediately chosen as a target (though it can still
	// be chosen if E1NWF is set and all cores are asleep).
	VGIC_ROUTE_DENIED = 0,

	// The VCPU has affinity to a remote physical CPU but is not expecting a
	// wakeup, which implies that it is either running and possibly busy,
	// preempted by another VCPU, or blocked by the hypervisor.
	VGIC_ROUTE_REMOTE_BUSY,

	// The VCPU has affinity to the local CPU, but is not current, and the
	// current VCPU has equal or higher scheduler priority. It is likely to
	// sleep for several milliseconds while the other VCPU runs.
	VGIC_ROUTE_PREEMPTED,

	// The VCPU has affinity to the local CPU but is already handling an IRQ
	// with equal or higher IRQ priority. It is likely to be busy with the
	// other IRQ for tens of microseconds or more.
	VGIC_ROUTE_BUSY,

	// The VCPU has affinity to a remote physical CPU and is waiting for a
	// wakeup from WFI. Note that VCPUs in a virtual power-off suspend will
	// have their groups disabled, and therefore will return DENIED.
	VGIC_ROUTE_REMOTE,

	// The VCPU has affinity to the local CPU. It is either current with no
	// other vIRQs at equal or higher priority, or is in WFI and will
	// preempt the current thread if woken.
	VGIC_ROUTE_IMMEDIATE,
} vgic_route_preference_t;

// Determine the level of route preference for the specified VCPU.
//
// The VCPU's scheduler lock is held when this is called, so it is safe to query
// the scheduler state. However, note that the lock will be released before the
// result is used.
//
// The VCPU's LR owner lock is not held when this is called.
static vgic_route_preference_t
vgic_route_1n_preference(vic_t *vic, thread_t *vcpu,
			 vgic_delivery_state_t dstate)
	REQUIRE_SCHEDULER_LOCK(vcpu)
{
	vgic_route_preference_t ret	= VGIC_ROUTE_DENIED;
	thread_t	       *current = thread_get_self();

	if (compiler_unexpected(!vgic_route_allowed(vic, vcpu, dstate))) {
		ret = VGIC_ROUTE_DENIED;
	} else if (compiler_expected(vcpu == current)) {
#if VGIC_HAS_1N_PRIORITY_CHECK
		// Check whether any of the LRs are valid and higher priority.
		//
		// This is closest to the documented behaviour of the GIC-700,
		// but it is fairly expensive to do in software.
		//
		// Note that we can't just check whether the VCPU has IRQs
		// masked in PSTATE, because the Linux idle thread executes WFI
		// with interrupts masked.
		uint8_t new_priority =
			vgic_delivery_state_get_priority(&dstate);
		uint8_t current_priority = GIC_PRIORITY_LOWEST;
		for (index_t i = 0; i < CPU_GICH_LR_COUNT; i++) {
			vgic_lr_status_t *status = &vcpu->vgic_lrs[i];
			if (status->dstate == NULL) {
				continue;
			}
			vgic_read_lr_state(i);
			if (ICH_LR_EL2_base_get_State(&status->lr.base) ==
			    ICH_LR_EL2_STATE_INVALID) {
				continue;
			}
			// We could also check BPR if the LR is in
			// active state, but that is rarely used and
			// probably not worthwhile.
			current_priority = util_min(
				current_priority,
				ICH_LR_EL2_base_get_Priority(&status->lr.base));
		}
		ret = (new_priority < current_priority) ? VGIC_ROUTE_IMMEDIATE
							: VGIC_ROUTE_BUSY;
#else
		ret = VGIC_ROUTE_IMMEDIATE;
#endif
	} else if (cpulocal_get_index() !=
		   scheduler_get_active_affinity(vcpu)) {
		ret = vcpu_expects_wakeup(vcpu) ? VGIC_ROUTE_REMOTE
						: VGIC_ROUTE_REMOTE_BUSY;
	} else if (vcpu_expects_wakeup(vcpu) &&
		   scheduler_will_preempt_current(vcpu)) {
		ret = VGIC_ROUTE_IMMEDIATE;
	} else {
		ret = VGIC_ROUTE_PREEMPTED;
	}

	return ret;
}

static bool
vgic_retry_unrouted_virq(vic_t *vic, virq_t virq);

// Attempt to wake a VCPU to handle a 1-of-N SPI.
//
// This should be called after flagging a 1-of-N SPI as unrouted.
static void
vgic_wakeup_1n(vic_t *vic, virq_t virq, bool class0, bool class1)
{
	// Check whether 1-of-N wakeups are permitted by the VM.
	GICD_CTLR_DS_t gicd_ctlr = atomic_load_relaxed(&vic->gicd_ctlr);
	if (!GICD_CTLR_DS_get_E1NWF(&gicd_ctlr)) {
		VGIC_DEBUG_TRACE(ROUTE, vic, NULL, "wakeup-1n {:d}: disabled",
				 virq);
		goto out;
	}

	// Ensure that the sleep state checks are ordered after the IRQs are
	// flagged as unrouted. There is a matching fence between entering sleep
	// state and checking for unrouted VIRQs in vgic_gicr_rd_set_sleep().
	atomic_thread_fence(memory_order_seq_cst);

	// Find a VCPU that has its GICR in sleep state.
	//
	// Per section 11.1 of the GICv3 spec, we are allowed to wake any
	// arbitrary VCPU and assume that it will eventually handle the
	// interrupt. We don't need to monitor whether that has happened.
	//
	// We always start this search from the VCPU corresponding to the
	// current physical CPU, to reduce the chances of waking a second
	// physical CPU if the GIC has just chosen to wake this one.
	count_t start_point = cpulocal_check_index(cpulocal_get_index_unsafe());
	for (index_t i = 0U; i < vic->gicr_count; i++) {
		cpu_index_t cpu =
			(cpu_index_t)((i + start_point) % vic->gicr_count);
		thread_t *candidate =
			atomic_load_consume(&vic->gicr_vcpus[cpu]);
		if ((candidate == NULL) ||
		    ((platform_irq_cpu_class(cpu) == 0U) ? !class0 : !class1)) {
			// VCPU is missing, or IRQ is not enabled for this
			// VCPU's class
			continue;
		}
		vgic_sleep_state_t sleep_state =
			atomic_load_relaxed(&candidate->vgic_sleep);
		while (sleep_state == VGIC_SLEEP_STATE_ASLEEP) {
			if (atomic_compare_exchange_weak_explicit(
				    &candidate->vgic_sleep, &sleep_state,
				    VGIC_SLEEP_STATE_WAKEUP_1N,
				    memory_order_acquire,
				    memory_order_acquire)) {
				VGIC_DEBUG_TRACE(
					ROUTE, vic, candidate,
					"wakeup-1n {:d}: waking GICR {:d}",
					virq, candidate->vgic_gicr_index);
				scheduler_lock(candidate);
				vcpu_wakeup(candidate);
				scheduler_unlock(candidate);
				goto out;
			}
		}
		if (sleep_state == VGIC_SLEEP_STATE_WAKEUP_1N) {
			VGIC_TRACE(ROUTE, vic, NULL,
				   "wakeup-1n {:d}: GICR {:d} already waking",
				   virq, candidate->vgic_gicr_index);
			goto out;
		}
	}

	// If the VIRQ's classes have no sleeping VCPUs but also no VCPUs that
	// are currently valid targets, we must consider two possibilities:
	// at least one VCPU is concurrently in its resume path, or all VCPUs
	// are concurrently in their suspend paths or hotplugged.
	//
	// The first case, which is much more likely, has a race in which the
	// following sequence might occur:
	//
	//   1. Core A tries to route VIRQ, fails due to disabled group
	//   2. Core B enables group
	//   3. Core B checks for unrouted IRQs, finds none
	//   4. Core A marks VIRQ as unrouted, then calls this function
	//
	// To avoid leaving the VIRQ unrouted in this case, we retry routing.
	if (!vgic_retry_unrouted_virq(vic, virq)) {
		VGIC_TRACE(ROUTE, vic, NULL, "wakeup-1n {:d}: already woken",
			   virq);
		goto out;
	}

	// If the retry didn't work, then either there is a VCPU in its wakeup
	// path that has not enabled its IRQ groups yet, or else all VCPUs are
	// in their suspend paths and have not enabled sleep yet. We retry all
	// unrouted IRQs when enabling either IRQ groups or sleep, so there's
	// nothing more to do here.
	VGIC_TRACE(ROUTE, vic, NULL, "wakeup-1n {:d}: failed", virq);

out:
	(void)0;
}
#endif

// Choose a VCPU to receive an interrupt, given its delivery state.
//
// For 1-of-N delivery, if the use_local_vcpu argument is set, we check the VCPU
// for the local physical CPU first. Otherwise, we use round-robin to select the
// first VCPU to check. This option is typically set for hardware IRQ
// deliveries, and clear otherwise.
//
// This may return NULL if there is no suitable route. It must be called from an
// RCU critical section.
thread_t *
vgic_get_route_from_state(vic_t *vic, vgic_delivery_state_t dstate,
			  bool use_local_vcpu)
{
#if VGIC_HAS_1N
	thread_t *target = NULL;

	// If not 1-of-N, find and return the direct target.
	if (compiler_expected(!vgic_delivery_state_get_route_1n(&dstate))) {
		index_t route_index = vgic_delivery_state_get_route(&dstate);
		if (route_index < vic->gicr_count) {
			target = atomic_load_consume(
				&vic->gicr_vcpus[route_index]);
		}
		goto out;
	}

	count_t start_point;
	if (use_local_vcpu) {
		// Assuming that any VM receiving physical 1-of-N IRQs has a
		// 1:1 VCPU to PCPU mapping, start by checking the local VCPU.
		start_point = cpulocal_check_index(cpulocal_get_index_unsafe());
	} else {
		// Determine the starting point for VIRQ selection using
		// round-robin, if we didn't get a hint from the physical GIC.
		start_point = atomic_fetch_add_explicit(
			&vic->rr_start_point, 1U, memory_order_relaxed);
	}

	// Ensure that i + start_point doesn't overflow below, because
	// we might fail to check all VCPUs in that case.
	start_point %= vic->gicr_count;

	// Look for the best target.
	vgic_route_preference_t target_pref = VGIC_ROUTE_DENIED;
	for (index_t i = 0U; i < vic->gicr_count; i++) {
		thread_t *candidate = atomic_load_consume(
			&vic->gicr_vcpus[(i + start_point) % vic->gicr_count]);
		if (candidate == NULL) {
			continue;
		}
		scheduler_lock(candidate);
		vgic_route_preference_t candidate_pref =
			vgic_route_1n_preference(vic, candidate, dstate);
		scheduler_unlock(candidate);
		if (compiler_expected(candidate_pref == VGIC_ROUTE_IMMEDIATE)) {
			target = candidate;
			VGIC_DEBUG_TRACE(ROUTE, vic, target,
					 "route: {:d} immediate, checked {:d}",
					 target->vgic_gicr_index,
					 ((register_t)i + 1U));
			goto out;
		}
		if (candidate_pref > target_pref) {
			target	    = candidate;
			target_pref = candidate_pref;
		}
	}

	// If we found a valid target, return it.
	//
	// This should be unconditional, and everything beyond this point
	// should be moved to after the VIRQ has been flagged as unrouted.
	//
	// FIXME:
	if (compiler_expected(target != NULL)) {
		VGIC_DEBUG_TRACE(ROUTE, vic, target, "route: {:d} best ({:d})",
				 target->vgic_gicr_index,
				 (uint64_t)target_pref);
		goto out;
	}

	GICD_CTLR_DS_t gicd_ctlr     = atomic_load_relaxed(&vic->gicd_ctlr);
	bool	       trace_IsE1NWF = GICD_CTLR_DS_get_E1NWF(&gicd_ctlr);
	VGIC_TRACE(ROUTE, vic, target, "route: none (E1NWF={:d})",
		   (register_t)trace_IsE1NWF);

out:
	return target;
#else
	(void)use_local_vcpu;
	index_t route_index = vgic_delivery_state_get_route(&dstate);
	return (route_index < vic->gicr_count)
		       ? atomic_load_consume(&vic->gicr_vcpus[route_index])
		       : NULL;
#endif
}

// Choose a VCPU to receive an SPI, given its IRQ number.
//
// This may return NULL if there is no suitable route. It must be called from an
// RCU critical section.
thread_t *
vgic_get_route_for_spi(vic_t *vic, virq_t virq, bool use_local_vcpu)
{
	assert(vgic_irq_is_spi(virq));
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, NULL, virq);
	return vgic_get_route_from_state(vic, atomic_load_relaxed(dstate),
					 use_local_vcpu);
}

// Choose a VCPU to receive an unlisted interrupt, mark it pending, and trigger
// a wakeup.
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
// 5. a pending 1-of-N routed VIRQ loses a race to be delivered to a VCPU
//    before it disables the relevant group, and needs to be rerouted
//
// In most of these cases, we need to check the current route register and
// priority register for the interrupt, and reroute it based on those values.
static bool
vgic_try_route_and_flag(vic_t *vic, virq_t virq,
			vgic_delivery_state_t new_dstate, bool use_local_vcpu)
	REQUIRE_PREEMPT_DISABLED
{
	rcu_read_start();
	thread_t *target =
		vgic_get_route_from_state(vic, new_dstate, use_local_vcpu);

	if (target != NULL) {
		uint8_t priority =
			vgic_delivery_state_get_priority(&new_dstate);
		vgic_flag_unlocked(virq, target, priority);
	}

	rcu_read_finish();

	return (target != NULL);
}

// Wrapper for vgic_try_route_and_flag() that flags the VIRQ as unrouted on
// failure, and triggers a 1-of-N wakeup.
static void
vgic_route_and_flag(vic_t *vic, virq_t virq, vgic_delivery_state_t new_dstate,
		    bool use_local_vcpu) REQUIRE_PREEMPT_DISABLED
{
	if (!vgic_try_route_and_flag(vic, virq, new_dstate, use_local_vcpu)) {
		vgic_flag_unrouted(vic, virq);
#if VGIC_HAS_1N
		vgic_wakeup_1n(vic, virq,
			       vgic_get_delivery_state_is_class0(&new_dstate),
			       vgic_get_delivery_state_is_class1(&new_dstate));
#endif
	}
}

static vgic_delivery_state_t
vgic_reclaim_update_level_src_and_hw(const vic_t *vic, const thread_t *vcpu,
				     virq_t		    virq,
				     vgic_delivery_state_t *old_dstate,
				     bool lr_active, bool lr_hw,
				     vgic_lr_status_t *status,
				     virq_source_t    *source)
	REQUIRE_PREEMPT_DISABLED
{
	bool lr_pending = (ICH_LR_EL2_base_get_State(&status->lr.base) ==
			   ICH_LR_EL2_STATE_PENDING) ||
			  (ICH_LR_EL2_base_get_State(&status->lr.base) ==
			   ICH_LR_EL2_STATE_PENDING_ACTIVE);
	vgic_delivery_state_t new_dstate;
	bool		      need_deactivate;

	// We should never try to reclaim an LR that has a pending EOI trap;
	// it isn't handled correctly below, and needs vgic_sync_lr().
	assert(lr_pending || lr_active ||
	       ICH_LR_EL2_base_get_HW(&status->lr.base) ||
	       !ICH_LR_EL2_HW0_get_EOI(&status->lr.sw));

	do {
		new_dstate	= *old_dstate;
		need_deactivate = false;

		vgic_delivery_state_set_active(&new_dstate, lr_active);
		vgic_delivery_state_set_listed(&new_dstate, false);
		vgic_delivery_state_set_need_sync(&new_dstate, false);
		vgic_delivery_state_set_hw_detached(&new_dstate, false);
		if (lr_pending) {
			vgic_delivery_state_set_edge(&new_dstate, true);
		}

		// Update level_src and hw_active based on the LR state.
		if (lr_hw && vgic_delivery_state_get_hw_active(old_dstate)) {
			// If it's a hardware IRQ that has already been marked
			// active somewhere else, we don't need to change its
			// state beyond the abave. For this to happen, it must
			// have been inactive in the LR already.
			assert(!lr_pending && !lr_active);
		} else if (lr_hw && lr_pending &&
			   vgic_delivery_state_get_need_sync(old_dstate) &&
			   !vgic_delivery_state_get_cfg_is_edge(old_dstate)) {
			// If it's a pending hardware level-triggered interrupt
			// that has been marked for sync, we clear its pending
			// state and deactivate it early to force the hardware
			// to re-check it (and possibly re-route it in 1-of-N
			// mode).
			vgic_delivery_state_set_level_src(&new_dstate, false);
			need_deactivate = true;
		} else if (lr_hw && (lr_pending || lr_active)) {
			// If it's a pending or active hardware IRQ, we must
			// re-set hw_active, and clear level_src if it has
			// been acknowledged.
			vgic_delivery_state_set_hw_active(&new_dstate, true);
			if (!lr_pending) {
				vgic_delivery_state_set_level_src(&new_dstate,
								  false);
			}
		} else if (lr_hw) {
			// If it's a hardware IRQ that was deactivated directly,
			// reset level_src to the old hw_active (which preserves
			// any remote assertion).
			vgic_delivery_state_set_level_src(
				&new_dstate,
				vgic_delivery_state_get_hw_active(old_dstate));
		} else if (vgic_delivery_state_get_level_src(old_dstate)) {
			// It's a software IRQ with level_src set; call the
			// source to check whether it's still pending, and order
			// the check_pending event after the dstate read.
			bool reassert =
				lr_pending ||
				vgic_delivery_state_get_edge(old_dstate);
			if (!vgic_virq_check_pending(source, reassert)) {
				vgic_delivery_state_set_level_src(&new_dstate,
								  false);
			}
		} else {
			// Software IRQ with level_src clear; nothing to do.
		}
	} while (!atomic_compare_exchange_strong_explicit(
		status->dstate, old_dstate, new_dstate, memory_order_relaxed,
		memory_order_relaxed));

	VGIC_TRACE(DSTATE_CHANGED, vic, vcpu, "reclaim_lr {:d}: {:#x} -> {:#x}",
		   virq, vgic_delivery_state_raw(*old_dstate),
		   vgic_delivery_state_raw(new_dstate));

	if (need_deactivate) {
		VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
			   "reclaim_lr {:d}: deactivate HW IRQ {:d}",
			   ICH_LR_EL2_HW1_get_vINTID(&status->lr.hw),
			   ICH_LR_EL2_HW1_get_pINTID(&status->lr.hw));
		gicv3_irq_deactivate(ICH_LR_EL2_HW1_get_pINTID(&status->lr.hw));
	}

	return new_dstate;
}

// Clear out a VIRQ from a specified LR and flag it to be delivered later.
//
// This is used when there are no empty LRs available to deliver an IRQ, but
// an LR is occupied by an IRQ that is either lower-priority, or already
// acknowledged, or (in the current thread) already deactivated. It is also
// used when tearing down a VCPU permanently, so active IRQs can't be left
// in the LRs as they are for a normal group disable. In the latter case, the
// reroute argument should be true, to force the route to be recalculated.
//
// The specified VCPU must either be the current thread, or LR-locked by the
// caller and known not to be running remotely. If the specified VCPU is the
// current thread, the caller must rewrite the LR after calling this function.
//
// The specified LR must be occupied. If it contains an active interrupt
// (regardless of its pending state), it must be the lowest-priority listed
// active interrupt on the VCPU, to ensure that the active_unlisted stack is
// correctly ordered.
static void
vgic_reclaim_lr(vic_t *vic, thread_t *vcpu, index_t lr, bool reroute)
	REQUIRE_LOCK(vcpu->vgic_lr_owner_lock) REQUIRE_PREEMPT_DISABLED
{
	const bool	  from_self = vcpu == thread_get_self();
	vgic_lr_status_t *status    = &vcpu->vgic_lrs[lr];
	assert(status->dstate != NULL);

	if (from_self) {
		vgic_read_lr_state(lr);
	}

	virq_t virq	 = ICH_LR_EL2_base_get_vINTID(&status->lr.base);
	bool   lr_hw	 = ICH_LR_EL2_base_get_HW(&status->lr.base);
	bool   lr_active = (ICH_LR_EL2_base_get_State(&status->lr.base) ==
			    ICH_LR_EL2_STATE_ACTIVE) ||
			 (ICH_LR_EL2_base_get_State(&status->lr.base) ==
			  ICH_LR_EL2_STATE_PENDING_ACTIVE);

#if VGIC_HAS_LPI && GICV3_HAS_VLPI_V4_1
	if (vic->vsgis_enabled &&
	    (vgic_get_irq_type(virq) == VGIC_IRQ_TYPE_SGI)) {
		// vSGIs have no active state.
		lr_active = false;
	}
#endif

	if (lr_active) {
		index_t i = vcpu->vgic_active_unlisted_count % VGIC_PRIORITIES;
		vcpu->vgic_active_unlisted[i] = virq;
		vcpu->vgic_active_unlisted_count++;
	}

	virq_source_t	     *source	 = vgic_find_source(vic, vcpu, virq);
	vgic_delivery_state_t old_dstate = atomic_load_relaxed(status->dstate);

	vgic_delivery_state_t new_dstate = vgic_reclaim_update_level_src_and_hw(
		vic, vcpu, virq, &old_dstate, lr_active, lr_hw, status, source);

#if VGIC_HAS_1N
	if (vgic_delivery_state_get_route_1n(&new_dstate)) {
		vgic_spi_reset_route_1n(source, new_dstate);
	}
#endif

	// The LR is no longer in use; clear out the status structure.
	status->dstate	= NULL;
	status->lr.base = ICH_LR_EL2_base_default();

	// Determine how this IRQ will be delivered, if necessary.
	if (vgic_delivery_state_get_enabled(&new_dstate) &&
	    vgic_delivery_state_is_pending(&new_dstate) &&
	    !vgic_delivery_state_get_active(&new_dstate)) {
		if (reroute || vgic_delivery_state_get_need_sync(&old_dstate)) {
			vgic_route_and_flag(vic, virq, new_dstate, false);
		} else {
			// Note: CPU_INDEX_INVALID because this VCPU is always
			// either current or not running.
			vgic_flag_locked(
				virq, vcpu,
				vgic_delivery_state_get_priority(&new_dstate),
				vgic_delivery_state_get_group1(&new_dstate),
				CPU_INDEX_INVALID);
		}
	}
}

static bool
vgic_sync_vcpu(thread_t *vcpu, bool hw_access);

static void
vgic_list_irq(vgic_delivery_state_t new_dstate, index_t lr, bool is_hw,
	      uint8_t priority, _Atomic vgic_delivery_state_t *dstate,
	      virq_t virq, vic_t *vic, thread_t *vcpu, virq_source_t *source,
	      bool to_self) REQUIRE_LOCK(vcpu->vgic_lr_owner_lock)
	REQUIRE_PREEMPT_DISABLED
{
	assert(vgic_delivery_state_get_listed(&new_dstate));
	assert_debug(lr < CPU_GICH_LR_COUNT);

	vgic_lr_status_t *status = &vcpu->vgic_lrs[lr];
	if (status->dstate != NULL) {
		vgic_reclaim_lr(vic, vcpu, lr, false);
		assert(status->dstate == NULL);
	}

#if VGIC_HAS_1N
	if (vgic_delivery_state_get_route_1n(&new_dstate) && (source != NULL) &&
	    (source->trigger == VIRQ_TRIGGER_VGIC_FORWARDED_SPI)) {
		// Set the HW IRQ's route to the VCPU's current physical core
		hwirq_t *hwirq = hwirq_from_virq_source(source);
		(void)gicv3_spi_set_route(hwirq->irq, vcpu->vgic_irouter);
	}
#endif

	status->dstate = dstate;
	ICH_LR_EL2_base_set_HW(&status->lr.base, is_hw);
	if (is_hw) {
		ICH_LR_EL2_HW1_set_pINTID(&status->lr.hw,
					  hwirq_from_virq_source(source)->irq);
	} else {
		ICH_LR_EL2_HW0_set_EOI(
			&status->lr.sw,
			!vgic_delivery_state_get_cfg_is_edge(&new_dstate) &&
				vgic_delivery_state_is_level_asserted(
					&new_dstate));
	}
	ICH_LR_EL2_base_set_vINTID(&status->lr.base, virq);
	ICH_LR_EL2_base_set_Priority(&status->lr.base, priority);
	ICH_LR_EL2_base_set_Group(&status->lr.base,
				  vgic_delivery_state_get_group1(&new_dstate));
	ICH_LR_EL2_base_set_State(&status->lr.base, ICH_LR_EL2_STATE_PENDING);

	if (to_self) {
		vgic_write_lr(lr);
	}
}

typedef struct {
	bool need_wakeup;
	bool need_sync_all;
} vgic_deliver_list_or_flag_info_t;

static vgic_deliver_list_or_flag_info_t
vgic_deliver_list_or_flag(vic_t *vic, thread_t *vcpu, virq_source_t *source,
			  vgic_delivery_state_t old_dstate,
			  vgic_delivery_state_t new_dstate, index_result_t lr_r,
			  _Atomic vgic_delivery_state_t *dstate, virq_t virq,
			  cpu_index_t remote_cpu, uint8_t lr_priority,
			  bool is_private, bool to_self, bool is_hw,
			  uint8_t priority, bool pending, bool enabled,
			  bool route_valid)
	REQUIRE_LOCK(vcpu->vgic_lr_owner_lock) REQUIRE_PREEMPT_DISABLED
{
	bool need_wakeup   = true;
	bool need_sync_all = false;

	assert(vcpu != NULL);
	thread_t *target = vcpu;

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
			TRACE(VGIC, INFO,
			      "vgic sync after failed redeliver of {:#x}: dstate {:#x} -> {:#x}",
			      virq, vgic_delivery_state_raw(old_dstate),
			      vgic_delivery_state_raw(new_dstate));

			(void)vgic_sync_vcpu(target, to_self);
		}
	} else if (!enabled) {
		// Not enabled; nothing more to do.
		need_wakeup = false;
	} else if (!route_valid) {
		// The route became invalid after it was selected. Try to
		// re-route and flag it, and if that fails, flag it as unrouted.
		// This function issues a wakeup, so we don't need to do it
		// below.
		vgic_route_and_flag(vic, virq, new_dstate, false);
		need_wakeup = false;
	} else if ((lr_r.e == OK) && (priority < lr_priority)) {
		// List the IRQ immediately.
		vgic_list_irq(new_dstate, lr_r.r, is_hw, priority, dstate, virq,
			      vic, vcpu, source, to_self);
	} else {
		assert(route_valid);
		// We have a valid route, but can't immediately list; set the
		// search flags in the target VCPU so it finds this VIRQ next
		// time it goes looking for something to deliver. A delivery IPI
		// is sent if the target is currently running.
		vgic_flag_locked(virq, target, priority,
				 vgic_delivery_state_get_group1(&new_dstate),
				 remote_cpu);
	}

	return (vgic_deliver_list_or_flag_info_t){
		.need_wakeup   = need_wakeup,
		.need_sync_all = need_sync_all,
	};
}

typedef struct {
	vgic_delivery_state_t new_dstate;
	vgic_delivery_state_t old_dstate;
	bool		      need_wakeup;
	bool		      need_sync_all;
	uint8_t		      pad[2];
} vgic_deliver_info_t;

static vgic_deliver_info_t
vgic_deliver_update_state(virq_t virq, vgic_delivery_state_t prev_dstate,
			  vgic_delivery_state_t		 assert_dstate,
			  _Atomic vgic_delivery_state_t *dstate, vic_t *vic,
			  thread_t *vcpu, cpu_index_t remote_cpu,
			  virq_source_t *source, bool is_private, bool to_self)
	REQUIRE_LOCK(vcpu->vgic_lr_owner_lock) REQUIRE_PREEMPT_DISABLED
{
	// Keep track of the LR allocated for delivery (if any) and the priority
	// of the VIRQ currently in it (if any).
	index_result_t lr_r = index_result_error(ERROR_BUSY);
	uint8_t	       priority;
	uint8_t	       lr_priority	= GIC_PRIORITY_LOWEST;
	uint8_t	       checked_priority = GIC_PRIORITY_LOWEST;
	bool	       pending;
	bool	       enabled;
	bool	       route_valid;
	bool	       is_hw;

	// Clarify for the static analyser that we have not allocated an LR yet
	// at this point.
	assert(lr_r.e != OK);

	vgic_delivery_state_t new_dstate;
	vgic_delivery_state_t old_dstate    = prev_dstate;
	bool		      need_wakeup   = true;
	bool		      need_sync_all = false;

	do {
		new_dstate =
			vgic_delivery_state_union(old_dstate, assert_dstate);
		is_hw	 = vgic_delivery_state_get_hw_active(&new_dstate);
		priority = vgic_delivery_state_get_priority(&new_dstate);

		pending	    = vgic_delivery_state_is_pending(&new_dstate);
		enabled	    = vgic_delivery_state_get_enabled(&new_dstate);
		route_valid = (vcpu != NULL) &&
			      vgic_route_allowed(vic, vcpu, new_dstate);

		if (vgic_delivery_state_get_listed(&old_dstate)) {
			// Already listed (and not redelivered locally, above);
			// just request a sync.
			vgic_delivery_state_set_need_sync(&new_dstate, true);
			continue;
		}

		if (!route_valid || !pending || !enabled ||
		    vgic_delivery_state_get_active(&old_dstate)) {
			// Can't deliver; just update the delivery state.
			continue;
		}

		// Try to allocate an LR, unless we have already done so at a
		// priority no lower than the current one.
		if ((lr_r.e != OK) && (priority < checked_priority) &&
		    !cpulocal_index_valid(remote_cpu)) {
			lr_r = vgic_select_lr(vcpu, priority, &lr_priority);
			checked_priority = priority;
		}

		if ((lr_r.e == OK) && (priority < lr_priority)) {
			// We're newly listing the IRQ.
			vgic_delivery_state_set_listed(&new_dstate, true);
			vgic_delivery_state_set_edge(&new_dstate, false);
			vgic_delivery_state_set_hw_active(&new_dstate, false);
		}
	} while (!atomic_compare_exchange_strong_explicit(
		dstate, &old_dstate, new_dstate, memory_order_relaxed,
		memory_order_relaxed));

	VGIC_TRACE(DSTATE_CHANGED, vic, vcpu, "deliver {:d}: {:#x} -> {:#x}",
		   virq, vgic_delivery_state_raw(old_dstate),
		   vgic_delivery_state_raw(new_dstate));

	if (vcpu == NULL) {
		// VIRQ is unrouted. Flag it in the shared search bitmap.
		if (pending && enabled) {
			vgic_flag_unrouted(vic, virq);
#if VGIC_HAS_1N
			// If this is a 1-of-N VIRQ, we might need to pick a
			// VCPU to wake (if E1NWF is enabled).
			need_wakeup =
				vgic_delivery_state_get_route_1n(&new_dstate);
#else
			// There is no VCPU to wake.
			need_wakeup = false;
#endif
		} else {
			need_wakeup = false;
		}

		goto out;
	}

	vgic_deliver_list_or_flag_info_t info = vgic_deliver_list_or_flag(
		vic, vcpu, source, old_dstate, new_dstate, lr_r, dstate, virq,
		remote_cpu, lr_priority, is_private, to_self, is_hw, priority,
		pending, enabled, route_valid);

	need_wakeup   = info.need_wakeup;
	need_sync_all = info.need_sync_all;

out:
	return (vgic_deliver_info_t){
		.new_dstate    = new_dstate,
		.old_dstate    = old_dstate,
		.need_wakeup   = need_wakeup,
		.need_sync_all = need_sync_all,
	};
}

static void
vgic_deliver_update_spi_route(vgic_delivery_state_t old_dstate,
			      const vic_t *vic, const thread_t *vcpu,
			      cpu_index_t remote_cpu, virq_source_t *source)
{
#if !VGIC_HAS_1N
	(void)old_dstate;
#endif

	if ((source == NULL) ||
	    (source->trigger != VIRQ_TRIGGER_VGIC_FORWARDED_SPI)) {
		// Not a HW IRQ; don't try to update the route.
	}
#if VGIC_HAS_1N
	else if (vgic_delivery_state_get_route_1n(&old_dstate)) {
		// IRQ doesn't have a fixed route, so there is no need to update
		// it here. Note that we may update it later when it is listed.
	}
#endif
	else if (cpulocal_index_valid(remote_cpu)) {
		assert(vcpu != NULL);
		// HW IRQ was delivered on the wrong CPU, probably because the
		// VCPU was migrated. Update the route. Note that we don't need
		// to disable / enable the IRQ or execute any waits or barriers
		// here because we are tolerant of further misrouting.
		hwirq_t *hwirq = hwirq_from_virq_source(source);
		(void)gicv3_spi_set_route(hwirq->irq, vcpu->vgic_irouter);

		VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
			   "lazy reroute {:d}: to cpu {:d}", hwirq->irq,
			   remote_cpu);
	} else {
		// Directly routed to the correct CPU or not routed to any CPU
		// yet; nothing to do.
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
// If the level_src pending bit or the hw_active bit is being set, the VIRQ
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
	     vgic_delivery_state_t assert_dstate, bool is_private)
{
	bool to_self	   = vcpu == thread_get_self();
	bool need_wakeup   = true;
	bool need_sync_all = false;

	assert((source != NULL) ||
	       !vgic_delivery_state_get_level_src(&assert_dstate));
	assert((source == NULL) ||
	       (vgic_get_irq_type(source->virq) == VGIC_IRQ_TYPE_PPI) ||
	       (vgic_get_irq_type(source->virq) == VGIC_IRQ_TYPE_SPI));

	cpu_index_t remote_cpu = vgic_lr_owner_lock(vcpu);

	vgic_delivery_state_t old_dstate = atomic_load_relaxed(dstate);
	vgic_delivery_state_t new_dstate =
		vgic_delivery_state_union(old_dstate, assert_dstate);

	if (vgic_delivery_state_get_listed(&old_dstate) &&
	    vgic_delivery_state_is_pending(&new_dstate) &&
	    vgic_delivery_state_get_enabled(&new_dstate) && (vcpu != NULL) &&
	    !cpulocal_index_valid(remote_cpu)) {
		// Fast path: try to reset the pending state in the LR. This can
		// fail if the LR is not found, e.g. because the routing has
		// changed. Note that this function updates dstate if it
		// succeeds, so we can skip the updates below.
		//
		// We don't check the route, priority or group enables here
		// because listed IRQs affected by changes in those since they
		// were first listed either don't need an immediate update, or
		// else will be updated by whoever is changing them.
		//
		// We only need to try this once, because the listed bit can't
		// be changed by anyone else while we're holding the LR lock.
		bool_result_t redeliver_wakeup = vgic_redeliver(
			vic, vcpu, source, dstate, &old_dstate, assert_dstate);
		if (redeliver_wakeup.e == OK) {
			need_wakeup = redeliver_wakeup.r;
			goto out;
		}
	}

	// If this is a physical SPI assertion, we may need to update the route
	// of the physical SPI.
	vgic_deliver_update_spi_route(old_dstate, vic, vcpu, remote_cpu,
				      source);

	// Update the dstate and deliver the interrupt
	vgic_deliver_info_t vgic_deliver_info = vgic_deliver_update_state(
		virq, old_dstate, assert_dstate, dstate, vic, vcpu, remote_cpu,
		source, is_private, to_self);

	new_dstate    = vgic_deliver_info.new_dstate;
	old_dstate    = vgic_deliver_info.old_dstate;
	need_wakeup   = vgic_deliver_info.need_wakeup;
	need_sync_all = vgic_deliver_info.need_sync_all;

out:
	vgic_lr_owner_unlock(vcpu);

	if (need_wakeup) {
		if (to_self) {
			vcpu_wakeup_self();
		} else if (vcpu != NULL) {
			scheduler_lock(vcpu);
			vcpu_wakeup(vcpu);
			scheduler_unlock(vcpu);
		} else {
#if VGIC_HAS_1N
			vgic_wakeup_1n(
				vic, virq,
				vgic_get_delivery_state_is_class0(&new_dstate),
				vgic_get_delivery_state_is_class1(&new_dstate));
#else
			// VIRQ is unrouted; there is no VCPU we can wake.
			assert(!need_wakeup);
#endif
		}
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
			wakeup = vgic_sync_vcpu(vcpu, true) || wakeup;
			if (wakeup) {
				vcpu_wakeup_self();
			}
		} else if (vcpu != NULL) {
			cpu_index_t lr_owner = vgic_lr_owner_lock(vcpu);
			if (!vcpu->vgic_group0_enabled &&
			    !vcpu->vgic_group1_enabled) {
				// Nothing should be listed on this CPU, so we
				// don't need to sync it.
			} else {
				if (cpulocal_index_valid(lr_owner)) {
					ipi_one(IPI_REASON_VGIC_SYNC, lr_owner);
				} else {
					wakeup = vgic_sync_vcpu(vcpu, false) ||
						 wakeup;
				}
			}
			vgic_lr_owner_unlock(vcpu);
			if (wakeup) {
				scheduler_lock(vcpu);
				vcpu_wakeup(vcpu);
				scheduler_unlock(vcpu);
			}
		} else {
			// No VCPU attached at this index, nothing to do
		}
	}

	rcu_read_finish();
}

static bool
vgic_gicr_update_group_enables(vic_t *vic, thread_t *gicr_vcpu,
			       GICD_CTLR_DS_t gicd_ctlr)
	REQUIRE_LOCK(gicr_vcpu->vgic_lr_owner_lock) REQUIRE_PREEMPT_DISABLED;

void
vgic_update_enables(vic_t *vic, GICD_CTLR_DS_t gicd_ctlr)
{
	preempt_disable();
	rcu_read_start();

	for (index_t i = 0; i < vic->gicr_count; i++) {
		thread_t   *vcpu     = atomic_load_consume(&vic->gicr_vcpus[i]);
		cpu_index_t lr_owner = vgic_lr_owner_lock_nopreempt(vcpu);
		if (thread_get_self() == vcpu) {
			if (vgic_gicr_update_group_enables(vic, vcpu,
							   gicd_ctlr)) {
				vcpu_wakeup_self();
			}
			vgic_lr_owner_unlock_nopreempt(vcpu);
		} else if (vcpu != NULL) {
			bool wakeup = false;
			if (cpulocal_index_valid(lr_owner)) {
				ipi_one(IPI_REASON_VGIC_ENABLE, lr_owner);
			} else {
				wakeup = vgic_gicr_update_group_enables(
					vic, vcpu, gicd_ctlr);
			}
			vgic_lr_owner_unlock_nopreempt(vcpu);
			if (wakeup) {
				scheduler_lock_nopreempt(vcpu);
				vcpu_wakeup(vcpu);
				scheduler_unlock_nopreempt(vcpu);
			}
		} else {
			// No VCPU attached at this index, nothing to do
			vgic_lr_owner_unlock_nopreempt(vcpu);
		}
	}

	rcu_read_finish();
	preempt_enable();
}

error_t
virq_clear(virq_source_t *source)
{
	error_t			       err    = ERROR_VIRQ_NOT_BOUND;
	_Atomic vgic_delivery_state_t *dstate = NULL;

	// The source's VIC and VCPU pointers are RCU-protected.
	rcu_read_start();

	// We must have a VIC to clear from (note that a disconnected source is
	// always considered clear).
	vic_t *vic = atomic_load_acquire(&source->vic);
	if (compiler_unexpected(vic == NULL)) {
		goto out;
	}

	// Try to find the current target VCPU. This may be inaccurate or NULL
	// for a shared IRQ, but must be correct for a private IRQ.
	thread_t *vcpu = vgic_find_target(vic, source);
	if (compiler_unexpected(vcpu == NULL) && source->is_private) {
		// The VIRQ has been concurrently unbound.
		goto out;
	}

	// At this point we can't fail.
	err = OK;

	// Clear the level_src bit in the delivery state.
	vgic_delivery_state_t clear_dstate = vgic_delivery_state_default();
	vgic_delivery_state_set_level_src(&clear_dstate, true);
	vgic_delivery_state_set_hw_active(&clear_dstate, true);

	dstate = vgic_find_dstate(vic, vcpu, source->virq);
	(void)vgic_undeliver(vic, vcpu, dstate, source->virq, clear_dstate,
			     false);

	// We ignore the result of vgic_undeliver() here, which increases the
	// chances that the VM will receive a spurious IRQ, on the basis that
	// it's cheaper to handle a spurious IRQ than to broadcast a sync that
	// may or may not succeed in preventing it. A caller that really cares
	// about this should be using a check-pending event.

out:
	rcu_read_finish();

	return err;
}

bool_result_t
virq_query(virq_source_t *source)
{
	bool_result_t result = bool_result_error(ERROR_VIRQ_NOT_BOUND);

	rcu_read_start();

	if (source == NULL) {
		goto out;
	}

	vic_t *vic = atomic_load_acquire(&source->vic);
	if (compiler_unexpected(vic == NULL)) {
		goto out;
	}

	// If the VIRQ is private, we must find its target VCPU.
	thread_t *vcpu = NULL;
	if (source->is_private) {
		vcpu = vgic_find_target(vic, source);
		if (compiler_unexpected(vcpu == NULL)) {
			goto out;
		}
	}

	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, vcpu, source->virq);
	assert(dstate != NULL);

	vgic_delivery_state_t cur_dstate = atomic_load_relaxed(dstate);
	result = bool_result_ok(vgic_delivery_state_get_level_src(&cur_dstate));
out:
	rcu_read_finish();

	return result;
}

// Handle an EOI maintenance interrupt.
//
// These are enabled for all level-triggered interrupts with non-hardware
// sources; this includes registered VIRQ sources, ISPENDR writes, and SETSPI
// writes. They are also enabled when an edge triggered interrupt is asserted
// by software and hardware sources simultaneously.
//
// The specified VCPU must be the current thread. The specified LR must be in
// the invalid state in hardware, but have a software-asserted VIRQ associated
// with it.
static void
vgic_handle_eoi_lr(vic_t *vic, thread_t *vcpu, index_t lr)
	REQUIRE_PREEMPT_DISABLED
{
	assert(thread_get_self() == vcpu);
	assert_debug(lr < CPU_GICH_LR_COUNT);

	// The specified LR should have a software delivery listed in it
	vgic_lr_status_t *status = &vcpu->vgic_lrs[lr];
	assert(status->dstate != NULL);
	assert(!ICH_LR_EL2_base_get_HW(&status->lr.base));

	vgic_read_lr_state(lr);
	(void)vgic_sync_lr(vic, vcpu, status, vgic_delivery_state_default(),
			   true);
	vgic_write_lr(lr);
}

typedef struct {
	vgic_delivery_state_t new_dstate;
	bool		      need_deactivate;
	bool		      res;
	uint8_t		      pad[2];
} vgic_deactivate_info_t;

static vgic_deactivate_info_t
vgic_do_deactivate(const vic_t *vic, const thread_t *vcpu, virq_t virq,
		   _Atomic vgic_delivery_state_t *dstate,
		   vgic_delivery_state_t old_dstate, bool set_edge,
		   bool hw_active, virq_source_t *source, bool local_listed)
	REQUIRE_PREEMPT_DISABLED
{
	bool		      res = false;
	vgic_delivery_state_t new_dstate;
	bool		      need_deactivate;

	do {
		new_dstate	= old_dstate;
		need_deactivate = false;

		if (local_listed) {
			// Nobody else should delist the IRQ from under us.
			assert(vgic_delivery_state_get_listed(&old_dstate) ==
			       true);
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
				VGIC_TRACE(
					DSTATE_CHANGED, vic, vcpu,
					"deactivate {:d}: already listed {:#x}",
					virq,
					vgic_delivery_state_raw(old_dstate));
				res = true;
				goto out;
			}
			if (!vgic_delivery_state_get_active(&old_dstate)) {
				// Interrupt is already inactive; we have
				// nothing to do.
				VGIC_TRACE(
					DSTATE_CHANGED, vic, vcpu,
					"deactivate {:d}: already inactive {:#x}",
					virq,
					vgic_delivery_state_raw(old_dstate));
				res = true;
				goto out;
			}
			assert(!set_edge && !hw_active);
			vgic_delivery_state_set_active(&new_dstate, false);
		}

		// If the hw_active bit is set but the edge bit is not, we are
		// deactivating an acknowledged hardware interrupt.
		if (hw_active ||
		    (vgic_delivery_state_get_hw_active(&old_dstate) &&
		     !vgic_delivery_state_get_edge(&old_dstate))) {
			need_deactivate = true;
			vgic_delivery_state_set_hw_active(&new_dstate, false);
		}

		// If level_src is set, check that the source is still pending
		// before we try to deliver it.
		if (vgic_delivery_state_get_level_src(&old_dstate)) {
			if (!vgic_virq_check_pending(
				    source, vgic_delivery_state_get_edge(
						    &new_dstate))) {
				vgic_delivery_state_set_level_src(&new_dstate,
								  false);
			}
		}
	} while (!atomic_compare_exchange_strong_explicit(
		dstate, &old_dstate, new_dstate, memory_order_relaxed,
		memory_order_relaxed));

	VGIC_TRACE(DSTATE_CHANGED, vic, vcpu, "deactivate {:d}: {:#x} -> {:#x}",
		   virq, vgic_delivery_state_raw(old_dstate),
		   vgic_delivery_state_raw(new_dstate));

out:
	return (vgic_deactivate_info_t){
		.new_dstate	 = new_dstate,
		.need_deactivate = need_deactivate,
		.res		 = res,
	};
}

// Handle a software deactivate of a specific VIRQ.
//
// This may be called by the DIR trap handler if the VM's EOImode is 1, by the
// LRENP maintenance interrupt handler if the VM's EOImode is 0, or by the
// ICACTIVER trap handler in either case.
//
// If the interrupt is listed, the specified VCPU must be the current VCPU, and
// the list register must be known to be in active or pending+active state. In
// this case, the set_edge parameter determines whether the edge bit will be
// set, and the set_hw_active parameter determines whether the hw_active bit
// will be set.
//
// The specified old_dstate value must have been load-acquired before checking
// the listed bit to decide whether to call this function.
void
vgic_deactivate(vic_t *vic, thread_t *vcpu, virq_t virq,
		_Atomic vgic_delivery_state_t *dstate,
		vgic_delivery_state_t old_dstate, bool set_edge, bool hw_active)
	REQUIRE_PREEMPT_DISABLED
{
	bool local_listed = vgic_delivery_state_get_listed(&old_dstate);
	assert(!local_listed || (thread_get_self() == vcpu));

	// Find the registered source, if any.
	rcu_read_start();
	virq_source_t *source = vgic_find_source(vic, vcpu, virq);

	// Clear active in the delivery state, and level_src too if necessary.
	vgic_delivery_state_t new_dstate;
	bool		      need_deactivate;
	bool		      res = false;
	{
		vgic_deactivate_info_t vgic_deactivate_info =
			vgic_do_deactivate(vic, vcpu, virq, dstate, old_dstate,
					   set_edge, hw_active, source,
					   local_listed);
		res		= vgic_deactivate_info.res;
		new_dstate	= vgic_deactivate_info.new_dstate;
		need_deactivate = vgic_deactivate_info.need_deactivate;
	}
	if (res) {
		goto out;
	}

	// If the interrupt is hardware-sourced then forward the deactivation to
	// the hardware.
	if (need_deactivate) {
		assert((source != NULL) &&
		       (source->trigger == VIRQ_TRIGGER_VGIC_FORWARDED_SPI));
		hwirq_t *hwirq = hwirq_from_virq_source(source);
		VGIC_TRACE(HWSTATE_CHANGED, vic, vcpu,
			   "deactivate {:d}: deactivate HW IRQ {:d}", virq,
			   hwirq->irq);
		irq_deactivate(hwirq);
	}

	// If the interrupt is still pending, deliver it immediately. Note that
	// this can't be HW=1, even if the interrupt we just deactivated was,
	// because the physical IRQ is inactive (above). It might be a software
	// delivery that occurred while the physical source was active.
	if (vgic_delivery_state_is_pending(&new_dstate) &&
	    vgic_delivery_state_get_enabled(&new_dstate)) {
		thread_t *new_target =
			vgic_get_route_from_state(vic, new_dstate, false);
		if (new_target != NULL) {
			(void)vgic_deliver(virq, vic, new_target, source,
					   dstate,
					   vgic_delivery_state_default(),
					   !vgic_irq_is_spi(virq));
		}
	}

out:
	rcu_read_finish();
	(void)0;
}

static void
vgic_deactivate_unlisted(vic_t *vic, thread_t *vcpu, virq_t virq)
	REQUIRE_PREEMPT_DISABLED
{
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, vcpu, virq);
	vgic_delivery_state_t old_dstate = atomic_load_relaxed(dstate);
	if (vgic_delivery_state_get_listed(&old_dstate)) {
		// Somebody else must have deactivated it already, so ignore the
		// deactivate.
		VGIC_TRACE(DSTATE_CHANGED, vic, vcpu,
			   "deactivate {:d}: already re-listed ({:#x})", virq,
			   vgic_delivery_state_raw(old_dstate));
	} else {
		vgic_deactivate(vic, vcpu, virq, dstate, old_dstate, false,
				false);
	}
}

// Handle an unlisted EOI signalled by an LRENP maintenance interrupt.
//
// The specified VCPU must be the current thread.
static void
vgic_handle_unlisted_eoi(vic_t *vic, thread_t *vcpu) REQUIRE_PREEMPT_DISABLED
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
// This function returns OK if the given VIRQ was listed, ERROR_DENIED if the
// VIRQ cannot be delivered due to the priority limit or the VCPU's group
// disables (so it should remain flagged), and any other error code if the VIRQ
// cannot be delivered due to its state (disabled, active, already listed, etc).
static error_t
vgic_list_if_pending(vic_t *vic, thread_t *vcpu, virq_t virq,
		     uint8_t priority_limit, index_t lr)
	REQUIRE_LOCK(vcpu->vgic_lr_owner_lock) REQUIRE_PREEMPT_DISABLED
{
	error_t err;
	uint8_t priority;

	// Find the delivery state.
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, vcpu, virq);

	vgic_delivery_state_t old_dstate = atomic_load_relaxed(dstate);
	vgic_delivery_state_t new_dstate;
	do {
		if (!vgic_delivery_state_get_enabled(&old_dstate) ||
		    !vgic_delivery_state_is_pending(&old_dstate)) {
			err = ERROR_IDLE;
			goto out;
		}

		if (vgic_delivery_state_get_listed(&old_dstate) ||
		    vgic_delivery_state_get_active(&old_dstate)) {
			err = ERROR_BUSY;
			goto out;
		}

		priority = vgic_delivery_state_get_priority(&old_dstate);
		if ((priority > priority_limit) ||
		    (vgic_delivery_state_get_group1(&old_dstate)
			     ? !vcpu->vgic_group1_enabled
			     : !vcpu->vgic_group0_enabled)) {
			err = ERROR_DENIED;
			goto out;
		}

		// Note: this must be checked _after_ the group disables,
		// because it checks the group disables itself and would
		// incorrectly drop the pending state of a VIRQ blocked by them.
		if (!vgic_route_allowed(vic, vcpu, old_dstate)) {
			err = ERROR_IDLE;
			goto out;
		}

		new_dstate = old_dstate;
		vgic_delivery_state_set_listed(&new_dstate, true);
		vgic_delivery_state_set_edge(&new_dstate, false);
		vgic_delivery_state_set_hw_active(&new_dstate, false);
	} while (!atomic_compare_exchange_strong_explicit(
		dstate, &old_dstate, new_dstate, memory_order_relaxed,
		memory_order_relaxed));

	VGIC_TRACE(DSTATE_CHANGED, vic, vcpu,
		   "list_if_pending {:d}: {:#x} -> {:#x}", virq,
		   vgic_delivery_state_raw(old_dstate),
		   vgic_delivery_state_raw(new_dstate));

	bool	       to_self = (vcpu == thread_get_self());
	bool	       is_hw   = vgic_delivery_state_get_hw_active(&old_dstate);
	virq_source_t *source  = vgic_find_source(vic, vcpu, virq);

	vgic_list_irq(new_dstate, lr, is_hw, priority, dstate, virq, vic, vcpu,
		      source, to_self);

	err = OK;
out:
	return err;
}

static bool
vgic_find_pending_at_priority(vic_t *vic, thread_t *vcpu, index_t prio_index,
			      index_t lr, bool *reset_prio)
	REQUIRE_LOCK(vcpu->vgic_lr_owner_lock) REQUIRE_PREEMPT_DISABLED
{
	bool	listed	 = false;
	uint8_t priority = (uint8_t)(prio_index << VGIC_PRIO_SHIFT);

	_Atomic BITMAP_DECLARE_PTR(VGIC_LOW_RANGES, ranges) =
		&vcpu->vgic_search_ranges_low[prio_index];
	BITMAP_ATOMIC_FOREACH_SET_BEGIN(range, *ranges, VGIC_LOW_RANGES)
		if (compiler_unexpected(!bitmap_atomic_test_and_clear(
			    *ranges, range, memory_order_acquire))) {
			continue;
		}

		bool reset_range = false;
		for (index_t i = 0; i < vgic_low_range_size(range); i++) {
			virq_t virq =
				(virq_t)((range * VGIC_LOW_RANGE_SIZE) + i);

			error_t err = vgic_list_if_pending(vic, vcpu, virq,
							   priority, lr);
			if (err == OK) {
				listed = true;
				break;
			} else if (err == ERROR_DENIED) {
				reset_range = true;
				*reset_prio = true;
			} else {
				// Unable to list
			}
		}

		// If we listed a VIRQ in this range, then we (probably)
		// did not check the entire range, so we need to reset
		// the range's search bit in case there are more VIRQs.
		if (listed) {
			bitmap_atomic_set(*ranges, range, memory_order_relaxed);
			break;
		}

		// If we found a VIRQ in this range that was pending,
		// but we were unable to deliver it to this VCPU due to
		// priority or group disables, reset the range bit.
		if (reset_range) {
			bitmap_atomic_set(*ranges, range, memory_order_relaxed);
		}
	BITMAP_ATOMIC_FOREACH_SET_END

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
			   index_t lr) REQUIRE_LOCK(vcpu->vgic_lr_owner_lock)
	REQUIRE_PREEMPT_DISABLED
{
	bool	listed		= false;
	index_t prio_mask_index = (index_t)priority_mask >> VGIC_PRIO_SHIFT;

	_Atomic BITMAP_DECLARE_PTR(VGIC_PRIORITIES, prios) =
		&vcpu->vgic_search_prios;
	BITMAP_ATOMIC_FOREACH_SET_BEGIN(prio_index, *prios, prio_mask_index)
		if (compiler_unexpected(!bitmap_atomic_test_and_clear(
			    *prios, prio_index, memory_order_acquire))) {
			continue;
		}

		bool reset_prio = false;
#if !GICV3_HAS_VLPI && VGIC_HAS_LPI
#error lpi search ranges not implemented
#endif
		listed = vgic_find_pending_at_priority(vic, vcpu, prio_index,
						       lr, &reset_prio);

		// If we listed a VIRQ at this priority, then we (probably) did
		// not check every range, so we need to reset the priority's
		// search bit in case there ore more VIRQs.
		if (listed) {
			bitmap_atomic_set(*prios, prio_index,
					  memory_order_release);
			break;
		}

		// If we found a VIRQ at this priority that was pending, but we
		// were unable to deliver it to this VCPU due to priority or
		// group disables, reset the priority bit.
		if (reset_prio) {
			bitmap_atomic_set(*prios, prio_index,
					  memory_order_release);
		}
	BITMAP_ATOMIC_FOREACH_SET_END

	return listed;
}

static void
vgic_try_to_list_pending(thread_t *vcpu, vic_t *vic) REQUIRE_PREEMPT_DISABLED
{
	asm_context_sync_ordered(&gich_lr_ordering);
	cpu_index_t lr_owner = vgic_lr_owner_lock_nopreempt(vcpu);
	assert(lr_owner == CPU_INDEX_INVALID);
	register_t elrsr =
		register_ICH_ELRSR_EL2_read_ordered(&gich_lr_ordering);
	elrsr &= util_mask(CPU_GICH_LR_COUNT);

	// If no LRs are empty, find the lowest priority active one.
	if (elrsr == 0U) {
		uint8_t	       lr_priority = GIC_PRIORITY_LOWEST;
		index_result_t lr_r =
			vgic_select_lr(vcpu, GIC_PRIORITY_LOWEST, &lr_priority);
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

		assert_debug(lr < CPU_GICH_LR_COUNT);

		if (vgic_find_pending_and_list(vic, vcpu, GIC_PRIORITY_LOWEST,
					       lr)) {
			vcpu_wakeup_self();
		} else {
			// Nothing left deliverable; clear NPIE.
			vcpu->vgic_ich_hcr = register_ICH_HCR_EL2_read();
			ICH_HCR_EL2_set_NPIE(&vcpu->vgic_ich_hcr, false);
			register_ICH_HCR_EL2_write(vcpu->vgic_ich_hcr);
			break;
		}
	}
	vgic_lr_owner_unlock_nopreempt(vcpu);
}

bool
vgic_handle_irq_received_maintenance(void)
{
	assert_preempt_disabled();

	thread_t *vcpu = thread_get_self();
	vic_t	 *vic  = vcpu->vgic_vic;

	if (compiler_unexpected((vcpu->kind != THREAD_KIND_VCPU) ||
				(vic == NULL))) {
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

	if (!vgic_fgt_allowed()) {
		// Check for enable bit changes. This will clear out all of the
		// LRs and redo any deliveries, so we can skip the none-pending
		// handling.
		if (ICH_MISR_EL2_get_VGrp0D(&misr) ||
		    ICH_MISR_EL2_get_VGrp1D(&misr) ||
		    ICH_MISR_EL2_get_VGrp0E(&misr) ||
		    ICH_MISR_EL2_get_VGrp1E(&misr)) {
			GICD_CTLR_DS_t gicd_ctlr =
				atomic_load_acquire(&vic->gicd_ctlr);
			cpu_index_t lr_owner =
				vgic_lr_owner_lock_nopreempt(vcpu);
			assert(lr_owner == CPU_INDEX_INVALID);
			VGIC_TRACE(ASYNC_EVENT, vic, vcpu,
				   "group enable maintenance: {:#x}",
				   ICH_MISR_EL2_raw(misr));
			if (vgic_gicr_update_group_enables(vic, vcpu,
							   gicd_ctlr)) {
				vcpu_wakeup_self();
			}
			vgic_lr_owner_unlock_nopreempt(vcpu);
			goto out;
		}
	}

	// Always try to deliver more interrupts if the NP interrupt is enabled,
	// regardless of whether it is actually asserted. Note that NP may have
	// become asserted as a result of EOI or group disable handling above,
	// so we would have to reread MISR to get the right value anyway.
	if (ICH_HCR_EL2_get_NPIE(&vcpu->vgic_ich_hcr)) {
		vgic_try_to_list_pending(vcpu, vic);
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
	REQUIRE_LOCK(vcpu->vgic_lr_owner_lock)
		REQUIRE_PREEMPT_DISABLED EXCLUDE_SCHEDULER_LOCK(vcpu)
{
	assert_debug(lr < CPU_GICH_LR_COUNT);
	vgic_lr_status_t *status = &vcpu->vgic_lrs[lr];
	assert(status->dstate != NULL);
	bool need_update = false;

	vgic_delivery_state_t old_dstate = atomic_load_relaxed(status->dstate);
	if (vgic_delivery_state_get_hw_detached(&old_dstate) ||
	    vgic_delivery_state_get_need_sync(&old_dstate)) {
		(void)vgic_sync_lr(vic, vcpu, status,
				   vgic_delivery_state_default(), true);
		need_update = true;
	}

	return need_update;
}

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
static bool
vgic_sync_vcpu(thread_t *vcpu, bool hw_access)
	REQUIRE_LOCK(vcpu->vgic_lr_owner_lock) REQUIRE_PREEMPT_DISABLED
{
	bool wakeup = false;

	assert(vcpu != NULL);
	assert((thread_get_self() == vcpu) == hw_access);

	vic_t *vic = vcpu->vgic_vic;

	if (compiler_expected(vic != NULL)) {
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

	return wakeup;
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
	REQUIRE_LOCK(vcpu->vgic_lr_owner_lock) REQUIRE_PREEMPT_DISABLED
{
	bool wakeup = false;

	vgic_sleep_state_t sleep_state = atomic_load_relaxed(&vcpu->vgic_sleep);
	if (sleep_state != VGIC_SLEEP_STATE_AWAKE) {
		// The GICR is asleep. We can't deliver anything.
		ICH_HCR_EL2_set_NPIE(&vcpu->vgic_ich_hcr, false);

#if VGIC_HAS_1N
		if (sleep_state == VGIC_SLEEP_STATE_WAKEUP_1N) {
			// The GICR has been chosen for 1-of-N wakeup.
			wakeup = true;
			goto out;
		}
#endif

		// If anything is flagged for delivery, wake up immediately.
		wakeup = !bitmap_atomic_empty(vcpu->vgic_search_prios,
					      VGIC_PRIORITIES);
		goto out;
	}

	if (!vcpu->vgic_group0_enabled && !vcpu->vgic_group1_enabled) {
		// Both groups are disabled; no VIRQs are deliverable.
		ICH_HCR_EL2_set_NPIE(&vcpu->vgic_ich_hcr, false);
		goto out;
	}

	index_t prio_index_cutoff = VGIC_PRIORITIES;
	while (!bitmap_atomic_empty(vcpu->vgic_search_prios,
				    prio_index_cutoff)) {
		uint8_t lowest_prio    = GIC_PRIORITY_HIGHEST;
		index_t lowest_prio_lr = 0U;

		// Search for any LR we can safely deliver to.
		for (index_t i = 0; i < CPU_GICH_LR_COUNT; i++) {
			vgic_lr_status_t *status = &vcpu->vgic_lrs[i];

			if ((status->dstate == NULL) ||
			    vgic_lr_is_empty(status->lr)) {
				// LR is empty; we can fill it immediately.
				lowest_prio_lr = i;
				lowest_prio    = GIC_PRIORITY_LOWEST;
				break;
			}

			if (ICH_LR_EL2_base_get_State(&status->lr.base) !=
			    ICH_LR_EL2_STATE_INVALID) {
				// LR is valid; we can try to replace the IRQ in
				// it if it has the (possibly equal) lowest
				// priority of all valid LRs.
				uint8_t this_prio =
					ICH_LR_EL2_base_get_Priority(
						&status->lr.base);
				if (this_prio >= lowest_prio) {
					lowest_prio_lr = i;
					lowest_prio    = this_prio;
				}
			}
		}

		if (lowest_prio > GIC_PRIORITY_HIGHEST) {
			if (vgic_find_pending_and_list(vic, vcpu, lowest_prio,
						       lowest_prio_lr)) {
				wakeup = true;
			} else {
				break;
			}

		} else {
			break;
		}

		// We can't deliver IRQs that are equal or lower (numerically
		// greater) priority than the lowest-priority pending LR, so
		// exclude them from the next vgic_search_prios check.
		prio_index_cutoff = (index_t)lowest_prio >> VGIC_PRIO_SHIFT;
	}

	ICH_HCR_EL2_set_NPIE(&vcpu->vgic_ich_hcr,
			     !bitmap_atomic_empty(vcpu->vgic_search_prios,
						  VGIC_PRIORITIES));

out:
	return wakeup;
}

static bool
vgic_retry_unrouted_virq(vic_t *vic, virq_t virq)
{
	assert(vic != NULL);
	// Only SPIs can be unrouted
	assert(vgic_irq_is_spi(virq));

	preempt_disable();

	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, NULL, virq);
	assert(dstate != NULL);
	vgic_delivery_state_t current_dstate = atomic_load_relaxed(dstate);

	bool unclaimed = false;
	if (vgic_delivery_state_get_enabled(&current_dstate) &&
	    !vgic_delivery_state_get_listed(&current_dstate) &&
	    vgic_delivery_state_is_pending(&current_dstate)) {
		// The IRQ can be delivered, but hasn't been yet. Choose
		// a route for it, checking the current VCPU first for 1-of-N.
		if (!vgic_try_route_and_flag(vic, virq, current_dstate, true)) {
			unclaimed = true;
		}
	}

	preempt_enable();

	return unclaimed;
}

void
vgic_retry_unrouted(vic_t *vic)
{
	spinlock_acquire(&vic->search_lock);

	BITMAP_ATOMIC_FOREACH_SET_BEGIN(range, vic->search_ranges_low,
					VGIC_LOW_RANGES)
		if (compiler_unexpected(!bitmap_atomic_test_and_clear(
			    vic->search_ranges_low, range,
			    memory_order_acquire))) {
			continue;
		}

		VGIC_DEBUG_TRACE(ROUTE, vic, NULL, "unrouted: check range {:d}",
				 range);

		bool unclaimed = false;
		for (index_t i = 0; i < vgic_low_range_size(range); i++) {
			virq_t virq =
				(virq_t)((range * VGIC_LOW_RANGE_SIZE) + i);
			if (vgic_irq_is_spi(virq) &&
			    vgic_retry_unrouted_virq(vic, virq)) {
				unclaimed = true;
			}
		}

		if (unclaimed) {
			// We didn't succeed in routing all of the IRQs in
			// this range, so reset the range's search bit.
			bitmap_atomic_set(vic->search_ranges_low, range,
					  memory_order_acquire);
		}
	BITMAP_ATOMIC_FOREACH_SET_END

	spinlock_release(&vic->search_lock);
}

#if VGIC_HAS_1N
static bool
vgic_check_unrouted_virq(vic_t *vic, thread_t *vcpu, virq_t virq)
{
	assert(vic != NULL);
	// Only SPIs can be unrouted
	assert(vgic_irq_is_spi(virq));

	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, NULL, virq);
	assert(dstate != NULL);
	vgic_delivery_state_t current_dstate = atomic_load_relaxed(dstate);

	return vgic_delivery_state_get_enabled(&current_dstate) &&
	       !vgic_delivery_state_get_listed(&current_dstate) &&
	       vgic_delivery_state_is_pending(&current_dstate) &&
	       ((platform_irq_cpu_class((cpu_index_t)vcpu->vgic_gicr_index) ==
		 0U)
			? vgic_get_delivery_state_is_class0(&current_dstate)
			: vgic_get_delivery_state_is_class1(&current_dstate));
}

static bool
vgic_check_unrouted(vic_t *vic, thread_t *vcpu)
{
	bool wakeup_found = false;

	BITMAP_ATOMIC_FOREACH_SET_BEGIN(range, vic->search_ranges_low,
					VGIC_LOW_RANGES)
		VGIC_DEBUG_TRACE(ROUTE, vic, NULL, "unrouted: check range {:d}",
				 range);

		for (index_t i = 0; i < vgic_low_range_size(range); i++) {
			virq_t virq =
				(virq_t)((range * VGIC_LOW_RANGE_SIZE) + i);
			if (vgic_irq_is_spi(virq) &&
			    vgic_check_unrouted_virq(vic, vcpu, virq)) {
				wakeup_found = true;
				break;
			}
		}
	BITMAP_ATOMIC_FOREACH_SET_END

	return wakeup_found;
}
#endif

// This function is called when permanently tearing down a VCPU.
//
// It clears out the list registers, disregarding the priority order of active
// LRs (rather than reclaiming the lowest active priority first as usual). It
// also reroutes all pending inactive IRQs that are flagged in the VCPU's search
// bitmaps, including directly routed IRQs.
//
// The specified thread must not be running on any CPU.
void
vgic_undeliver_all(vic_t *vic, thread_t *vcpu)
{
	cpu_index_t lr_owner = vgic_lr_owner_lock(vcpu);
	assert(!cpulocal_index_valid(lr_owner));

	vcpu->vgic_group0_enabled = false;
	vcpu->vgic_group1_enabled = false;

	for (index_t i = 0U; i < CPU_GICH_LR_COUNT; i++) {
		if (vcpu->vgic_lrs[i].dstate != NULL) {
			vgic_reclaim_lr(vic, vcpu, i, true);
		}
	}

	BITMAP_ATOMIC_FOREACH_SET_BEGIN(prio, vcpu->vgic_search_prios,
					VGIC_PRIORITIES)
		BITMAP_ATOMIC_FOREACH_SET_BEGIN(
			range, vcpu->vgic_search_ranges_low[prio],
			VGIC_LOW_RANGES)
			for (index_t i = 0; i < vgic_low_range_size(range);
			     i++) {
				virq_t virq =
					(virq_t)((range * VGIC_LOW_RANGE_SIZE) +
						 i);
				if (!vgic_irq_is_spi(virq)) {
					// The IRQ can't be rerouted.
					continue;
				}

				_Atomic vgic_delivery_state_t *dstate =
					vgic_find_dstate(vic, vcpu, virq);
				assert(dstate != NULL);
				vgic_delivery_state_t current_dstate =
					atomic_load_relaxed(dstate);

				if (vgic_delivery_state_get_enabled(
					    &current_dstate) &&
				    !vgic_delivery_state_get_listed(
					    &current_dstate) &&
				    vgic_delivery_state_is_pending(
					    &current_dstate)) {
					vgic_route_and_flag(vic, virq,
							    current_dstate,
							    false);
				}
			}
		BITMAP_ATOMIC_FOREACH_SET_END
	BITMAP_ATOMIC_FOREACH_SET_END

	vgic_lr_owner_unlock(vcpu);
}

#if VGIC_HAS_1N
static bool
vgic_do_reroute(vic_t *vic, thread_t *vcpu, index_t prio_index)
	REQUIRE_PREEMPT_DISABLED
{
	bool reset_prio = false;

	_Atomic BITMAP_DECLARE_PTR(VGIC_LOW_RANGES, ranges) =
		&vcpu->vgic_search_ranges_low[prio_index];
	BITMAP_ATOMIC_FOREACH_SET_BEGIN(range, *ranges, VGIC_LOW_RANGES)
		if (compiler_unexpected(!bitmap_atomic_test_and_clear(
			    *ranges, range, memory_order_acquire))) {
			continue;
		}
		bool reset_range = false;
		for (index_t i = 0; i < vgic_low_range_size(range); i++) {
			virq_t virq =
				(virq_t)((range * VGIC_LOW_RANGE_SIZE) + i);
			if (!vgic_irq_is_spi(virq)) {
				// IRQ can't be rerouted; reset the
				// pending flag
				reset_range = true;
				continue;
			}

			_Atomic vgic_delivery_state_t *dstate =
				vgic_find_dstate(vic, NULL, virq);
			assert(dstate != NULL);
			vgic_delivery_state_t current_dstate =
				atomic_load_relaxed(dstate);

			if (!vgic_delivery_state_get_enabled(&current_dstate) ||
			    vgic_delivery_state_get_listed(&current_dstate) ||
			    !vgic_delivery_state_is_pending(&current_dstate)) {
				// Not pending
			} else if (vgic_delivery_state_get_route_1n(
					   &current_dstate)) {
				// 1-of-N; reroute it
				VGIC_DEBUG_TRACE(ROUTE, vic, NULL,
						 "reroute-all: {:d}", virq);
				vgic_route_and_flag(vic, virq, current_dstate,
						    false);
			} else {
				// Direct; reset the pending flag
				reset_range = true;
			}
		}
		if (reset_range) {
			bitmap_atomic_set(*ranges, range, memory_order_relaxed);
			reset_prio = true;
		}
	BITMAP_ATOMIC_FOREACH_SET_END

	return reset_prio;
}
#endif

// This function is called after disabling one or both VIRQ groups.
//
// It removes the pending state from all LRs, and reroutes any pending inactive
// VIRQs that were in the LRs. It also reroutes all pending inactive 1-of-N IRQs
// that are flagged in the VCPU's search bitmaps.
//
// This is distinct from vgic_undeliver_all() in three ways: active LRs remain
// active; direct IRQs aren't rerouted; and the search bitmap is updated
// (because not doing so might prevent a subsequent sleep).
//
// If the specified VCPU is not current, its LR lock must be held, and it must
// not be running remotely.
static void
vgic_reroute_all(vic_t *vic, thread_t *vcpu)
	REQUIRE_LOCK(vcpu->vgic_lr_owner_lock) REQUIRE_PREEMPT_DISABLED
{
#if VGIC_HAS_1N
	BITMAP_ATOMIC_FOREACH_SET_BEGIN(prio_index, vcpu->vgic_search_prios,
					VGIC_PRIORITIES)

		if (compiler_unexpected(!bitmap_atomic_test_and_clear(
			    vcpu->vgic_search_prios, prio_index,
			    memory_order_acquire))) {
			continue;
		}

		bool reset_prio = vgic_do_reroute(vic, vcpu, prio_index);
		if (reset_prio) {
			bitmap_atomic_set(vcpu->vgic_search_prios, prio_index,
					  memory_order_relaxed);
		}
	BITMAP_ATOMIC_FOREACH_SET_END
#endif

	bool from_self = (thread_get_self() == vcpu);
	for (index_t i = 0U; i < CPU_GICH_LR_COUNT; i++) {
		if (vcpu->vgic_lrs[i].dstate != NULL) {
			if (from_self) {
				vgic_read_lr_state(i);
			}
			(void)vgic_sync_lr(vic, vcpu, &vcpu->vgic_lrs[i],
					   vgic_delivery_state_default(),
					   false);
			if (from_self) {
				vgic_write_lr(i);
			}
		}
	}
}

// Check for changes to the group enable bits, and update LRs as necessary.
//
// If the specified VCPU is not current, its LR lock must be held, and it must
// not be running remotely. The GICD_CTLR value should be read from the GICD
// before acquiring the LR lock; any subsequent change to the GICD_CTLR by
// another CPU must trigger another call to this function, typically by sending
// an IPI.
static bool
vgic_gicr_update_group_enables(vic_t *vic, thread_t *gicr_vcpu,
			       GICD_CTLR_DS_t gicd_ctlr)
{
	bool hw_access = thread_get_self() == gicr_vcpu;
	bool wakeup    = false;

	assert_preempt_disabled();

	bool group0_was_enabled = gicr_vcpu->vgic_group0_enabled;
	bool group1_was_enabled = gicr_vcpu->vgic_group1_enabled;

	if (hw_access) {
		// Read ICH_VMCR_EL2 to check the current group enables
		gicr_vcpu->vgic_ich_vmcr =
			register_ICH_VMCR_EL2_read_ordered(&asm_ordering);
	}

	bool group0_enable = GICD_CTLR_DS_get_EnableGrp0(&gicd_ctlr) &&
			     ICH_VMCR_EL2_get_VENG0(&gicr_vcpu->vgic_ich_vmcr);
	bool group1_enable = GICD_CTLR_DS_get_EnableGrp1(&gicd_ctlr) &&
			     ICH_VMCR_EL2_get_VENG1(&gicr_vcpu->vgic_ich_vmcr);

	// Update the group enables. Note that we do this before we clear
	// out the LRs, to ensure that any 1-of-N IRQs that are no longer
	// deliverable will be flagged on another CPU, or as unrouted.
	gicr_vcpu->vgic_group0_enabled = group0_enable;
	gicr_vcpu->vgic_group1_enabled = group1_enable;

	// If either group is newly disabled, reroute everything. Only active
	// IRQs will be left in the LRs. Pending 1-of-N IRQs will be flagged on
	// another CPU if possible, or as unrouted otherwise.
	if ((!group0_enable && group0_was_enabled) ||
	    (!group1_enable && group1_was_enabled)) {
		vgic_reroute_all(vic, gicr_vcpu);
	}

	if (hw_access) {
		// Read ICH_HCR_EL2 so we can safely update the trap enables
		// and call vgic_do_delivery_check()
		gicr_vcpu->vgic_ich_hcr = register_ICH_HCR_EL2_read();
	}

#if VGIC_HAS_LPI && GICV3_HAS_VLPI_V4_1
	// The vSGIEOICount flag is set for every VCPU based on the nASSGIreq
	// flag in GICD_CTLR, which the VM can only update while the groups
	// are disabled in GICD_CTLR. Updating it unconditionally here is
	// probably faster than checking whether we need to update it.
	ICH_HCR_EL2_set_vSGIEOICount(&gicr_vcpu->vgic_ich_hcr,
				     vic->vsgis_enabled);
#endif

	// Update the group enable / disable traps. This isn't needed if we have
	// ARMv8.6-FGT, because we can unconditionally trap all ICC_IGRPENn_EL1
	// writes in that case.
	if (!vgic_fgt_allowed()) {
		ICH_HCR_EL2_set_TALL0(&gicr_vcpu->vgic_ich_hcr, !group0_enable);
		ICH_HCR_EL2_set_TALL1(&gicr_vcpu->vgic_ich_hcr, !group1_enable);
		ICH_HCR_EL2_set_VGrp0DIE(&gicr_vcpu->vgic_ich_hcr,
					 group0_enable);
		ICH_HCR_EL2_set_VGrp1DIE(&gicr_vcpu->vgic_ich_hcr,
					 group1_enable);
	}

	// Now search for and list all deliverable VIRQs.
	if (group0_enable || group1_enable) {
#if VGIC_HAS_1N
		// If either group is newly enabled, check for unrouted 1-of-N
		// VIRQs, and flag them on this CPU if possible.
		if ((group0_enable && !group0_was_enabled) ||
		    (group1_enable && !group1_was_enabled)) {
			vgic_retry_unrouted(vic);
		}
#endif

		wakeup = vgic_do_delivery_check(vic, gicr_vcpu);
	}

	if (hw_access) {
		// Update the trap enables (including NPIE which may be set by
		// the call to vgic_do_delivery_check())
		register_ICH_HCR_EL2_write(gicr_vcpu->vgic_ich_hcr);
	}

	return wakeup;
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
				   true);
	}
}

void
vgic_handle_thread_context_switch_post(thread_t *prev)
{
	vic_t *vic = prev->vgic_vic;

	if (vic != NULL) {
		bool wakeup_prev = false;

		cpu_index_t lr_owner = vgic_lr_owner_lock(prev);
		assert(lr_owner == cpulocal_get_index());
		if (ipi_clear(IPI_REASON_VGIC_SYNC)) {
			if (vgic_sync_vcpu(prev, false)) {
				wakeup_prev = true;
			}
		}

		if (ipi_clear(IPI_REASON_VGIC_ENABLE)) {
			if (vgic_gicr_update_group_enables(
				    vic, prev,
				    atomic_load_acquire(&vic->gicd_ctlr))) {
				wakeup_prev = true;
			}
		}
		atomic_store_relaxed(&prev->vgic_lr_owner_lock.owner,
				     CPU_INDEX_INVALID);

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

			wakeup_prev = vgic_do_delivery_check(vic, prev) ||
				      wakeup_prev;

			vgic_lr_owner_unlock(prev);

			if (wakeup_prev) {
				scheduler_lock(prev);
				vcpu_wakeup(prev);
				scheduler_unlock(prev);
			}
			vgic_deliver_pending_sgi(vic, prev);
		} else {
			vgic_lr_owner_unlock(prev);
		}
	}
}

void
vgic_handle_thread_load_state(void) LOCK_IMPL
{
	thread_t *vcpu = thread_get_self();
	assert(vcpu != NULL);
	vic_t *vic = vcpu->vgic_vic;

	if (vic != NULL) {
		spinlock_acquire(&vcpu->vgic_lr_owner_lock.lock);
		atomic_store_relaxed(&vcpu->vgic_lr_owner_lock.owner,
				     cpulocal_get_index());

		// Match the seq_cst fences in vgic_flag_unlocked and
		// vgic_icc_generate_sgi. This ensures that those routines
		// either see us as the new owner and send an IPI after
		// the fence, so we will see and handle it after the context
		// switch ends, or else write the pending IRQ state before
		// the fence, so it is seen by our checks below.
		atomic_thread_fence(memory_order_seq_cst);

		for (index_t i = 0; i < CPU_GICH_LR_COUNT; i++) {
			vgic_write_lr(i);
		}

		(void)vgic_do_delivery_check(vic, vcpu);

		gicv3_write_ich_aprs(vcpu->vgic_ap0rs, vcpu->vgic_ap1rs);
		register_ICH_VMCR_EL2_write(vcpu->vgic_ich_vmcr);
		register_ICH_HCR_EL2_write(vcpu->vgic_ich_hcr);

		spinlock_release(&vcpu->vgic_lr_owner_lock.lock);
		vgic_deliver_pending_sgi(vic, vcpu);
	} else {
		register_ICH_HCR_EL2_write(ICH_HCR_EL2_default());
	}
}

void
vgic_gicr_rd_set_sleep(vic_t *vic, thread_t *gicr_vcpu, bool sleep)
{
#if VGIC_HAS_1N
	if (sleep) {
		// Update the sleep state, but only if we were awake; don't wipe
		// out a wakeup if this is a redundant write of the sleep bit.
		vgic_sleep_state_t old_sleep_state = VGIC_SLEEP_STATE_AWAKE;
		if (atomic_compare_exchange_strong_explicit(
			    &gicr_vcpu->vgic_sleep, &old_sleep_state,
			    VGIC_SLEEP_STATE_ASLEEP, memory_order_relaxed,
			    memory_order_relaxed)) {
			// We successfully entered sleep and there was no
			// existing wakeup. We now need to check whether any
			// IRQs had been marked unrouted prior to us entering
			// sleep. We need a seq_cst fence to order the check
			// after entering sleep, matching the seq_cst fence in
			// vgic_wakeup_1n().
			atomic_thread_fence(memory_order_seq_cst);
			if (vgic_check_unrouted(vic, gicr_vcpu)) {
				old_sleep_state = VGIC_SLEEP_STATE_ASLEEP;
				(void)atomic_compare_exchange_strong_explicit(
					&gicr_vcpu->vgic_sleep,
					&old_sleep_state,
					VGIC_SLEEP_STATE_WAKEUP_1N,
					memory_order_relaxed,
					memory_order_relaxed);
			}
		}
	} else {
		// We're waking up; if there's a wakeup it can be
		// discarded.
		atomic_store_relaxed(&gicr_vcpu->vgic_sleep,
				     VGIC_SLEEP_STATE_AWAKE);
	}
#else
	(void)vic;
	atomic_store_relaxed(&gicr_vcpu->vgic_sleep,
			     sleep ? VGIC_SLEEP_STATE_ASLEEP
				   : VGIC_SLEEP_STATE_AWAKE);
#endif
}

bool
vgic_gicr_rd_check_sleep(thread_t *gicr_vcpu)
{
	bool is_asleep;

	if (atomic_load_relaxed(&gicr_vcpu->vgic_sleep) !=
	    VGIC_SLEEP_STATE_AWAKE) {
		if (!vgic_fgt_allowed()) {
			cpu_index_t lr_owner = vgic_lr_owner_lock(gicr_vcpu);
			// We might not have received the maintenance interrupt
			// yet after the VM cleared the group enable bits.
			// Synchronise the group enables before checking them.
			if (lr_owner == CPU_INDEX_INVALID) {
				(void)vgic_gicr_update_group_enables(
					gicr_vcpu->vgic_vic, gicr_vcpu,
					atomic_load_acquire(
						&gicr_vcpu->vgic_vic
							 ->gicd_ctlr));
			}
			vgic_lr_owner_unlock(gicr_vcpu);
		}
		// We can only sleep if the groups are disabled.
		is_asleep = !gicr_vcpu->vgic_group0_enabled &&
			    !gicr_vcpu->vgic_group1_enabled;
	} else {
		is_asleep = false;
#if VGIC_HAS_LPI && GICV3_HAS_VLPI_V4_1
		if (gicv3_vpe_check_wakeup(false)) {
			// The GICR hasn't finished scheduling the vPE yet.
			// Returning true here means that the GICR_WAKER poll
			// on VCPU resume will effectively prevent the VCPU
			// entering its idle loop (and maybe suspending again)
			// until the GICR has had an opportunity to forward any
			// pending SGIs and LPIs.
			is_asleep = true;
		}
#endif
	}

	return is_asleep;
}

bool
vgic_handle_vcpu_pending_wakeup(void)
{
	thread_t *vcpu = thread_get_self();
	assert(vcpu != NULL);

	bool pending =
		!bitmap_atomic_empty(vcpu->vgic_search_prios, VGIC_PRIORITIES);

#if VGIC_HAS_1N
	if (!pending && (atomic_load_relaxed(&vcpu->vgic_sleep) ==
			 VGIC_SLEEP_STATE_WAKEUP_1N)) {
		pending = true;
	}
#endif

	if (!pending &&
	    (vcpu->vgic_group0_enabled || vcpu->vgic_group1_enabled)) {
		// There might be interrupts left in the LRs. This could happen
		// at a preemption point in a long-running service call, or
		// during a suspend call into a retention state.
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
vgic_handle_vcpu_stopped(void)
{
	thread_t *vcpu = thread_get_self();

	if (vcpu->vgic_vic != NULL) {
		// Disable interrupt delivery and reroute any pending IRQs. The
		// VCPU really should have done this itself, but PSCI_CPU_OFF
		// is not able to fail if it hasn't, so we just go ahead and do
		// it ourselves.
		if (vcpu->vgic_group0_enabled || vcpu->vgic_group1_enabled) {
			cpu_index_t remote_cpu = vgic_lr_owner_lock(vcpu);
			assert(remote_cpu == CPU_INDEX_INVALID);

			register_ICH_VMCR_EL2_write_ordered(
				ICH_VMCR_EL2_default(), &asm_ordering);

			(void)vgic_gicr_update_group_enables(
				vcpu->vgic_vic, vcpu, GICD_CTLR_DS_default());

			vgic_lr_owner_unlock(vcpu);
		}
	}
}

vcpu_trap_result_t
vgic_handle_vcpu_trap_wfi(void)
{
	thread_t *vcpu = thread_get_self();
	assert(vcpu != NULL);

	if (vcpu->vgic_vic != NULL) {
		(void)vgic_lr_owner_lock(vcpu);

#if VGIC_HAS_1N
		// Eagerly release invalid LRs. This increases the likelihood
		// that a 1-of-N IRQ that is next delivered to some remote CPU
		// can be locally asserted on that remote CPU.
		register_t elrsr =
			register_ICH_ELRSR_EL2_read_ordered(&gich_lr_ordering);
		while (elrsr != 0U) {
			index_t lr = compiler_ctz(elrsr);
			elrsr &= ~util_bit(lr);

			assert_debug(lr < CPU_GICH_LR_COUNT);

			vgic_lr_status_t *status = &vcpu->vgic_lrs[lr];
			if (status->dstate != NULL) {
				vgic_reclaim_lr(vcpu->vgic_vic, vcpu, lr,
						false);
				// No need to rewrite the LR because we
				// know that it is already invalid
			}
		}
#endif

		// It is possible that a maintenance interrupt is currently
		// pending but was not delivered before the WFI trap. If so,
		// handling it might make more IRQs deliverable, in which case
		// the WFI should not be allowed to sleep.
		//
		// The simplest way to deal with this possibility is to run the
		// maintenance handler directly.
		(void)vgic_handle_irq_received_maintenance();

		vgic_lr_owner_unlock(vcpu);
	}

	// Continue to the default handler
	return VCPU_TRAP_RESULT_UNHANDLED;
}

bool
vgic_handle_ipi_received_enable(void)
{
	thread_t *current = thread_get_self();
	assert(current->vgic_vic != NULL);
	(void)vgic_lr_owner_lock_nopreempt(current);
	bool wakeup = vgic_gicr_update_group_enables(
		current->vgic_vic, current,
		atomic_load_acquire(&current->vgic_vic->gicd_ctlr));
	vgic_lr_owner_unlock_nopreempt(current);
	return wakeup;
}

bool
vgic_handle_ipi_received_sync(void)
{
	thread_t *current = thread_get_self();
	(void)vgic_lr_owner_lock_nopreempt(current);
	bool wakeup = vgic_sync_vcpu(current, true);
	vgic_lr_owner_unlock_nopreempt(current);
	return wakeup;
}

bool
vgic_handle_ipi_received_deliver(void)
{
	thread_t *current = thread_get_self();
	assert(current != NULL);
	vic_t *vic = current->vgic_vic;

	if (vic != NULL) {
		(void)vgic_lr_owner_lock_nopreempt(current);
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
		vgic_lr_owner_unlock_nopreempt(current);
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
vgic_icc_set_group_enable(bool is_group_1, ICC_IGRPEN_EL1_t igrpen)
{
	thread_t *current = thread_get_self();
	assert(current != NULL);
	vic_t *vic = current->vgic_vic;
	assert(vic != NULL);

	cpu_index_t remote_cpu = vgic_lr_owner_lock(current);
	assert(remote_cpu == CPU_INDEX_INVALID);

	current->vgic_ich_vmcr = register_ICH_VMCR_EL2_read();
	bool enabled	       = ICC_IGRPEN_EL1_get_Enable(&igrpen);
	VGIC_TRACE(ICC_WRITE, vic, current, "group {:d} {:s}",
		   (register_t)is_group_1,
		   (uintptr_t)(enabled ? "enabled" : "disabled"));
	if (is_group_1) {
		ICH_VMCR_EL2_set_VENG1(&current->vgic_ich_vmcr, enabled);
	} else {
		ICH_VMCR_EL2_set_VENG0(&current->vgic_ich_vmcr, enabled);
	}
	register_ICH_VMCR_EL2_write_ordered(current->vgic_ich_vmcr,
					    &asm_ordering);

	GICD_CTLR_DS_t gicd_ctlr = atomic_load_acquire(&vic->gicd_ctlr);
	if (vgic_gicr_update_group_enables(vic, current, gicd_ctlr)) {
		vcpu_wakeup_self();
	}

	vgic_lr_owner_unlock(current);
}

void
vgic_icc_irq_deactivate(vic_t *vic, irq_t irq_num)
{
	thread_t		      *vcpu = thread_get_self();
	_Atomic vgic_delivery_state_t *dstate =
		vgic_find_dstate(vic, vcpu, irq_num);
	assert(dstate != NULL);

	// Don't let context switches delist the VIRQ out from under us
	preempt_disable();

	// Call generic deactivation handling if not currently listed
	vgic_delivery_state_t old_dstate = atomic_load_relaxed(dstate);
	if (!vgic_delivery_state_get_listed(&old_dstate)) {
		vgic_deactivate(vic, thread_get_self(), irq_num, dstate,
				old_dstate, false, false);
		goto out;
	}

	// Search the current CPU's list registers for the VIRQ
	for (index_t lr = 0; lr < CPU_GICH_LR_COUNT; lr++) {
		vgic_lr_status_t *status = &vcpu->vgic_lrs[lr];
		if (status->dstate != dstate) {
			continue;
		}

		vgic_read_lr_state(lr);
		ICH_LR_EL2_State_t state =
			ICH_LR_EL2_base_get_State(&status->lr.base);

		if ((state == ICH_LR_EL2_STATE_PENDING) ||
		    (state == ICH_LR_EL2_STATE_INVALID)) {
			// Interrupt is not active; nothing to do.
			goto out;
		}

		// Determine whether the edge bit should be reset when
		// delisting.
		bool set_edge = state == ICH_LR_EL2_STATE_PENDING_ACTIVE;

		// Determine whether the hw_active bit should be reset when
		// delisting (or alternatively, the physical IRQ should be
		// manually deactivated).
		bool hw_active = ICH_LR_EL2_base_get_HW(&status->lr.base);

		// Kick the interrupt out of the LR. We could potentially keep
		// it listed if it is still pending, but that complicates the
		// code too much and we don't care about EOImode=1 VMs anyway.
		status->lr.base = ICH_LR_EL2_base_default();
		status->dstate	= NULL;
		vgic_write_lr(lr);

#if VGIC_HAS_1N
		if (vgic_delivery_state_get_route_1n(&old_dstate)) {
			virq_source_t *source =
				vgic_find_source(vic, vcpu, irq_num);
			vgic_spi_reset_route_1n(source, old_dstate);
		}
#endif

		vgic_deactivate(vic, thread_get_self(), irq_num, dstate,
				old_dstate, set_edge, hw_active);

		goto out;
	}

	// If we didn't find the LR, it's listed on another CPU.
	//
	// DIR is supposed to work across CPUs so we should flag the IRQ and
	// send an IPI to deactivate it. Possibly an extra dstate bit would
	// work for this. However, few VMs will use EOImode=1 so we don't care
	// very much just yet. For now, warn and do nothing.
	//
	// FIXME:
#if !defined(NDEBUG)
	static _Thread_local bool warned_about_ignored_dir = false;
	if (!warned_about_ignored_dir) {
		TRACE_AND_LOG(VGIC, WARN,
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

static void
vgic_send_sgi(vic_t *vic, thread_t *vcpu, virq_t virq, bool is_group_1)
	REQUIRE_RCU_READ
{
	_Atomic vgic_delivery_state_t *dstate =
		&vcpu->vgic_private_states[virq];
	vgic_delivery_state_t old_dstate = atomic_load_relaxed(dstate);

	if (!is_group_1 && vgic_delivery_state_get_group1(&old_dstate)) {
		// SGI0R & ASGI1R do not generate group 1 SGIs
		goto out;
	}

#if GICV3_HAS_VLPI_V4_1 && VGIC_HAS_LPI
	// Raise SGI using direct injection through the ITS if possible.
	//
	// We can only use direct injection if:
	// - The SGI is not listed in an LR (which has unpredictable behaviour
	//   when combined with direct injection of the same SGI)
	// - The VM has permitted vSGI delivery with no active state, by setting
	//   GICD_CTLR.nASSGIreq (cached in vic->vsgis_enabled)
	// - The VCPU has enabled vLPIs, and the ITS commands to sync the SGI
	//   configuration into the LPI tables have completed
	if (!vgic_delivery_state_get_listed(&old_dstate) &&
	    vic->vsgis_enabled && (vgic_vsgi_assert(vcpu, virq) == OK)) {
		goto out;
	}
#endif

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
			atomic_load_relaxed(&vcpu->vgic_lr_owner_lock.owner);

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

		(void)vgic_deliver(virq, vic, vcpu, NULL, dstate, assert_dstate,
				   true);
	}

out:
	return;
}

void
vgic_icc_generate_sgi(vic_t *vic, ICC_SGIR_EL1_t sgir, bool is_group_1)
{
	register_t target_list	 = ICC_SGIR_EL1_get_TargetList(&sgir);
	index_t	   target_offset = 16U * (index_t)ICC_SGIR_EL1_get_RS(&sgir);
	virq_t	   virq		 = ICC_SGIR_EL1_get_INTID(&sgir);

	assert(virq < GIC_SGI_NUM);

	if (compiler_unexpected(ICC_SGIR_EL1_get_IRM(&sgir))) {
		thread_t *current = thread_get_self();
		for (index_t i = 0U; i < vic->gicr_count; i++) {
			rcu_read_start();
			thread_t *vcpu =
				atomic_load_consume(&vic->gicr_vcpus[i]);
			if ((vcpu != NULL) && (vcpu != current)) {
				vgic_send_sgi(vic, vcpu, virq, is_group_1);
			}
			rcu_read_finish();
		}
	} else {
		while (target_list != 0U) {
			index_t target_bit = compiler_ctz(target_list);
			target_list &= ~util_bit(target_bit);

			index_result_t cpu_r = vgic_get_index_for_mpidr(
				vic, (uint8_t)(target_bit + target_offset),
				ICC_SGIR_EL1_get_Aff1(&sgir),
				ICC_SGIR_EL1_get_Aff2(&sgir),
				ICC_SGIR_EL1_get_Aff3(&sgir));
			if (cpu_r.e != OK) {
				// ignore invalid target
				continue;
			}
			assert(cpu_r.r < vic->gicr_count);

			rcu_read_start();
			thread_t *vcpu =
				atomic_load_consume(&vic->gicr_vcpus[cpu_r.r]);
			if (vcpu != NULL) {
				vgic_send_sgi(vic, vcpu, virq, is_group_1);
			}
			rcu_read_finish();
		}
	}
}
