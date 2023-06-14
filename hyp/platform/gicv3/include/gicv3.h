// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// IRQ functions

count_t
gicv3_irq_max(void);

gicv3_irq_type_t
gicv3_get_irq_type(irq_t irq);

bool
gicv3_irq_is_percpu(irq_t irq);

error_t
gicv3_irq_check(irq_t irq);

void
gicv3_irq_enable(irq_t irq);

void
gicv3_irq_enable_local(irq_t irq) REQUIRE_PREEMPT_DISABLED;

void
gicv3_irq_enable_percpu(irq_t irq, cpu_index_t cpu);

void
gicv3_irq_disable(irq_t irq);

void
gicv3_irq_disable_local(irq_t irq) REQUIRE_PREEMPT_DISABLED;

void
gicv3_irq_disable_local_nowait(irq_t irq) REQUIRE_PREEMPT_DISABLED;

void
gicv3_irq_disable_percpu(irq_t irq, cpu_index_t cpu);

void
gicv3_irq_cancel_nowait(irq_t irq);

irq_trigger_result_t
gicv3_irq_set_trigger(irq_t irq, irq_trigger_t trigger);

irq_trigger_result_t
gicv3_irq_set_trigger_percpu(irq_t irq, irq_trigger_t trigger, cpu_index_t cpu);

error_t
gicv3_spi_set_route(irq_t irq, GICD_IROUTER_t route);

#if GICV3_HAS_GICD_ICLAR
error_t
gicv3_spi_set_classes(irq_t irq, bool class0, bool class1);
#endif

irq_result_t
gicv3_irq_acknowledge(void) REQUIRE_PREEMPT_DISABLED;

void
gicv3_irq_priority_drop(irq_t irq);

void
gicv3_irq_deactivate(irq_t irq);

void
gicv3_irq_deactivate_percpu(irq_t irq, cpu_index_t cpu);

// IPI specific functions

void
gicv3_ipi_others(ipi_reason_t ipi);

void
gicv3_ipi_one(ipi_reason_t ipi, cpu_index_t cpu);

void
gicv3_ipi_clear(ipi_reason_t ipi);

#if GICV3_HAS_LPI

// LPI configuration cache invalidation.
//
// If the virtual GICR internally caches VLPI configuration (rather than mapping
// a guest-accessible address to the ITS directly), it must have already updated
// the cache before calling any of these functions.
//
// There are five variants: LPI by (device, event) pair, LPI by IRQ number, VLPI
// by VIRQ number, all LPIs by physical CPU ID, or all VLPIs by VCPU.
//
// The first variant queues an INV command on the relevant ITS. It is only
// implemented if there is at least one ITS, and is therefore declared in
// gicv3_its.h rather than here.
//
// The second and third variants are only implemented on GICv4.1, or (for the
// second variant) on GICv3 with no ITS. On GICv4.0 or GICv3 with an ITS, the
// caller must instead either find or synthesise a (device, event) pair that is
// mapped to the given LPI or VLPI, and then call the first variant.
//
// The fourth variant uses the GICR if possible (GICv4.1 or GICv3 with no ITS)
// and queues an INVALL command on the ITS otherwise.
//
// The fifth variant is only available on GICv4.1; the caller must otherwise
// scan the virtual IC and call the first variant for every (device, event) pair
// mapped to it.
//
// These operations are not guaranteed to complete immediately. The first
// variant returns a sequence number which can be used to poll or wait using the
// functions above. The remaining variants have corresponding functions to poll
// completion of all preceding calls for a specified PCPU or VCPU; note that
// they may spuriously show non-completion because all VCPUs affine to a PCPU
// share the completion state of that PCPU.
#if !GICV3_HAS_ITS || GICV3_HAS_VLPI_V4_1
void
gicv3_lpi_inv_by_id(cpu_index_t cpu, irq_t lpi);
#endif

#if GICV3_HAS_VLPI_V4_1
void
gicv3_vlpi_inv_by_id(thread_t *vcpu, virq_t vlpi);
#endif

void
gicv3_lpi_inv_all(cpu_index_t cpu);

#if GICV3_HAS_VLPI_V4_1
void
gicv3_vlpi_inv_all(thread_t *vcpu);
#endif

