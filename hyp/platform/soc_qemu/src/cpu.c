// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <compiler.h>
#include <cpulocal.h>
#include <idle.h>
#include <panic.h>
#include <partition.h>
#include <platform_cpu.h>
#include <platform_psci.h>
#include <preempt.h>
#include <psci.h>
#include <thread.h>
#include <util.h>

#include "event_handlers.h"
#include "psci_smc.h"

// The entry points are really functions, but we don't use function types for
// them because they are never directly called from C, and using function types
// here would force us to break MISRA required rule 11.1 in platform_cpu_on().
extern const char soc_qemu_entry_cold_secondary;
extern const char soc_qemu_entry_warm;

CPULOCAL_DECLARE_STATIC(bool, cpu_started);

void
soc_qemu_handle_boot_cpu_cold_init(cpu_index_t cpu)
{
	CPULOCAL_BY_INDEX(cpu_started, cpu) = true;
}

bool
platform_cpu_exists(cpu_index_t cpu)
{
	assert(cpu < PLATFORM_MAX_CORES);

	return compiler_expected((util_bit(cpu) & PLATFORM_USABLE_CORES) != 0U);
}

error_t
platform_cpu_on(cpu_index_t cpu)
{
	MPIDR_EL1_t mpidr  = platform_cpu_index_to_mpidr(cpu);
	thread_t   *thread = idle_thread_for(cpu);
	uintptr_t   entry_virt =
		  CPULOCAL_BY_INDEX(cpu_started, cpu)
			  ? (uintptr_t)&soc_qemu_entry_warm
			  : (uintptr_t)&soc_qemu_entry_cold_secondary;
	psci_mpidr_t psci_mpidr = psci_mpidr_default();
	psci_mpidr_set_Aff0(&psci_mpidr, MPIDR_EL1_get_Aff0(&mpidr));
	psci_mpidr_set_Aff1(&psci_mpidr, MPIDR_EL1_get_Aff1(&mpidr));
	psci_mpidr_set_Aff2(&psci_mpidr, MPIDR_EL1_get_Aff2(&mpidr));
	psci_mpidr_set_Aff3(&psci_mpidr, MPIDR_EL1_get_Aff3(&mpidr));
	return psci_smc_cpu_on(psci_mpidr,
			       partition_virt_to_phys(partition_get_private(),
						      entry_virt),
			       (uintptr_t)thread);
}

static noreturn register_t
psci_smc_system_reset_arg(register_t unused)
{
	(void)unused;

	psci_smc_system_reset();

	panic("psci_smc_system_reset failed!");
}

void
platform_system_reset(void)
{
	thread_freeze(psci_smc_system_reset_arg, 0, 0);
}

static noreturn register_t
psci_smc_cpu_off_arg(register_t unused)
{
	(void)unused;

	psci_smc_cpu_off();

	panic("psci_smc_cpu_off failed!");
}

void
platform_cpu_off(void)
{
	assert(idle_is_current());

	thread_freeze(psci_smc_cpu_off_arg, 0U, 0U);
}

static register_t
psci_smc_cpu_suspend_arg(register_t power_state) REQUIRE_PREEMPT_DISABLED
{
	thread_t *idle = idle_thread();

	paddr_t entry_phys = partition_virt_to_phys(
		partition_get_private(), (uintptr_t)&soc_qemu_entry_warm);

	error_t ret =
		psci_smc_cpu_suspend(power_state, entry_phys, (register_t)idle);

	return (register_t)ret;
}

bool_result_t
platform_cpu_suspend(psci_suspend_powerstate_t power_state)
{
	register_t ret;

	assert(idle_is_current());

	ret = thread_freeze(psci_smc_cpu_suspend_arg,
			    psci_suspend_powerstate_raw(power_state), ~0UL);

	return (ret == 0UL)    ? bool_result_ok(false)
	       : (ret == ~0UL) ? bool_result_ok(true)
			       : bool_result_error((error_t)ret);
}

error_t
platform_psci_set_suspend_mode(psci_mode_t mode)
{
	return psci_smc_psci_set_suspend_mode(mode);
}

#if defined(PLATFORM_PSCI_DEFAULT_SUSPEND)
static register_t
psci_smc_cpu_default_suspend_arg(register_t unused)
{
	(void)unused;

	thread_t *idle = idle_thread();

	paddr_t entry_phys = partition_virt_to_phys(
		partition_get_private(), (uintptr_t)&soc_qemu_entry_warm);

	error_t ret =
		psci_smc_cpu_default_suspend(entry_phys, (register_t)idle);

	return (register_t)ret;
}

bool_result_t
platform_cpu_default_suspend(void)
{
	register_t ret;

	assert(idle_is_current());
	ret = thread_freeze(psci_smc_cpu_default_suspend_arg, 0UL, ~0UL);

	return (ret == 0UL)    ? bool_result_ok(false)
	       : (ret == ~0UL) ? bool_result_ok(true)
			       : bool_result_error((error_t)ret);
}
#endif
