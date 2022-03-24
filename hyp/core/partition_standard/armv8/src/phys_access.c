// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier; BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <hyp_aspace.h>
#include <log.h>
#include <panic.h>
#include <partition.h>
#include <trace.h>
#include <util.h>

#include "event_handlers.h"

void
partition_phys_access_cpu_warm_init(void)
{
#if ARCH_AARCH64_USE_PAN
	__asm__ volatile("msr PAN, 1" ::: "memory");
#else
	// Nothing to do here.
#endif
}

bool
partition_phys_valid(paddr_t paddr, size_t size)
{
	bool ret = true;

	if (util_add_overflows(paddr, size)) {
		ret = false;
		goto out;
	}
	if (paddr >= util_bit(HYP_ASPACE_MAP_DIRECT_BITS)) {
		ret = false;
		goto out;
	}

#if ARCH_AARCH64_USE_PAN
	for (paddr_t check_pa = paddr; check_pa < (paddr + size);
	     check_pa += PGTABLE_HYP_PAGE_SIZE) {
		paddr_t	    pa_lookup;
		MAIR_ATTR_t memattr;
		void	     *check_va = (void *)((uintptr_t)check_pa +
						HYP_ASPACE_PHYSACCESS_OFFSET);
		error_t err = hyp_aspace_va_to_pa_el2_read(check_va, &pa_lookup,
							   &memattr, NULL);

		if (err != OK) {
			LOG(DEBUG, INFO,
			    "partition_phys_valid failed for PA: {:#x}",
			    check_pa);
			ret = false;
			break;
		}

		if (compiler_unexpected(check_pa != pa_lookup)) {
			LOG(ERROR, WARN,
			    "Unexpected look-up result in partition_phys_valid."
			    " PA:{:#x}, looked-up PA: {:#x}",
			    check_pa, pa_lookup);
			panic("partition_phys_valid: Bad look-up result");
		}

		// We map the HYP_ASPACE_PHYSACCESS_OFFSET as device type when
		// invalid.
		if ((memattr & ~MAIR_ATTR_DEVICE_MASK) == 0U) {
			ret = false;
			break;
		}
	}
#endif

out:
	return ret;
}

void *
partition_phys_map(paddr_t paddr, size_t size)
{
#if ARCH_AARCH64_USE_PAN
	assert(!util_add_overflows(paddr, size));

#if defined(VERBOSE) && VERBOSE
	assert(partition_phys_valid(paddr, size));
#endif

	return (void *)((uintptr_t)paddr + HYP_ASPACE_PHYSACCESS_OFFSET);
#else
#error not implemented: locate allocator and convert paddr to virtual, or map
#endif
}

void
partition_phys_access_enable(const void *ptr)
{
#if ARCH_AARCH64_USE_PAN
	(void)ptr;
	__asm__ volatile("msr PAN, 0" ::: "memory");
#else
	// Nothing to do here.
#endif
}

void
partition_phys_access_disable(const void *ptr)
{
#if ARCH_AARCH64_USE_PAN
	(void)ptr;
	__asm__ volatile("msr PAN, 1" ::: "memory");
#else
	// Nothing to do here.
#endif
}

void
partition_phys_unmap(const void *vaddr, paddr_t paddr, size_t size)
{
#if ARCH_AARCH64_USE_PAN
	(void)vaddr;
	(void)paddr;
	(void)size;

	// Nothing to do here.
#else
#error not implemented: unmap if mapped above
#endif
}
