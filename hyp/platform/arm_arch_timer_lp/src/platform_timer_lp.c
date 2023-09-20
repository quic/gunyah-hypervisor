// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hyprights.h>

#include <atomic.h>
#include <compiler.h>
#include <cpulocal.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <hyp_aspace.h>
#include <irq.h>
#include <memextent.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <pgtable.h>
#include <platform_cpu.h>
#include <platform_timer_lp.h>
#include <preempt.h>

#include <events/platform.h>

#include <asm/barrier.h>

#include "event_handlers.h"
#include "gicv3.h"

static cntbase_t *hyp_timer_cnt;
static size_t	  virt_hyp_timer_size;

static GICD_IROUTER_t current_route;

static void
platform_timer_lp_enable_and_unmask(void)
{
	CNTP_CTL_t cntp_ctl;
	CNTP_CTL_init(&cntp_ctl);
	CNTP_CTL_set_ENABLE(&cntp_ctl, true);
	CNTP_CTL_set_IMASK(&cntp_ctl, false);

	atomic_store_relaxed(&hyp_timer_cnt->p_ctl, cntp_ctl);
}

void
platform_timer_lp_set_timeout(ticks_t timeout)
{
	assert_preempt_disabled();

	atomic_store_relaxed(&hyp_timer_cnt->p_cval, timeout);
	platform_timer_lp_enable_and_unmask();
}

uint64_t
platform_timer_lp_get_timeout(void)
{
	return atomic_load_relaxed(&hyp_timer_cnt->p_cval);
}

void
platform_timer_lp_cancel_timeout(void)
{
	CNTP_CTL_t cntp_ctl;
	CNTP_CTL_init(&cntp_ctl);
	CNTP_CTL_set_ENABLE(&cntp_ctl, false);
	CNTP_CTL_set_IMASK(&cntp_ctl, true);

	atomic_store_relaxed(&hyp_timer_cnt->p_ctl, cntp_ctl);
}

uint32_t
platform_timer_lp_get_frequency(void)
{
	return atomic_load_relaxed(&hyp_timer_cnt->frq);
}

uint64_t
platform_timer_lp_get_current_ticks(void)
{
	return atomic_load_relaxed(&hyp_timer_cnt->pct);
}

void
platform_timer_lp_visibility(bool visible)
{
	CNTEL0ACR_t acr = CNTEL0ACR_default();

	CNTEL0ACR_set_EL0VCTEN(&acr, visible);
	CNTEL0ACR_set_EL0VTEN(&acr, visible);

	atomic_store_relaxed(&hyp_timer_cnt->el0acr, acr);
}

void
platform_timer_lp_handle_boot_cold_init(void)
{
	size_t timer_size = PGTABLE_HYP_PAGE_SIZE;

	// Allocate hyp_timer_cnt

	virt_range_result_t range = hyp_aspace_allocate(timer_size);
	if (range.e != OK) {
		panic("timer_lp: Allocation failed.");
	}

	hyp_timer_cnt	    = (cntbase_t *)range.r.base;
	virt_hyp_timer_size = range.r.size;

	// Map the low power timer

	pgtable_hyp_start();

	error_t err = pgtable_hyp_map(
		partition_get_private(), (uintptr_t)hyp_timer_cnt, timer_size,
		PLATFORM_HYP_ARCH_TIMER_LP_BASE, PGTABLE_HYP_MEMTYPE_DEVICE,
		PGTABLE_ACCESS_RW, VMSA_SHAREABILITY_NON_SHAREABLE);
	if (err != OK) {
		panic("timer_lp: Mapping failed.");
	}

	pgtable_hyp_commit();

	assert(platform_timer_lp_get_frequency() ==
	       PLATFORM_ARCH_TIMER_LP_FREQ);

	// In this code we are assuming that the generic timer is in sync with
	// the low power timer and therefore, we do not need to convert
	// timeouts.
	static_assert(PLATFORM_ARCH_TIMER_LP_FREQ == PLATFORM_ARCH_TIMER_FREQ,
		      "Arch timer and lp timer must have the same frequency");

	platform_timer_lp_visibility(true);

	// FIXME:
	// Unmap/remap the priviledged timer frame in the HLOS S2 address space
}

void
platform_timer_lp_handle_boot_hypervisor_start(void)
{
	// Create the low power timer IRQ
	hwirq_create_t params = {
		.irq	= PLATFORM_HYP_ARCH_TIMER_LP_IRQ,
		.action = HWIRQ_ACTION_HYP_TIMER_LP,
	};

	hwirq_ptr_result_t ret =
		partition_allocate_hwirq(partition_get_private(), params);

	if (ret.e != OK) {
		panic("Failed to create low power timer IRQ");
	}

	if (object_activate_hwirq(ret.r) != OK) {
		panic("Failed to activate low power timer IRQ");
	}

	irq_enable_shared(ret.r);
}

bool
platform_timer_lp_handle_irq_received(void)
{
	trigger_platform_timer_lp_expiry_event();

	return true;
}

void
platform_timer_lp_set_timeout_and_route(ticks_t timeout, cpu_index_t cpu_index)
{
	MPIDR_EL1_t    mpidr	  = platform_cpu_index_to_mpidr(cpu_index);
	GICD_IROUTER_t phys_route = GICD_IROUTER_default();
	GICD_IROUTER_set_IRM(&phys_route, false);
	GICD_IROUTER_set_Aff0(&phys_route, MPIDR_EL1_get_Aff0(&mpidr));
	GICD_IROUTER_set_Aff1(&phys_route, MPIDR_EL1_get_Aff1(&mpidr));
	GICD_IROUTER_set_Aff2(&phys_route, MPIDR_EL1_get_Aff2(&mpidr));
	GICD_IROUTER_set_Aff3(&phys_route, MPIDR_EL1_get_Aff3(&mpidr));

	if (!GICD_IROUTER_is_equal(current_route, phys_route)) {
		if (gicv3_spi_set_route(PLATFORM_HYP_ARCH_TIMER_LP_IRQ,
					phys_route) != OK) {
			panic("LPTimer: Failed to set the IRQ route!");
		}
		current_route = phys_route;
	}

	platform_timer_lp_set_timeout(timeout);
}

#if defined(MODULE_VM_ROOTVM)
void
platform_timer_lp_handle_rootvm_init(cspace_t *cspace, hyp_env_data_t *hyp_env)
{
	memextent_ptr_result_t m = cspace_lookup_memextent(
		cspace, hyp_env->device_me_capid, CAP_RIGHTS_MEMEXTENT_DERIVE);
	if (compiler_unexpected(m.e != OK)) {
		panic("Failed to find device memextent.");
	}

	memextent_t *parent = m.r;

	memextent_ptr_result_t me_ret = memextent_derive(
		parent, PLATFORM_HYP_ARCH_TIMER_LP_BASE, (size_t)1U << 12,
		MEMEXTENT_MEMTYPE_DEVICE, PGTABLE_ACCESS_RW);
	if (me_ret.e != OK) {
		panic("Failed creation of low power timer memextent");
	}

	object_put_memextent(parent);
}
#endif
