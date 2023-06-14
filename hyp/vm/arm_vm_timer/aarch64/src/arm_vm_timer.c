// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypconstants.h>
#include <hypregisters.h>

#include <compiler.h>
#include <cpulocal.h>
#include <irq.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <preempt.h>
#include <scheduler.h>
#include <trace.h>

#include <asm/barrier.h>

#include "arm_vm_timer.h"
#include "event_handlers.h"

#if defined(VERBOSE) && VERBOSE
#define VM_TIMER_DEBUG 1
#else
#define VM_TIMER_DEBUG 0
#endif

#define ARM_VM_TIMER_TYPE_NUM (ENUM_ARM_VM_TIMER_TYPE_MAX_VALUE + 1)

static hwirq_t *arm_vm_timer_hwirq[ARM_VM_TIMER_TYPE_NUM];
CPULOCAL_DECLARE_STATIC(bool, arm_vm_timer_irq_active)[ARM_VM_TIMER_TYPE_NUM];

void
arm_vm_timer_init(arm_vm_timer_type_t tt)
{
	CNT_CTL_t cnt_ctl;

	CNT_CTL_init(&cnt_ctl);
	CNT_CTL_set_IMASK(&cnt_ctl, true);

	if (tt == ARM_VM_TIMER_TYPE_VIRTUAL) {
		register_CNTV_CTL_EL0_write_ordered(cnt_ctl, &asm_ordering);
	} else if (tt == ARM_VM_TIMER_TYPE_PHYSICAL) {
		register_CNTP_CTL_EL0_write_ordered(cnt_ctl, &asm_ordering);
	} else {
		panic("Invalid timer");
	}
}

bool
arm_vm_timer_is_irq_enabled(arm_vm_timer_type_t tt)
{
	CNT_CTL_t cnt_ctl;

	if (tt == ARM_VM_TIMER_TYPE_VIRTUAL) {
		cnt_ctl = register_CNTV_CTL_EL0_read_volatile_ordered(
			&asm_ordering);
	} else if (tt == ARM_VM_TIMER_TYPE_PHYSICAL) {
		cnt_ctl = register_CNTP_CTL_EL0_read_volatile_ordered(
			&asm_ordering);
	} else {
		panic("Invalid timer");
	}

	return (CNT_CTL_get_ENABLE(&cnt_ctl) && !CNT_CTL_get_IMASK(&cnt_ctl));
}

bool
arm_vm_timer_is_irq_pending(arm_vm_timer_type_t tt)
{
	CNT_CTL_t cnt_ctl;

	if (tt == ARM_VM_TIMER_TYPE_VIRTUAL) {
		cnt_ctl = register_CNTV_CTL_EL0_read_volatile_ordered(
			&asm_ordering);
	} else if (tt == ARM_VM_TIMER_TYPE_PHYSICAL) {
		cnt_ctl = register_CNTP_CTL_EL0_read_volatile_ordered(
			&asm_ordering);
	} else {
		panic("Invalid timer");
	}

	return (CNT_CTL_get_ENABLE(&cnt_ctl) && !CNT_CTL_get_IMASK(&cnt_ctl) &&
		CNT_CTL_get_ISTATUS(&cnt_ctl));
}

void
arm_vm_timer_cancel_timeout(arm_vm_timer_type_t tt)
{
	CNT_CTL_t cnt_ctl;

	CNT_CTL_init(&cnt_ctl);
	CNT_CTL_set_ENABLE(&cnt_ctl, false);

	if (tt == ARM_VM_TIMER_TYPE_VIRTUAL) {
		register_CNTV_CTL_EL0_write_ordered(cnt_ctl, &asm_ordering);
	} else if (tt == ARM_VM_TIMER_TYPE_PHYSICAL) {
		register_CNTP_CTL_EL0_write_ordered(cnt_ctl, &asm_ordering);
	} else {
		panic("Invalid timer");
	}
}

