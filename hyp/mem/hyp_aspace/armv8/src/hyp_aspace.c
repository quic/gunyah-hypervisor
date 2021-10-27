// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier; BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypregisters.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <hyp_aspace.h>
#include <log.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <pgtable.h>
#include <platform_mem.h>
#include <spinlock.h>
#include <trace.h>
#include <util.h>

#include <asm/barrier.h>
#include <asm/cache.h>
#include <asm/cpu.h>

#include "event_handlers.h"

#define HYP_ASPACE_ALLOCATE_BITS (25U)
#define HYP_ASPACE_ALLOCATE_SIZE (size_t) util_bit(HYP_ASPACE_ALLOCATE_BITS)
#define HYP_ASPACE_NUM_REGIONS	 (256U)
#define HYP_ASPACE_TOTAL_REGION_SIZE                                           \
	(HYP_ASPACE_NUM_REGIONS * HYP_ASPACE_ALLOCATE_SIZE)

static spinlock_t      hyp_aspace_direct_lock;
static const uintptr_t hyp_aspace_direct_end =
	util_bit(HYP_ASPACE_MAP_DIRECT_BITS) - 1;

static spinlock_t hyp_aspace_alloc_lock;
static uintptr_t  hyp_aspace_alloc_base;
static uintptr_t  hyp_aspace_alloc_end;
static _Atomic BITMAP_DECLARE(HYP_ASPACE_NUM_REGIONS, hyp_aspace_regions);

extern const char image_virt_start;
extern const char image_virt_last;
extern const char image_phys_start;
extern const char image_phys_last;

static const uintptr_t virt_start     = (paddr_t)&image_virt_start;
static const uintptr_t virt_end	      = (paddr_t)&image_virt_last;
static const paddr_t   hyp_phys_start = (paddr_t)&image_phys_start;
static const paddr_t   hyp_phys_last  = (paddr_t)&image_phys_last;

static_assert(ARCH_AARCH64_USE_VHE || !ARCH_AARCH64_USE_PAN,
	      "PAN is not useful when VHE is disabled");

void
hyp_aspace_handle_boot_cold_init(void)
{
	spinlock_init(&hyp_aspace_direct_lock);

	partition_t *hyp_partition = partition_get_private();

#if ARCH_AARCH64_USE_PAN
	// Congruent (constant offset) mappings to support physical address
	// access (partition_phys_*).
	//
	// Access rights are set to PGTABLE_ACCESS_NONE, which creates mappings
	// that can only be accessed with PSTATE.PAN cleared.
	//
	// First, map the kernel image, assuming that all of the initial page
	// tables are within its physical memory. This should be sufficient to
	// allow partition_phys_access_begin to work, so we can do other page
	// table operations with the private partition.
	size_t phys_size = (size_t)(hyp_phys_last - hyp_phys_start + 1U);

	pgtable_hyp_start();
	error_t err = pgtable_hyp_map(
		hyp_partition,
		(uintptr_t)hyp_phys_start + HYP_ASPACE_PHYSACCESS_OFFSET,
		phys_size, hyp_phys_start, PGTABLE_HYP_MEMTYPE_WRITEBACK,
		PGTABLE_ACCESS_NONE, VMSA_SHAREABILITY_INNER_SHAREABLE);
	assert(err == OK);
	pgtable_hyp_commit();
#else // !ARCH_AARCH64_USE_PAN
#error Non-PAN physical address access is not yet implemented
#endif

	spinlock_init(&hyp_aspace_alloc_lock);
	hyp_aspace_alloc_base =
		util_p2align_up(virt_end + 1U, HYP_ASPACE_ALLOCATE_BITS);

	// Check for overflow when virt_end is at the end of the address space
#if ARCH_AARCH64_USE_VHE
	if (util_add_overflows(virt_end, 1U)) {
		hyp_aspace_alloc_base = -util_bit(HYP_ASPACE_HIGH_BITS);
	}
#else
	if (hyp_aspace_alloc_base >= util_bit(HYP_ASPACE_BITS)) {
		hyp_aspace_alloc_base = hyp_aspace_direct_end + 1U;
	}
#endif

	// Calculate the end address of the allocation region
	hyp_aspace_alloc_end =
		hyp_aspace_alloc_base + HYP_ASPACE_TOTAL_REGION_SIZE - 1U;

	// Check for overflowing end address
	if (hyp_aspace_alloc_end < hyp_aspace_alloc_base) {
		// Overflow. Shift the range so it is below virt_start. Note
		// that we don't just start at the minimum address because we
		// want to preserve the random bits we took from virt_end.
		//
		// Note that this assumes that HYP_ASPACE_TOTAL_REGION_SIZE is
		// less than half of the available address space. It would
		// be simpler to make it the entire region, and reserve the
		// virt_start--virt_end range in the bitmap at boot time.
#if ARCH_AARCH64_USE_VHE
		hyp_aspace_alloc_base = hyp_aspace_alloc_end + 1U -
					util_bit(HYP_ASPACE_HIGH_BITS);
		assert(hyp_aspace_alloc_base >=
		       -util_bit(HYP_ASPACE_HIGH_BITS));
#else
		hyp_aspace_alloc_base =
			hyp_aspace_alloc_end + 1U + hyp_aspace_direct_end + 1U;
		assert(hyp_aspace_alloc_base >= hyp_aspace_direct_end + 1U);
#endif
		hyp_aspace_alloc_end = hyp_aspace_alloc_base +
				       HYP_ASPACE_TOTAL_REGION_SIZE - 1U;

		assert(hyp_aspace_alloc_end > hyp_aspace_alloc_base);
		assert(hyp_aspace_alloc_end < virt_start);
	}
}

