// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypconstants.h>
#include <hypregisters.h>

#include <compiler.h>
#include <log.h>
#include <preempt.h>
#include <scheduler.h>
#include <thread.h>
#include <trace.h>
#include <vcpu.h>
#include <virq.h>

#include <events/thread.h>
#include <events/vcpu.h>

#include <asm/barrier.h>
#include <asm/sysregs.h>

#include "event_handlers.h"

#if defined(ARCH_ARM_HAVE_SCXT)
#include <atomic.h>
#include <platform_features.h>
#include <prng.h>

static bool		scxt_disabled = false;
static _Atomic uint64_t scxt_count;
#endif

void
vcpu_handle_boot_runtime_init(void)
{
	// Disable floating-point traps
#if defined(ARCH_ARM_FEAT_VHE)
	CPTR_EL2_E2H1_t cptr =
		register_CPTR_EL2_E2H1_read_ordered(&asm_ordering);
	CPTR_EL2_E2H1_set_FPEN(&cptr, 3);
	register_CPTR_EL2_E2H1_write_ordered(cptr, &asm_ordering);
#else
	CPTR_EL2_E2H0_t cptr =
		register_CPTR_EL2_E2H0_read_ordered(&asm_ordering);
	CPTR_EL2_E2H0_set_TFP(&cptr, 0);
	register_CPTR_EL2_E2H0_write_ordered(cptr, &asm_ordering);
#endif
}

void
vcpu_handle_boot_cpu_warm_init(void)
{
#if defined(ARCH_ARM_FEAT_VHE)
	CONTEXTIDR_EL2_t ctxidr = CONTEXTIDR_EL2_default();
	register_CONTEXTIDR_EL2_write(ctxidr);
#endif

#if !SCHEDULER_CAN_MIGRATE
	// Expose the real MIDR to VMs; no need to context-switch it.
	register_VPIDR_EL2_write(register_MIDR_EL1_read());
#endif

	// Although ARM recommends these traps do not trap AArch32 EL0 to EL2,
	// it is implementation defined, so zero this register.
	HSTR_EL2_t hstr = HSTR_EL2_cast(0U);
	register_HSTR_EL2_write(hstr);
#if defined(ARCH_ARM_HAVE_SCXT)
	if (!scxt_disabled) {
		register_SCXTNUM_EL2_write(atomic_fetch_add_explicit(
			&scxt_count, 1U, memory_order_relaxed));
	}
#endif
}

#if defined(ARCH_ARM_HAVE_SCXT)
void
vcpu_handle_boot_cold_init(void)
{
	platform_cpu_features_t features = platform_get_cpu_features();

	scxt_disabled = platform_cpu_features_get_scxt_disable(&features);

	if (!scxt_disabled) {
		uint64_result_t rand_r = prng_get64();
		assert(rand_r.e == OK);
		atomic_store_relaxed(&scxt_count, rand_r.r);
	} else {
		LOG(DEBUG, DEBUG, "platform SCXTNUM_ELx access disabled!");
	}
}
#endif

static void
arch_vcpu_el1_registers_init(thread_t *vcpu)
{
	if (thread_get_self() == vcpu) {
		register_SCTLR_EL1_write(SCTLR_EL1_default());
	} else {
		SCTLR_EL1_init(&vcpu->vcpu_regs_el1.sctlr_el1);
	}
}

