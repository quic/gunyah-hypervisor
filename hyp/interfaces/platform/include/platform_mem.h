// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Add platform specific memory to the root VM partition.
//
// The root VM partition is created without any heap (besides possibly a
// minimal amount of memory provided by the hyp_partition to seed it). The
// platform must provide the rest of the initial memory for the root partition.
void
platform_add_root_heap(partition_t *partition) REQUIRE_PREEMPT_DISABLED;

// Return platform DDR (normal memory) information.
// May only be called after platform_ram_probe().
platform_ram_info_t *
platform_get_ram_info(void);

// Should be called once on boot to probe RAM information.
// This function expects to be able map to the hyp pagetable.
error_t
platform_ram_probe(void);

#if defined(ARCH_ARM)
// Check whether a platform VM page table is undergoing a break-before-make.
//
// This function is called while handling translation aborts in an address space
// that has the platform_pgtable flag set. It should return true if there is any
// possibility that an in-progress update of the VM page table has unmapped
// pages temporarily as part of an architecturally required break-before-make
// sequence while changing the block size of an existing mapping.
//
// If this function returns true, any translation abort in a read-only address
// space will be retried until either the fault is resolved or this function
// returns false. The platform code must therefore only return true from this
// function for a bounded period of time.
//
// Note that it is not necessary for the platform code to handle the race
// between completion of the break-before-make and reporting of a fault that
// occurred before it completed. That race will be handled by the caller.
bool
platform_pgtable_undergoing_bbm(void);
#endif
