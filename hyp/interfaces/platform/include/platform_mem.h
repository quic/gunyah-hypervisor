// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Add platform specific memory to the root VM partition.
//
// The root VM partition is created without any heap (besides possibly a
// minimal amount of memory provided by the hyp_partition to seed it). The
// platform must provide the rest of the initial memory for the root partition.
void
platform_add_root_heap(partition_t *partition);

// Return platform DDR (normal memory) information.
// May only be called after platform_ram_probe().
platform_ram_info_t *
platform_get_ram_info(void);

// Should be called once on boot to probe RAM information.
// This function expects to be able map to the hyp pagetable.
error_t
platform_ram_probe(void);