static void
arch_vcpu_el2_registers_init(vcpu_el2_registers_t *el2_regs)
{
#if defined(ARCH_ARM_FEAT_VHE)
	CPTR_EL2_E2H1_init(&el2_regs->cptr_el2);
	CPTR_EL2_E2H1_set_FPEN(&el2_regs->cptr_el2, 3);
#else
	CPTR_EL2_E2H0_init(&el2_regs->cptr_el2);
	CPTR_EL2_E2H0_set_TFP(&el2_regs->cptr_el2, 0);
#endif

	HCR_EL2_init(&el2_regs->hcr_el2);
	HCR_EL2_set_VM(&el2_regs->hcr_el2, true);
	HCR_EL2_set_SWIO(&el2_regs->hcr_el2, true);
	HCR_EL2_set_PTW(&el2_regs->hcr_el2, false);
	HCR_EL2_set_FMO(&el2_regs->hcr_el2, true);
	HCR_EL2_set_IMO(&el2_regs->hcr_el2, true);
	HCR_EL2_set_AMO(&el2_regs->hcr_el2, true);
	HCR_EL2_set_VF(&el2_regs->hcr_el2, false);
	HCR_EL2_set_VI(&el2_regs->hcr_el2, false);
	HCR_EL2_set_VSE(&el2_regs->hcr_el2, false);
	HCR_EL2_set_FB(&el2_regs->hcr_el2, false);
	HCR_EL2_set_BSU(&el2_regs->hcr_el2, 0);
	HCR_EL2_set_DC(&el2_regs->hcr_el2, false);
	HCR_EL2_set_TWI(&el2_regs->hcr_el2, true);
	HCR_EL2_set_TWE(&el2_regs->hcr_el2, false);
	HCR_EL2_set_TID0(&el2_regs->hcr_el2, false);
	HCR_EL2_set_TID1(&el2_regs->hcr_el2, false);
	HCR_EL2_set_TID2(&el2_regs->hcr_el2, false);
	HCR_EL2_set_TID3(&el2_regs->hcr_el2, true);
	HCR_EL2_set_TSC(&el2_regs->hcr_el2, true);
	HCR_EL2_set_TIDCP(&el2_regs->hcr_el2, true);
	HCR_EL2_set_TACR(&el2_regs->hcr_el2, true);
	HCR_EL2_set_TSW(&el2_regs->hcr_el2, true);
	HCR_EL2_set_TPCP(&el2_regs->hcr_el2, false);
	HCR_EL2_set_TPU(&el2_regs->hcr_el2, false);
	HCR_EL2_set_TTLB(&el2_regs->hcr_el2, false);
	HCR_EL2_set_TVM(&el2_regs->hcr_el2, false);
	HCR_EL2_set_TDZ(&el2_regs->hcr_el2, false);
	HCR_EL2_set_HCD(&el2_regs->hcr_el2, false);
	HCR_EL2_set_TRVM(&el2_regs->hcr_el2, false);
	HCR_EL2_set_RW(&el2_regs->hcr_el2, true);
	HCR_EL2_set_CD(&el2_regs->hcr_el2, false);
	HCR_EL2_set_ID(&el2_regs->hcr_el2, false);

	// We allow the guest to set its own inner and outer cacheability,
	// regardless of whether this may mean that memory accessed by another
	// agent (e.g. the Hypervisor) might cause a loss of coherency due to
	// mismatched memory attributes. Note, that this should never
	// constitute a secure issue as the Hypervisor must properly validate
	// any arguments from VM memory. The guest is aware of the Hypervisor
	// and it is its responsibility to ensure that memory used for
	// communication with the Hypervisor or other VMs, has the correct
	// attributes.
	HCR_EL2_set_MIOCNCE(&el2_regs->hcr_el2, true);

#if defined(ARCH_ARM_FEAT_VHE)
	HCR_EL2_set_E2H(&el2_regs->hcr_el2, true);
#endif
	HCR_EL2_set_TGE(&el2_regs->hcr_el2, false);

#if defined(ARCH_ARM_FEAT_LOR)
	// FIXME: we could temporarily set TLOR to false if we encounter Linux
	// using these registers
	HCR_EL2_set_TLOR(&el2_regs->hcr_el2, true);
#endif

#if defined(ARCH_ARM_FEAT_PAuth)
	HCR_EL2_set_APK(&el2_regs->hcr_el2, true);
	HCR_EL2_set_API(&el2_regs->hcr_el2, true);
#endif

#if defined(ARCH_ARM_FEAT_NV)
	HCR_EL2_set_AT(&el2_regs->hcr_el2, false);
	HCR_EL2_set_NV(&el2_regs->hcr_el2, false);
	HCR_EL2_set_NV1(&el2_regs->hcr_el2, false);
#endif

#if defined(ARCH_ARM_FEAT_NV2)
	HCR_EL2_set_NV2(&el2_regs->hcr_el2, false);
#endif

#if defined(ARCH_ARM_FEAT_S2FWB)
	HCR_EL2_set_FWB(&el2_regs->hcr_el2, false);
#endif

#if defined(ARCH_ARM_FEAT_RASv1p1)
	HCR_EL2_set_FIEN(&el2_regs->hcr_el2, false);
#endif

	MDCR_EL2_init(&el2_regs->mdcr_el2);
	// Enable all debug register traps by default
	MDCR_EL2_set_TDA(&el2_regs->mdcr_el2, true);
	MDCR_EL2_set_TDOSA(&el2_regs->mdcr_el2, true);
	MDCR_EL2_set_TDRA(&el2_regs->mdcr_el2, true);
	// Don't trap debug exceptions. The only ones not controlled by the
	// registers trapped above are BRK / BKPT which are never cross-VM
	MDCR_EL2_set_TDE(&el2_regs->mdcr_el2, false);
#if defined(ARCH_ARM_PMU_V3)
	// Enable PMU access traps by default
	MDCR_EL2_set_TPM(&el2_regs->mdcr_el2, true);
	MDCR_EL2_set_TPMCR(&el2_regs->mdcr_el2, true);
#endif
#if defined(ARCH_ARM_FEAT_SPEv1p1)
	// Enable SPE traps by default
	MDCR_EL2_set_TPMS(&el2_regs->mdcr_el2, true);
#endif
#if defined(ARCH_ARM_FEAT_TRF)
	// Enable trace traps by default
	MDCR_EL2_set_TTRF(&el2_regs->mdcr_el2, true);
#endif

	// FIXME: HACR_EL2 - per CPU type
}

