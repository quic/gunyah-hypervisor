// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <bootmem.h>
#include <util.h>

#include "event_handlers.h"

static bootmem_allocator_t bootmem_allocator;

// For now the hypervisor private heap is statically defined in the linker
// script. The intention is to replace this with dynamically determined memory -
// such as through boot configuration structures.
extern uint8_t heap_private_start;
extern uint8_t heap_private_end;
extern uint8_t image_virt_end;

void
allocator_boot_handle_boot_runtime_first_init(void)
{
	assert((uintptr_t)&heap_private_end > (uintptr_t)&heap_private_start);

	static_assert(
		PLATFORM_HEAP_PRIVATE_SIZE <= PLATFORM_RW_DATA_SIZE,
		"PLATFORM_HEAP_PRIVATE_SIZE must be <= PLATFORM_RW_DATA_SIZE");
	static_assert(PLATFORM_RW_DATA_SIZE >= 0x200000,
		      "PLATFORM_RW_DATA_SIZE must be >= 2MB");

	// We only give heap within the first 2MB RW page to the bootmem. We
	// will map the rest of the heap during the hyp_aspace init.
	void	 *base	  = &heap_private_start;
	uintptr_t map_end = util_balign_up((uintptr_t)base, 0x200000U);

	uintptr_t hyp_priv_end =
		(((uintptr_t)&image_virt_end) - (size_t)PLATFORM_RW_DATA_SIZE) +
		(size_t)PLATFORM_HEAP_PRIVATE_SIZE;
	uintptr_t end = util_min(map_end, hyp_priv_end);

	assert((uintptr_t)base < end);
	size_t size = end - (uintptr_t)base;

	assert(base != NULL);
	assert(size >= 0x1000U);

	bootmem_allocator.pool_base    = (uint8_t *)base;
	bootmem_allocator.pool_size    = size;
	bootmem_allocator.alloc_offset = 0;
}

void_ptr_result_t
bootmem_allocate(size_t size, size_t align)
{
	assert(bootmem_allocator.alloc_offset <= bootmem_allocator.pool_size);
	uintptr_t loc = (uintptr_t)bootmem_allocator.pool_base;

	assert(!util_add_overflows(loc, bootmem_allocator.alloc_offset));
	loc += bootmem_allocator.alloc_offset;

	if (!util_is_p2_or_zero(align)) {
		return void_ptr_result_error(ERROR_ARGUMENT_ALIGNMENT);
	}
	if (util_is_p2(align)) {
		loc = util_balign_up(loc, align);
	}

	size_t free = bootmem_allocator.pool_size -
		      (loc - (uintptr_t)bootmem_allocator.pool_base);

	if (size > free) {
		return void_ptr_result_error(ERROR_NOMEM);
	}

	bootmem_allocator.alloc_offset =
		(loc - (uintptr_t)bootmem_allocator.pool_base + size);

	return void_ptr_result_ok((void *)loc);
}

void_ptr_result_t
bootmem_allocate_remaining(size_t *size)
{
	assert(size != NULL);

	assert(bootmem_allocator.alloc_offset <= bootmem_allocator.pool_size);
	size_t free =
		bootmem_allocator.pool_size - bootmem_allocator.alloc_offset;
	if (free == 0U) {
		return void_ptr_result_error(ERROR_NOMEM);
	}
	*size = free;

	uintptr_t loc = (uintptr_t)bootmem_allocator.pool_base;
	assert(!util_add_overflows(loc, bootmem_allocator.alloc_offset));
	loc += bootmem_allocator.alloc_offset;

	bootmem_allocator.alloc_offset = bootmem_allocator.pool_size;

	return void_ptr_result_ok((void *)loc);
}

void *
bootmem_get_region(size_t *size)
{
	*size = bootmem_allocator.pool_size;
	return bootmem_allocator.pool_base;
}