bool
arm_vm_timer_get_is_expired(arm_vm_timer_type_t tt)
{
	CNT_CTL_t cnt_ctl;

	if (tt == ARM_VM_TIMER_TYPE_VIRTUAL) {
		cnt_ctl = register_CNTV_CTL_EL0_read_volatile_ordered(
			&asm_ordering);
	} else if (tt == ARM_VM_TIMER_TYPE_PHYSICAL) {
		cnt_ctl = register_CNTP_CTL_EL0_read_volatile_ordered(
			&asm_ordering);
	} else {
		panic("Invalid timer");
	}

	assert(CNT_CTL_get_ENABLE(&cnt_ctl));
	return CNT_CTL_get_ISTATUS(&cnt_ctl);
}

uint32_t
arm_vm_timer_get_freqeuncy(void)
{
	CNTFRQ_EL0_t cntfrq = register_CNTFRQ_EL0_read();

	return CNTFRQ_EL0_get_ClockFrequency(&cntfrq);
}

uint64_t
arm_vm_timer_get_ticks(void)
{
	// This register read below is allowed to occur speculatively at any
	// time after the most recent context sync event. If caller the wants
	// it to actually reflect the exact current time, it must execute an
	// ordered ISB before calling this function.
	CNTPCT_EL0_t cntpct =
		register_CNTPCT_EL0_read_volatile_ordered(&asm_ordering);

	return CNTPCT_EL0_get_CountValue(&cntpct);
}

uint64_t
arm_vm_timer_get_timeout(arm_vm_timer_type_t tt)
{
	CNT_CVAL_t cnt_cval;

	if (tt == ARM_VM_TIMER_TYPE_VIRTUAL) {
		cnt_cval = register_CNTV_CVAL_EL0_read_volatile();
	} else if (tt == ARM_VM_TIMER_TYPE_PHYSICAL) {
		cnt_cval = register_CNTP_CVAL_EL0_read_volatile();
	} else {
		panic("Invalid timer");
	}

	return CNT_CVAL_get_CompareValue(&cnt_cval);
}

void
arm_vm_timer_arch_timer_hw_irq_activated(arm_vm_timer_type_t tt)
{
	if ((tt == ARM_VM_TIMER_TYPE_PHYSICAL) ||
	    (tt == ARM_VM_TIMER_TYPE_VIRTUAL)) {
		CPULOCAL(arm_vm_timer_irq_active)[tt] = true;
	} else {
		panic("Invalid timer");
	}
}

void
arm_vm_timer_arch_timer_hw_irq_deactivate(arm_vm_timer_type_t tt)
{
	if ((tt == ARM_VM_TIMER_TYPE_PHYSICAL) ||
	    (tt == ARM_VM_TIMER_TYPE_VIRTUAL)) {
		if (CPULOCAL(arm_vm_timer_irq_active)[tt]) {
			CPULOCAL(arm_vm_timer_irq_active)[tt] = false;
			irq_deactivate(arm_vm_timer_hwirq[tt]);
		}
	} else {
		panic("Invalid timer");
	}
}

void
arm_vm_timer_handle_boot_cpu_cold_init(void)
{
	for (int tt = ENUM_ARM_VM_TIMER_TYPE_MIN_VALUE;
	     tt < ARM_VM_TIMER_TYPE_NUM; tt++) {
		CPULOCAL(arm_vm_timer_irq_active)[tt] = false;
	}
}

void
arm_vm_timer_handle_boot_hypervisor_start(void)
{
	hwirq_ptr_result_t ret;
	hwirq_create_t	   params[] = {
		    {
			    .irq    = PLATFORM_VM_ARCH_VIRTUAL_TIMER_IRQ,
			    .action = HWIRQ_ACTION_VM_TIMER,
		    },
		    {
			    .irq    = PLATFORM_VM_ARCH_PHYSICAL_TIMER_IRQ,
			    .action = HWIRQ_ACTION_VM_TIMER,
		    }
	};

	for (int tt = ENUM_ARM_VM_TIMER_TYPE_MIN_VALUE;
	     tt < ARM_VM_TIMER_TYPE_NUM; tt++) {
		ret = partition_allocate_hwirq(partition_get_private(),
					       params[tt]);

		if ((ret.e != OK) || (object_activate_hwirq(ret.r) != OK)) {
			panic("Failed to enable VM Timer IRQ");
		}

		arm_vm_timer_hwirq[tt] = ret.r;
		irq_enable_local(arm_vm_timer_hwirq[tt]);
	}
}