void
vcpu_handle_rootvm_init(thread_t *root_thread)
{
	vcpu_el2_registers_t *el2_regs = &root_thread->vcpu_regs_el2;

	// Run the root VM with HCR.DC set, so we don't need a stg-1 page-table
	// Set TVM to detect the VM attempts to enable stg-1 MMU,
	// Note however we don't support switching off HCR.DC yet!
	HCR_EL2_set_DC(&el2_regs->hcr_el2, true);
	HCR_EL2_set_TVM(&el2_regs->hcr_el2, true);
}

error_t
vcpu_arch_handle_object_create_thread(thread_create_t thread_create)
{
	error_t	  err	 = OK;
	thread_t *thread = thread_create.thread;
	assert(thread != NULL);

	if (thread->kind == THREAD_KIND_VCPU) {
		// Set up nonzero init values for EL2 registers
		arch_vcpu_el2_registers_init(&thread->vcpu_regs_el2);

		// Indicate that the VCPU is uniprocessor by default. The vgic
		// module will override this if the VCPU is attached to a VIC.
		thread->vcpu_regs_mpidr_el1 = MPIDR_EL1_default();
		MPIDR_EL1_set_U(&thread->vcpu_regs_mpidr_el1, true);

#if defined(ARCH_ARM_HAVE_SCXT)
		if (!scxt_disabled) {
			vcpu_runtime_flags_set_scxt_allowed(&thread->vcpu_flags,
							    true);
		}
#endif
	}

	return err;
}

#if SCHEDULER_CAN_MIGRATE
void
vcpu_arch_handle_thread_start(void)
{
	thread_t *thread = thread_get_self();

	if (thread->kind == THREAD_KIND_VCPU) {
		if (vcpu_option_flags_get_pinned(&thread->vcpu_options)) {
			// The VCPU won't migrate, so expose the real MIDR.
			thread->vcpu_regs_midr_el1 = register_MIDR_EL1_read();
		} else {
			// Use a MIDR distinct from that of a real CPU.
			// Otherwise the guest may try to use features
			// or errata workarounds that are unsupported.
			MIDR_EL1_t midr = MIDR_EL1_default();
			MIDR_EL1_set_Architecture(&midr, 0xfU);
			MIDR_EL1_set_Implementer(&midr, 0U);
			MIDR_EL1_set_PartNum(&midr, 0x48U);
			MIDR_EL1_set_Variant(&midr, 0U);
			MIDR_EL1_set_Revision(&midr, 0U);
			thread->vcpu_regs_midr_el1 = midr;
			// Use virtual ID registers for this VCPU.
			HCR_EL2_set_TID1(&thread->vcpu_regs_el2.hcr_el2, true);
			// For migratable threads, we ensure TLB operations are
			// broadcast to all inner-shareable cores. Since Linux
			// VMs normally do this anyway, there should be no real
			// impact, and thus should be the same as forcing a TLB
			// flush at migrate time. We also ensure that all
			// barriers apply to at least the inner-shareable
			// domain.
			HCR_EL2_set_FB(&thread->vcpu_regs_el2.hcr_el2, true);
			HCR_EL2_set_BSU(&thread->vcpu_regs_el2.hcr_el2, 1U);
		}
	}
}
#endif

