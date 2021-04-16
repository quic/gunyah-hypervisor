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

void
allocator_boot_handle_boot_runtime_first_init(void)
{
	assert((uintptr_t)&heap_private_end > (uintptr_t)&heap_private_start);

	void * base = &heap_private_start;
	size_t size = (size_t)(&heap_private_end - &heap_private_start);

	assert(base != NULL);
	assert(size >= 0x1000);

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
	if (free == 0) {
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