error_t
arm_vm_timer_handle_power_cpu_suspend(void)
{
	arm_vm_timer_arch_timer_hw_irq_deactivate(ARM_VM_TIMER_TYPE_VIRTUAL);
	arm_vm_timer_arch_timer_hw_irq_deactivate(ARM_VM_TIMER_TYPE_PHYSICAL);

	return OK;
}

void
arm_vm_timer_handle_boot_cpu_warm_init(void)
{
	arm_vm_timer_init(ARM_VM_TIMER_TYPE_VIRTUAL);
	arm_vm_timer_init(ARM_VM_TIMER_TYPE_PHYSICAL);

#if defined(ARCH_ARM_FEAT_VHE)
	CNTHCTL_EL2_E2H1_t cnthctl;
	CNTHCTL_EL2_E2H1_init(&cnthctl);

	CNTHCTL_EL2_E2H1_set_EL1PTEN(&cnthctl, true);
	CNTHCTL_EL2_E2H1_set_EL1PCTEN(&cnthctl, true);

	// TODO: Determine correct setting for EVNTI
	CNTHCTL_EL2_E2H1_set_EVNTI(&cnthctl, 5);
	CNTHCTL_EL2_E2H1_set_EVNTDIR(&cnthctl, false);
	CNTHCTL_EL2_E2H1_set_EVNTEN(&cnthctl, false);

	// These four are here for completeness and are not strictly necessary.
	CNTHCTL_EL2_E2H1_set_EL0PTEN(&cnthctl, true);
	CNTHCTL_EL2_E2H1_set_EL0VTEN(&cnthctl, true);
	CNTHCTL_EL2_E2H1_set_EL0VCTEN(&cnthctl, true);
	CNTHCTL_EL2_E2H1_set_EL0PCTEN(&cnthctl, true);

#if defined(ARCH_ARM_FEAT_ECV)
	// Explicitly disable the ECV feature and the access traps for the
	// virtual timer and counter registers.
	CNTHCTL_EL2_E2H1_set_ECV(&cnthctl, false);
	CNTHCTL_EL2_E2H1_set_EL1TVT(&cnthctl, false);
	CNTHCTL_EL2_E2H1_set_EL1TVCT(&cnthctl, false);
#endif

	register_CNTHCTL_EL2_E2H1_write(cnthctl);
#else
	CNTHCTL_EL2_E2H0_t cnthctl;
	CNTHCTL_EL2_E2H0_init(&cnthctl);

	// In order to disable the physical timer at EL0 and EL1 we trap the
	// accesses to the physical timer registers but do not provide a handler
	// for the trap, causing a synchronous data abort to be injected to the
	// guest.
	CNTHCTL_EL2_E2H0_set_EL1PCEN(&cnthctl, true);
	CNTHCTL_EL2_E2H0_set_EL1PCTEN(&cnthctl, true);

	// TODO: Determine correct setting for EVNTI
	CNTHCTL_EL2_E2H0_set_EVNTI(&cnthctl, 5);
	CNTHCTL_EL2_E2H0_set_EVNTDIR(&cnthctl, false);
	CNTHCTL_EL2_E2H0_set_EVNTEN(&cnthctl, false);

#if defined(ARCH_ARM_FEAT_ECV)
	// Explicitly disable the ECV feature and the access traps for the
	// virtual timer and counter registers.
	CNTHCTL_EL2_E2H0_set_ECV(&cnthctl, false);
	CNTHCTL_EL2_E2H0_set_EL1TVT(&cnthctl, false);
	CNTHCTL_EL2_E2H0_set_EL1TVCT(&cnthctl, false);
#endif

	register_CNTHCTL_EL2_E2H0_write(cnthctl);
#endif

#if VM_TIMER_DEBUG
	TRACE_LOCAL(
		DEBUG, INFO,
		"arm_vm_timer warm boot pcnt {:#x} vctl {:#x} vact {:d} pact {:d}",
		CNTPCT_EL0_raw(register_CNTPCT_EL0_read_volatile_ordered(
			&asm_ordering)),
		CNT_CTL_raw(register_CNTV_CTL_EL0_read_ordered(&asm_ordering)),
		(register_t)CPULOCAL(
			arm_vm_timer_irq_active)[ARM_VM_TIMER_TYPE_VIRTUAL],
		(register_t)CPULOCAL(
			arm_vm_timer_irq_active)[ARM_VM_TIMER_TYPE_PHYSICAL]);
#endif

	register_CNTVOFF_EL2_write(CNTVOFF_EL2_cast(0U));

	for (int tt = ENUM_ARM_VM_TIMER_TYPE_MIN_VALUE;
	     tt < ARM_VM_TIMER_TYPE_NUM; tt++) {
		if (arm_vm_timer_hwirq[tt] != NULL) {
			irq_enable_local(arm_vm_timer_hwirq[tt]);
		}
	}
}