#if ARCH_AARCH64_USE_PAN
error_t
hyp_aspace_handle_partition_add_ram_range(paddr_t phys_base, size_t size)
{
	partition_t *hyp_partition = partition_get_private();

	assert(util_is_baligned(phys_base, PGTABLE_HYP_PAGE_SIZE));
	assert(util_is_baligned(size, PGTABLE_HYP_PAGE_SIZE));

	if (util_add_overflows(phys_base, size - 1U) ||
	    ((phys_base + size - 1U) >= (1UL << HYP_ASPACE_MAP_DIRECT_BITS))) {
		LOG(ERROR, WARN, "Failed to add high memory: {:x}..{:x}\n",
		    phys_base, phys_base + size - 1U);
		return ERROR_ADDR_INVALID;
	}

	pgtable_hyp_start();

	error_t err = pgtable_hyp_remap(
		hyp_partition,
		(uintptr_t)phys_base + HYP_ASPACE_PHYSACCESS_OFFSET, size,
		phys_base, PGTABLE_HYP_MEMTYPE_WRITEBACK, PGTABLE_ACCESS_NONE,
		VMSA_SHAREABILITY_INNER_SHAREABLE);

	pgtable_hyp_commit();

	return err;
}

error_t
hyp_aspace_handle_partition_remove_ram_range(paddr_t phys_base, size_t size)
{
	partition_t *hyp_partition = partition_get_private();

	char *virt = (void *)(phys_base + HYP_ASPACE_PHYSACCESS_OFFSET);

	assert(util_is_baligned(phys_base, PGTABLE_HYP_PAGE_SIZE));
	assert(util_is_baligned(size, PGTABLE_HYP_PAGE_SIZE));

	pgtable_hyp_start();

	// Remap the memory as DEVICE so that no speculative reads occur.
	pgtable_hyp_remap(hyp_partition, (uintptr_t)virt, size, phys_base,
			  PGTABLE_HYP_MEMTYPE_DEVICE, PGTABLE_ACCESS_RW,
			  VMSA_SHAREABILITY_INNER_SHAREABLE);

	pgtable_hyp_commit();

	// Clean the memory range being removed to ensure no future write-backs
	// occur. No need to remap since speculative reads after the cache
	// clean won't be written back.
	CACHE_CLEAN_INVALIDATE_RANGE(virt, size);

	return OK;
}
#endif // ARCH_AARCH64_USE_PAN

virt_range_result_t
hyp_aspace_allocate(size_t min_size)
{
	virt_range_result_t ret;

	size_t	size = util_p2align_up(min_size, HYP_ASPACE_ALLOCATE_BITS);
	count_t bits = (count_t)(size >> HYP_ASPACE_ALLOCATE_BITS);

	// Contiguous allocation of multiple chunks is not implemented yet
	if (bits != 1U) {
		ret = virt_range_result_error(ERROR_ARGUMENT_SIZE);
		goto out;
	}

	// Find and set a cleared bit in the allocation bitmap
	index_t bit;
	do {
		// This should allocate randomly, not with ffc
		if (!bitmap_atomic_ffc(hyp_aspace_regions,
				       HYP_ASPACE_NUM_REGIONS, &bit)) {
			ret = virt_range_result_error(ERROR_NOMEM);
			goto out;
		}
	} while (bitmap_atomic_test_and_set(hyp_aspace_regions, bit,
					    memory_order_relaxed));

	uintptr_t virt =
		hyp_aspace_alloc_base + (bit << HYP_ASPACE_ALLOCATE_BITS);
	ret = virt_range_result_ok(
		(virt_range_t){ .base = virt, .size = size });

	// Preallocate shared page table levels before mapping
	spinlock_acquire(&hyp_aspace_alloc_lock);
	pgtable_hyp_preallocate(partition_get_private(), virt,
				HYP_ASPACE_ALLOCATE_SIZE);
	spinlock_release(&hyp_aspace_alloc_lock);

out:
	return ret;
}

