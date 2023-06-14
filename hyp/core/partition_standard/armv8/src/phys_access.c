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

static bool
memory_attr_type_check(MAIR_ATTR_t memattr, paddr_t check_pa)
{
	bool ret = true;

	switch (memattr) {
	case MAIR_ATTR_DEVICE_NGNRNE:
	case MAIR_ATTR_DEVICE_NGNRE:
	case MAIR_ATTR_DEVICE_NGRE:
	case MAIR_ATTR_DEVICE_GRE:
		ret = false;
		break;
	case MAIR_ATTR_NORMAL_NC:
	case MAIR_ATTR_NORMAL_WB_OUTER_NC:
#if defined(ARCH_ARM_FEAT_MTE)
	case MAIR_ATTR_TAGGED_NORMAL_WB:
#endif
	case MAIR_ATTR_NORMAL_WB:
		break;
	case MAIR_ATTR_DEVICE_NGNRNE_XS:
	case MAIR_ATTR_DEVICE_NGNRE_XS:
	case MAIR_ATTR_DEVICE_NGRE_XS:
	case MAIR_ATTR_DEVICE_GRE_XS:
	default:
		LOG(ERROR, WARN,
		    "Unexpected look-up result in partition_phys_valid."
		    " PA:{:#x}, attr : {:#x}",
		    check_pa, (register_t)memattr);
		ret = false;
		break;
	}

	return ret;
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

	for (paddr_t check_pa = paddr; check_pa < (paddr + size);
	     check_pa += PGTABLE_HYP_PAGE_SIZE) {
		paddr_t	    pa_lookup;
		MAIR_ATTR_t memattr;
		void	   *check_va = (void *)((uintptr_t)check_pa +
						hyp_aspace_get_physaccess_offset());
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

		// We map the hyp_aspace_physaccess_offset as device type when
		// invalid.
		ret = memory_attr_type_check(memattr, check_pa);

		if (!ret) {
			break;
		}
	}

out:
	return ret;
}

void *
partition_phys_map(paddr_t paddr, size_t size)
{
	assert(!util_add_overflows(paddr, size));
	assert_debug(partition_phys_valid(paddr, size));

	return (void *)((uintptr_t)paddr + hyp_aspace_get_physaccess_offset());
}

void
partition_phys_access_enable(const void *ptr)
{
	(void)ptr;
#if ARCH_AARCH64_USE_PAN
	__asm__ volatile("msr PAN, 0" ::: "memory");
#else
	// Nothing to do here.
#endif
}

void
partition_phys_access_disable(const void *ptr)
{
	(void)ptr;
#if ARCH_AARCH64_USE_PAN
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
	(void)vaddr;
	(void)paddr;
	(void)size;

	// Nothing to do here.
#endif
}