bool
arm_vm_timer_is_irq_enabled_thread(thread_t *thread, arm_vm_timer_type_t tt)
{
	CNT_CTL_t cnt_ctl;

	if (tt == ARM_VM_TIMER_TYPE_VIRTUAL) {
		cnt_ctl = thread->vcpu_regs_el1.cntv_ctl_el0;
	} else if (tt == ARM_VM_TIMER_TYPE_PHYSICAL) {
		cnt_ctl = thread->vcpu_regs_el1.cntp_ctl_el0;
	} else {
		panic("Invalid timer");
	}

	return (CNT_CTL_get_ENABLE(&cnt_ctl) && !CNT_CTL_get_IMASK(&cnt_ctl));
}

ticks_t
arm_vm_timer_get_timeout_thread(thread_t *thread, arm_vm_timer_type_t tt)
{
	CNT_CVAL_t cnt_cval;

	if (tt == ARM_VM_TIMER_TYPE_VIRTUAL) {
		cnt_cval = thread->vcpu_regs_el1.cntv_cval_el0;
	} else if (tt == ARM_VM_TIMER_TYPE_PHYSICAL) {
		cnt_cval = thread->vcpu_regs_el1.cntp_cval_el0;
	} else {
		panic("Invalid timer");
	}

	return CNT_CVAL_get_CompareValue(&cnt_cval);
}

void
arm_vm_timer_load_state(thread_t *thread)
{
	register_CNTKCTL_EL1_write_ordered(thread->vcpu_regs_el1.cntkctl_el1,
					   &asm_ordering);
	register_CNTV_CTL_EL0_write_ordered(thread->vcpu_regs_el1.cntv_ctl_el0,
					    &asm_ordering);
	register_CNTV_CVAL_EL0_write_ordered(
		thread->vcpu_regs_el1.cntv_cval_el0, &asm_ordering);
	register_CNTP_CTL_EL0_write_ordered(thread->vcpu_regs_el1.cntp_ctl_el0,
					    &asm_ordering);
	register_CNTP_CVAL_EL0_write_ordered(
		thread->vcpu_regs_el1.cntp_cval_el0, &asm_ordering);
}

void
arm_vm_timer_handle_thread_save_state(void)
{
	thread_t *thread = thread_get_self();

	if ((compiler_expected(thread->kind == THREAD_KIND_VCPU)) &&
	    !scheduler_is_blocked(thread, SCHEDULER_BLOCK_VCPU_OFF)) {
		thread->vcpu_regs_el1.cntkctl_el1 = register_CNTKCTL_EL1_read();
		thread->vcpu_regs_el1.cntv_ctl_el0 =
			register_CNTV_CTL_EL0_read();
		thread->vcpu_regs_el1.cntv_cval_el0 =
			register_CNTV_CVAL_EL0_read();
		thread->vcpu_regs_el1.cntp_ctl_el0 =
			register_CNTP_CTL_EL0_read();
		thread->vcpu_regs_el1.cntp_cval_el0 =
			register_CNTP_CVAL_EL0_read();
	}
}
