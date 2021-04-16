// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Arch-independent boot entry points.
//
// These are called from the arch-specific assembly entry points. They are
// responsible for triggering all of the boot module's events, other than the
// two boot_runtime_*_init which must be triggered directly from assembly to
// prevent problematic compiler optimisations.

// First power-on of the boot CPU.
noreturn void
boot_cold_init(cpu_index_t cpu);

// First power-on of any non-boot CPU.
noreturn void
boot_secondary_init(cpu_index_t cpu);

// Warm (second or later) power-on of any CPU.
noreturn void
boot_warm_init(void);

// Add address range to free ranges in env_data
error_t
boot_add_free_range(paddr_t base, size_t size, void *arg);