noreturn void
vcpu_exception_return(uintptr_t unused_param);

static noreturn void
vcpu_thread_start(bool warm_reset) EXCLUDE_PREEMPT_DISABLED
{
	trigger_vcpu_started_event(warm_reset);
	trigger_thread_exit_to_user_event(THREAD_ENTRY_REASON_NONE);
	thread_reset_stack(vcpu_exception_return, 0U);
}

static noreturn void
vcpu_thread_entry(uintptr_t unused_param) EXCLUDE_PREEMPT_DISABLED
{
	(void)unused_param;
	vcpu_thread_start(false);
}

thread_func_t
vcpu_handle_thread_get_entry_fn(void)
{
	assert(thread_get_self()->kind == THREAD_KIND_VCPU);

	return vcpu_thread_entry;
}

error_t
vcpu_configure(thread_t *thread, vcpu_option_flags_t vcpu_options)
{
	error_t ret = OK;

	assert(thread != NULL);
	assert(thread->kind == THREAD_KIND_VCPU);

	thread->vcpu_options = vcpu_options;

	return ret;
}

static void
vcpu_reset_execution_context(thread_t *vcpu)
{
	assert((vcpu != NULL) && (vcpu->kind == THREAD_KIND_VCPU));
	assert((thread_get_self() == vcpu) ||
	       scheduler_is_blocked(vcpu, SCHEDULER_BLOCK_VCPU_OFF));

	// Reset the EL1 registers.
	arch_vcpu_el1_registers_init(vcpu);

	// Reset the EL1 processor state: EL1H mode, all interrupts disabled.
	SPSR_EL2_A64_t spsr_el2 = SPSR_EL2_A64_default();
	SPSR_EL2_A64_set_M(&spsr_el2, SPSR_64BIT_MODE_EL1H);
	SPSR_EL2_A64_set_D(&spsr_el2, true);
	SPSR_EL2_A64_set_A(&spsr_el2, true);
	SPSR_EL2_A64_set_I(&spsr_el2, true);
	SPSR_EL2_A64_set_F(&spsr_el2, true);
	vcpu->vcpu_regs_gpr.spsr_el2.a64 = spsr_el2;
}

bool_result_t
vcpu_poweron(thread_t *vcpu, vmaddr_result_t entry_point,
	     register_result_t context)
{
	error_t err = OK;
	bool	ret = false;

	assert(vcpu != NULL);
	assert(vcpu->kind == THREAD_KIND_VCPU);
	assert(scheduler_is_blocked(vcpu, SCHEDULER_BLOCK_VCPU_OFF));

	err = trigger_vcpu_poweron_event(vcpu);
	if (err == OK) {
		vcpu_reset_execution_context(vcpu);
		if (entry_point.e == OK) {
			vcpu->vcpu_regs_gpr.pc = ELR_EL2_cast(entry_point.r);
		}
		if (context.e == OK) {
			vcpu->vcpu_regs_gpr.x[0] = context.r;
		}

		// We must have a valid address space and stage 2 must be
		// enabled. Otherwise the guest can trivially take over the
		// hypervisor.
		assert(HCR_EL2_get_VM(&vcpu->vcpu_regs_el2.hcr_el2) &&
		       (VTTBR_EL2_get_BADDR(
				&vcpu->addrspace->vm_pgtable.vttbr_el2) != 0U));

		ret = scheduler_unblock(vcpu, SCHEDULER_BLOCK_VCPU_OFF);
	}

	return (bool_result_t){ .e = err, .r = ret };
}

