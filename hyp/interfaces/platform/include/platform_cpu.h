// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Platform wrappers for SMP support.

// Power on the specified CPU.
error_t
platform_cpu_on(cpu_index_t cpu);

// Power off the calling CPU. Returns after the CPU is powered on again by a
// platform_cpu_on() call on another CPU.
void
platform_cpu_off(void);

// The system and CPUs are reset, and will restart from the firmware/bootloader.
void
platform_system_reset(void);

// Suspend the calling CPU until a wakeup event occurs.
//
// The argument is a platform-specific power state value which represents the
// deepest sleep state this call is permitted to enter. On success, the result
// is true if the CPU woke from a power-off state, and false if it either
// woke from a retention state or returned without suspending due to a pending
// wakeup event.
//
// This may fail with ERROR_ARGUMENT_INVALID if the power state argument is
// not understood or not permitted by the platform, or ERROR_DENIED if the
// attempt to sleep was aborted due to a pending wakeup on another CPU in the
// same power domain.
bool_result_t
platform_cpu_suspend(platform_power_state_t power_state);

// Set the suspend mode used by the hypervisor
//
// This function can only return OK if the following conditions are met:
// If switching from PC to OSI mode:
//	- All cores are either Running, OFF (using CPU_OFF or not booted yet),
//	or Suspended (using CPU_DEFAULT_SUSPEND)
//	- None of the processors has called CPU_SUSPEND since the last change of
//	mode or boot.
// If switching from OSI to PC mode: all cores other than the calling one are
// OFF (using CPU_OFF or not booted yet)
error_t
platform_psci_set_suspend_mode(psci_mode_t mode);

#if defined(PLATFORM_PSCI_DEFAULT_SUSPEND)
// Suspend the calling CPU until a wakeup event occurs. Similar to cpu suspend,
// but the caller does not need to specify a power state parameter.
bool_result_t
platform_cpu_default_suspend(void);
#endif

#if defined(ARCH_ARM) && (ARCH_ARM_VER >= 80)
psci_mpidr_t
platform_cpu_index_to_mpidr(cpu_index_t cpu);

cpu_index_result_t
platform_cpu_mpidr_to_index(psci_mpidr_t mpidr);
#endif