error_t
hyp_aspace_deallocate(partition_t *partition, virt_range_t virt_range)
{
	uintptr_t virt = virt_range.base;
	size_t	  size = virt_range.size;
	error_t	  err;
	index_t	  bit;

	if ((virt < hyp_aspace_alloc_base) || (virt > hyp_aspace_alloc_end)) {
		err = ERROR_ARGUMENT_INVALID;
		goto deallocate_error;
	}

	if (!util_is_p2aligned(virt, HYP_ASPACE_ALLOCATE_BITS)) {
		err = ERROR_ARGUMENT_ALIGNMENT;
		goto deallocate_error;
	}

	if (size != HYP_ASPACE_ALLOCATE_SIZE) {
		err = ERROR_ARGUMENT_SIZE;
		goto deallocate_error;
	}

	bit = (index_t)((virt - hyp_aspace_alloc_base) >>
			HYP_ASPACE_ALLOCATE_BITS);
	assert(bit < HYP_ASPACE_NUM_REGIONS);

	spinlock_acquire(&hyp_aspace_alloc_lock);
	// FIXME: Rather than unmap, this should check that no
	// page tables owned by the given partition remain.
	pgtable_hyp_start();
	pgtable_hyp_unmap(partition, virt, HYP_ASPACE_ALLOCATE_SIZE,
			  HYP_ASPACE_ALLOCATE_SIZE);
	pgtable_hyp_unmap(partition_get_private(), virt,
			  HYP_ASPACE_ALLOCATE_SIZE,
			  PGTABLE_HYP_UNMAP_PRESERVE_NONE);
	pgtable_hyp_commit();
	spinlock_release(&hyp_aspace_alloc_lock);

	(void)bitmap_atomic_test_and_clear(hyp_aspace_regions, bit,
					   memory_order_relaxed);
	err = OK;

deallocate_error:
	return err;
}

static error_t
hyp_aspace_check_region(uintptr_t virt, size_t size)
{
	error_t err;

	if (!util_is_baligned(virt, PGTABLE_HYP_PAGE_SIZE) ||
	    !util_is_baligned(size, PGTABLE_HYP_PAGE_SIZE)) {
		err = ERROR_ARGUMENT_ALIGNMENT;
	} else if (util_add_overflows(virt, size)) {
		err = ERROR_ARGUMENT_INVALID;
	} else if (virt + size - 1U > hyp_aspace_direct_end) {
		err = ERROR_ARGUMENT_INVALID;
	} else {
		err = OK;
	}

	return err;
}

error_t
hyp_aspace_map_direct(paddr_t phys, size_t size, pgtable_access_t access,
		      pgtable_hyp_memtype_t memtype, vmsa_shareability_t share)
{
	error_t	  err;
	uintptr_t virt = (uintptr_t)phys;

	if ((paddr_t)virt != phys) {
		// Physical address truncated by cast to uintptr_t
		// (possible on 32-bit ARMv8 or ARMv7-VE)
		err = ERROR_ARGUMENT_INVALID;
		goto map_error;
	}

	err = hyp_aspace_check_region(virt, size);
	if (err != OK) {
		goto map_error;
	}

	spinlock_acquire(&hyp_aspace_direct_lock);
	pgtable_hyp_start();
	err = pgtable_hyp_map(partition_get_private(), virt, size, phys,
			      memtype, access, share);
	pgtable_hyp_commit();
	spinlock_release(&hyp_aspace_direct_lock);

map_error:
	return err;
}

error_t
hyp_aspace_unmap_direct(paddr_t phys, size_t size)
{
	uintptr_t virt = (uintptr_t)phys;
	error_t	  err;

	if ((paddr_t)virt != phys) {
		// Physical address truncated by cast to uintptr_t
		// (possible on 32-bit ARMv8 or ARMv7-VE)
		err = ERROR_ARGUMENT_INVALID;
		goto unmap_error;
	}

	err = hyp_aspace_check_region(virt, size);
	if (err != OK) {
		goto unmap_error;
	}

	spinlock_acquire(&hyp_aspace_direct_lock);
	pgtable_hyp_start();
	pgtable_hyp_unmap(partition_get_private(), virt, size,
			  PGTABLE_HYP_UNMAP_PRESERVE_NONE);

	pgtable_hyp_commit();
	spinlock_release(&hyp_aspace_direct_lock);

unmap_error:
	return err;
}

