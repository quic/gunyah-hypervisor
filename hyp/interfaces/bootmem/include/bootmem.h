// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Allocate a block of memory
void_ptr_result_t
bootmem_allocate(size_t size, size_t align);

// Allocates all the remaining bootmem memory.
//
// This is used to hand off the remaining memory to the root partition memory
// allocator.
void_ptr_result_t
bootmem_allocate_remaining(size_t *size);

// Return the entire bootmem region, including any memory that has been
// allocated by the functions above.
//
// This is called while bootstrapping the memory ownership database.
void *
bootmem_get_region(size_t *size);