error_t
vcpu_poweroff(bool last_cpu, bool force)
{
	thread_t *current = thread_get_self();
	assert(current->kind == THREAD_KIND_VCPU);

	scheduler_lock(current);

	error_t ret = trigger_vcpu_poweroff_event(current, last_cpu, force);
	if (ret == OK) {
		scheduler_block(current, SCHEDULER_BLOCK_VCPU_OFF);
		scheduler_unlock_nopreempt(current);

		if (force) {
			preempt_enable();
			vcpu_halted();
			// not reached
		} else {
			trigger_vcpu_stopped_event();
			scheduler_yield();

			// If we get here, then someone has called
			// vcpu_poweron() on us.
			preempt_enable();
			vcpu_thread_start(false);
			// not reached
		}
	}

	scheduler_unlock(current);
	return ret;
}

#if defined(MODULE_VM_VCPU_RUN)
vcpu_run_state_t
vcpu_handle_vcpu_run_check(const thread_t *vcpu, register_t *state_data_0)
{
	vcpu_run_state_t ret = VCPU_RUN_STATE_BLOCKED;
	if (scheduler_is_blocked(vcpu, SCHEDULER_BLOCK_VCPU_FAULT)) {
		ret = VCPU_RUN_STATE_FAULT;
	} else if (scheduler_is_blocked(vcpu, SCHEDULER_BLOCK_VCPU_OFF)) {
		vcpu_run_poweroff_flags_t flags =
			vcpu_run_poweroff_flags_default();
		ret	      = VCPU_RUN_STATE_POWERED_OFF;
		*state_data_0 = vcpu_run_poweroff_flags_raw(flags);
	} else {
		// Nothing to do
	}
	return ret;
}
#endif

error_t
vcpu_suspend(void)
{
	error_t	  ret	  = OK;
	thread_t *current = thread_get_self();
	assert(current->kind == THREAD_KIND_VCPU);

	// Disable preemption so we don't try to deliver interrupts to the
	// current thread while it is suspended. We could handle that case in
	// vcpu_wakeup_self(), but we want that function to be fast.
	preempt_disable();

	if (vcpu_pending_wakeup()) {
		ret = ERROR_BUSY;
	} else {
		ret = trigger_vcpu_suspend_event(current);
	}

	if (ret == OK) {
		scheduler_lock_nopreempt(current);
		scheduler_block(current, SCHEDULER_BLOCK_VCPU_SUSPEND);
		scheduler_unlock_nopreempt(current);

		scheduler_yield();

		trigger_vcpu_resume_event(current);
	}

	preempt_enable();

	return ret;
}

void
vcpu_resume(thread_t *vcpu)
{
	assert(vcpu != NULL);
	assert(vcpu->kind == THREAD_KIND_VCPU);
	assert(scheduler_is_blocked(vcpu, SCHEDULER_BLOCK_VCPU_SUSPEND));

	if (scheduler_unblock(vcpu, SCHEDULER_BLOCK_VCPU_SUSPEND)) {
		scheduler_trigger();
	}
}

noreturn void
vcpu_warm_reset(paddr_t entry_point, register_t context)
{
	thread_t *vcpu = thread_get_self();

	assert(vcpu->kind == THREAD_KIND_VCPU);

	// Inform any other modules of the warm reset
	trigger_vcpu_warm_reset_event(vcpu);

	// Set the thread's startup context
	vcpu_reset_execution_context(vcpu);
	vcpu->vcpu_regs_gpr.pc	 = ELR_EL2_cast(entry_point);
	vcpu->vcpu_regs_gpr.x[0] = context;

	// We've been warm-reset; jump directly to the entry point.
	vcpu_thread_start(true);
}

noreturn void
vcpu_halted(void)
{
	thread_t *current = thread_get_self();

	assert(current->kind == THREAD_KIND_VCPU);

	preempt_disable();

	trigger_vcpu_stopped_event();

	(void)virq_assert(&current->vcpu_halt_virq_src, true);

	scheduler_yield();

	// If we get here, then someone resumed the halted vcpu.
	preempt_enable();
	vcpu_thread_start(false);
}
