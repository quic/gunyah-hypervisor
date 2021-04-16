// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <cpulocal.h>
#include <irq.h>
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

static hwirq_t *arm_vm_timer_hwirq;

CPULOCAL_DECLARE_STATIC(bool, arm_vm_timer_irq_active);

void
arm_vm_timer_init()
{
	CNTV_CTL_EL0_t cntv_ctl;

	CNTV_CTL_EL0_init(&cntv_ctl);
	CNTV_CTL_EL0_set_IMASK(&cntv_ctl, true);
	register_CNTV_CTL_EL0_write_ordered(cntv_ctl, &asm_ordering);
}

bool
arm_vm_timer_is_irq_enabled()
{
	CNTV_CTL_EL0_t cntv_ctl =
		register_CNTV_CTL_EL0_read_volatile_ordered(&asm_ordering);

	return (CNTV_CTL_EL0_get_ENABLE(&cntv_ctl) &&
		!CNTV_CTL_EL0_get_IMASK(&cntv_ctl));
}

bool
arm_vm_timer_is_irq_pending()
{
	CNTV_CTL_EL0_t cntv_ctl =
		register_CNTV_CTL_EL0_read_volatile_ordered(&asm_ordering);

	return (CNTV_CTL_EL0_get_ENABLE(&cntv_ctl) &&
		!CNTV_CTL_EL0_get_IMASK(&cntv_ctl) &&
		CNTV_CTL_EL0_get_ISTATUS(&cntv_ctl));
}

void
arm_vm_timer_cancel_timeout(void)
{
	CNTV_CTL_EL0_t cntv_ctl;

	CNTV_CTL_EL0_init(&cntv_ctl);
	CNTV_CTL_EL0_set_ENABLE(&cntv_ctl, false);
	register_CNTV_CTL_EL0_write_ordered(cntv_ctl, &asm_ordering);
}

bool
arm_vm_timer_get_is_expired()
{
	CNTV_CTL_EL0_t cntv_ctl =
		register_CNTV_CTL_EL0_read_volatile_ordered(&asm_ordering);

	assert(CNTV_CTL_EL0_get_ENABLE(&cntv_ctl));
	return CNTV_CTL_EL0_get_ISTATUS(&cntv_ctl);
}

uint32_t
arm_vm_timer_get_freqeuncy()
{
	CNTFRQ_EL0_t cntfrq = register_CNTFRQ_EL0_read();

	return CNTFRQ_EL0_get_ClockFrequency(&cntfrq);
}

uint64_t
arm_vm_timer_get_ticks()
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
arm_vm_timer_get_timeout()
{
	// This register read below is allowed to occur speculatively at any
	// time after the most recent context sync event. If caller the wants
	// it to actually reflect the exact current time, it must execute an ISB
	// before calling this function.
	CNTV_CVAL_EL0_t cntv_cval = register_CNTV_CVAL_EL0_read_volatile();

	return CNTV_CVAL_EL0_get_CompareValue(&cntv_cval);
}

void
arm_vm_timer_handle_boot_cpu_cold_init(void)
{
	CPULOCAL(arm_vm_timer_irq_active) = false;
}

void
arm_vm_timer_handle_boot_cpu_warm_init(void)
{
	arm_vm_timer_init();

	register_CNTVOFF_EL2_write(CNTVOFF_EL2_cast(0U));

#if ARCH_AARCH64_USE_VHE
	CNTHCTL_EL2_E2H1_t cnthctl;
	CNTHCTL_EL2_E2H1_init(&cnthctl);

	// In order to disable the physical timer at EL0 and EL1 we trap the
	// accesses to the physical timer registers but do not provide a handler
	// for the trap, causing a synchronous data abort to be injected to the
	// guest.
	// In the future we should virtualise the physical timer as well.
	CNTHCTL_EL2_E2H1_set_EL1PTEN(&cnthctl, false);
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

	register_CNTHCTL_EL2_E2H1_write(cnthctl);

#if VM_TIMER_DEBUG
	TRACE_LOCAL(DEBUG, INFO,
		    "arm_vm_timer warm boot pcnt {:#x} vctl {:#x} act {:d}",
		    CNTPCT_EL0_raw(register_CNTPCT_EL0_read_volatile_ordered(
			    &asm_ordering)),
		    CNTV_CTL_EL0_raw(
			    register_CNTV_CTL_EL0_read_ordered(&asm_ordering)),
		    CPULOCAL(arm_vm_timer_irq_active));
#endif

#else
#error Not implemented
#endif

	if (arm_vm_timer_hwirq != NULL) {
		irq_enable_local(arm_vm_timer_hwirq);
	}
}

void
arm_vm_timer_handle_boot_hypervisor_start()
{
	// Create the VM arch timer IRQ
	hwirq_create_t params = {
		.irq	= PLATFORM_VM_ARCH_TIMER_IRQ,
		.action = HWIRQ_ACTION_VM_TIMER,
	};

	hwirq_ptr_result_t ret =
		partition_allocate_hwirq(partition_get_private(), params);

	if (ret.e != OK) {
		panic("Failed to create VM Timer IRQ");
	}
	arm_vm_timer_hwirq = ret.r;
	irq_enable_local(arm_vm_timer_hwirq);
}

error_t
arm_vm_timer_handle_power_cpu_suspend(void)
{
	arm_vm_timer_arch_timer_hw_irq_deactivate();

	return OK;
}

bool
arm_vm_timer_is_irq_enabled_thread(thread_t *thread)
{
	CNTV_CTL_EL0_t cntv_ctl = thread->vcpu_regs_el1.cntv_ctl_el0;

	return (CNTV_CTL_EL0_get_ENABLE(&cntv_ctl) &&
		!CNTV_CTL_EL0_get_IMASK(&cntv_ctl));
}

ticks_t
arm_vm_timer_get_timeout_thread(thread_t *thread)
{
	CNTV_CVAL_EL0_t cntv_cval = thread->vcpu_regs_el1.cntv_cval_el0;

	return CNTV_CVAL_EL0_get_CompareValue(&cntv_cval);
}

void
arm_vm_timer_arch_timer_hw_irq_activated()
{
	CPULOCAL(arm_vm_timer_irq_active) = true;
}

void
arm_vm_timer_arch_timer_hw_irq_deactivate()
{
	if (CPULOCAL(arm_vm_timer_irq_active)) {
		CPULOCAL(arm_vm_timer_irq_active) = false;
		irq_deactivate(arm_vm_timer_hwirq);
	}
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
}

void
arm_vm_timer_handle_thread_save_state(void)
{
	thread_t *thread = thread_get_self();

	if (thread->kind == THREAD_KIND_VCPU &&
	    !scheduler_is_blocked(thread, SCHEDULER_BLOCK_VCPU_OFF)) {
		thread->vcpu_regs_el1.cntkctl_el1 = register_CNTKCTL_EL1_read();
		thread->vcpu_regs_el1.cntv_ctl_el0 =
			register_CNTV_CTL_EL0_read();
		thread->vcpu_regs_el1.cntv_cval_el0 =
			register_CNTV_CVAL_EL0_read();
	}
}