lookup_result_t
hyp_aspace_is_mapped(uintptr_t virt, size_t size, pgtable_access_t access)
{
	int		map_count = 0;
	lookup_result_t ret	  = lookup_result_default();

	paddr_t		      phys, expected_phys;
	pgtable_hyp_memtype_t curr_memtype, prev_memtype;
	pgtable_access_t      curr_access, prev_access;

	bool mapped, have_access, first_lookup = true, consistent = true;
	bool direct = true, contiguous = true;

	if (access == PGTABLE_ACCESS_NONE) {
		goto is_mapped_return;
	}

	if (util_add_overflows(virt, size - 1U)) {
		goto is_mapped_return;
	}

	// Set dummy values to stop warnings
	expected_phys = 0;
	prev_memtype  = PGTABLE_HYP_MEMTYPE_WRITEBACK;
	prev_access   = PGTABLE_ACCESS_NONE;

	size_t offset = 0U;
	while (offset < size) {
		uintptr_t curr = virt + offset;
		size_t	  mapped_size;
		mapped = pgtable_hyp_lookup(curr, &phys, &mapped_size,
					    &curr_memtype, &curr_access);
		if (mapped) {
			size_t mapping_offset = curr & (mapped_size - 1U);
			phys += mapping_offset;
			mapped_size -= mapping_offset;

			if (!first_lookup) {
				consistent = consistent &&
					     (expected_phys == phys) &&
					     (prev_memtype == curr_memtype) &&
					     (prev_access == curr_access);
			} else {
				first_lookup = false;
			}

			have_access = ((curr_access & access) == access);
			direct	    = direct && (curr == phys);
			contiguous  = contiguous && have_access;

			if (have_access) {
				map_count++;
			}

			expected_phys = phys + mapped_size;
			prev_memtype  = curr_memtype;
			prev_access   = curr_access;
		} else {
			contiguous  = false;
			mapped_size = util_balign_up(curr + 1U,
						     PGTABLE_HYP_PAGE_SIZE) -
				      curr;
			expected_phys += mapped_size;
		}

		if (util_add_overflows(offset, mapped_size)) {
			break;
		}
		offset += mapped_size;
	}

	if (map_count > 0) {
		lookup_result_set_mapped(&ret, true);
		lookup_result_set_consistent(&ret, consistent);
		lookup_result_set_contiguous(&ret, contiguous);
		lookup_result_set_direct(&ret, direct);
	}

is_mapped_return:
	return ret;
}

error_t
hyp_aspace_va_to_pa_el2_read(void *addr, paddr_t *pa, MAIR_ATTR_t *memattr,
			     vmsa_shareability_t *shareability)
{
	bool success;

	PAR_EL1_RAW_t saved_par =
		register_PAR_EL1_RAW_read_ordered(&asm_ordering);

	__asm__ volatile("at	S1E2R, %[addr]		;"
			 "isb				;"
			 : "+m"(asm_ordering)
			 : [addr] "r"(addr));

	PAR_EL1_RAW_t par_raw =
		register_PAR_EL1_RAW_read_ordered(&asm_ordering);

	PAR_EL1_F0_t par = PAR_EL1_F0_cast(PAR_EL1_RAW_raw(par_raw));
	success		 = !PAR_EL1_F0_get_F(&par);

	if (success) {
		if (pa != NULL) {
			*pa = PAR_EL1_F0_get_PA(&par);
			*pa |= (paddr_t)addr & 0xfffU;
		}
		if (memattr != NULL) {
			*memattr = PAR_EL1_F0_get_ATTR(&par);
		}
		if (shareability != NULL) {
			*shareability = PAR_EL1_F0_get_SH(&par);
		}
	}

	register_PAR_EL1_RAW_write_ordered(saved_par, &asm_ordering);

	return success ? OK : ERROR_ADDR_INVALID;
}

error_t
hyp_aspace_va_to_pa_el2_write(void *addr, paddr_t *pa, MAIR_ATTR_t *memattr,
			      vmsa_shareability_t *shareability)
{
	bool success;

	PAR_EL1_RAW_t saved_par =
		register_PAR_EL1_RAW_read_ordered(&asm_ordering);

	__asm__ volatile("at	S1E2W, %[addr]		;"
			 "isb				;"
			 : "+m"(asm_ordering)
			 : [addr] "r"(addr));

	PAR_EL1_RAW_t par_raw =
		register_PAR_EL1_RAW_read_ordered(&asm_ordering);

	PAR_EL1_F0_t par = PAR_EL1_F0_cast(PAR_EL1_RAW_raw(par_raw));
	success		 = !PAR_EL1_F0_get_F(&par);

	if (success) {
		if (pa != NULL) {
			*pa = PAR_EL1_F0_get_PA(&par);
			*pa |= (paddr_t)addr & 0xfffU;
		}
		if (memattr != NULL) {
			*memattr = PAR_EL1_F0_get_ATTR(&par);
		}
		if (shareability != NULL) {
			*shareability = PAR_EL1_F0_get_SH(&par);
		}
	}

	register_PAR_EL1_RAW_write_ordered(saved_par, &asm_ordering);

	return success ? OK : ERROR_ADDR_INVALID;
}