bool
gicv3_lpi_inv_pending(cpu_index_t cpu);

#if GICV3_HAS_VLPI_V4_1
bool
gicv3_vlpi_inv_pending(thread_t *vcpu);
#endif

#if defined(GICV3_ENABLE_VPE) && GICV3_ENABLE_VPE

// Virtual PE scheduling.
//
// These functions must be called to inform the GICR when the current VCPU
// has been mapped to a vPE ID with gicv3_its_vpe_map() and is not currently
// blocked in EL2 or EL3 nor set to sleep in its virtual GICR_WAKER.
//
// Points at which these functions must be called include context switching,
// entering or leaving the WFI fastpath, entering or leaving an interruptible
// call to EL3, or changing GICR_WAKER.ProcessorSleep or GICR_CTLR.EnableLPIs
// on the current VCPU.
//
// The _schedule function takes boolean arguments indicating whether direct vSGI
// delivery and the default doorbell should be enabled for each of the two
// interrupt groups. If these values must be changed for the running VCPU, e.g.
// due to a GICD_CTLR write, the VCPU must be descheduled and then scheduled
// with the new values. Note that these values have no effect on LPIs with
// individual doorbells, and therefore do nothing for GICv4.0.
//
// This function must call gicv3_vpe_sync_deschedule() to wait for the most
// recent deschedule to complete, so it should be called as late as possible.
void
gicv3_vpe_schedule(bool enable_group0, bool enable_group1)
	REQUIRE_PREEMPT_DISABLED;

// The _deschedule function takes a boolean argument indicating whether the
// previously scheduled VCPU is waiting for interrupts, and therefore requires a
// doorbell IRQ to wake it. It returns a boolean value which is true if a
// doorbell was requested but at least one VLPI or VSGI was already pending, in
// which case the VCPU should to be woken and rescheduled immediately.
//
// This function may not take effect immediately, as the GICR may take some
// time to scan its pending VLPI tables and synchronise with the ITSs to fully
// deschedule the vPE, and this function only waits for that synchronisation to
// complete if enable_doorbell is true. Subsequent calls to _schedule must call
// gicv3_vpe_sync_deschedule() to wait until it has taken effect. Therefore this
// function should be called as early as possible once it is known that a VCPU
// must be descheduled.
bool
gicv3_vpe_deschedule(bool enable_doorbell) REQUIRE_PREEMPT_DISABLED;

// Check whether a VCPU can safely block waiting for interrupts.
//
// Returns true if the current VCPU was previously woken by a pending vLPI or
// vSGI, a gicv3_vpe_schedule() call has been made for the current VCPU, and the
// GICR is not yet known to have finished scheduling the VCPU.
//
// This is used to prevent the VCPU entering a loop where it is woken by a
// doorbell or the PendingLast bit due to a pending vLPI or vSGI, but then
// blocks agoin before the GICR delivers the interrupt.
//
// The VGIC must ensure that this is called at some point during any VCPU idle
// loop or suspend / resume path such that the VCPU does not block while it
// returns true, and will observe the pending interrupt after it returns false.
//
// If the retry_trap argument is true, the result will indicate the state of the
// GICR before this function was called (i.e. when the trap that triggered it
// occurred). Otherwise, it will indicate the state of the GICR after the
// function was called.
bool
gicv3_vpe_check_wakeup(bool retry_trap);

// Poll until any pending vPE deschedule is complete on the specified CPU.
//
// If the maybe_scheduled boolean is false, this function asserts that there
// is no currently scheduled vPE. If it is true, the function has no effect if
// there is a currently scheduled vPE. This is called by gicv3_vpe_schedule(),
// but may also be called elsewhere when it is necessary to guarantee that the
// GICR has completely descheduled a VCPU.
void
gicv3_vpe_sync_deschedule(cpu_index_t cpu, bool maybe_scheduled)
	REQUIRE_PREEMPT_DISABLED;

#if GICV3_HAS_VLPI_V4_1
// Ask the GICR for a specific VCPU's pending vSGI state.
uint32_result_t
gicv3_vpe_vsgi_query(thread_t *vcpu);
#endif

#endif // GICV3_ENABLE_VPE

#endif // GICV3_HAS_LPI
