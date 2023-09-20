// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypregisters.h>

#include <compiler.h>
#include <partition.h>
#include <pgtable.h>
#include <rcu.h>
#include <util.h>

#include <asm/barrier.h>
#include <asm/cache.h>
#include <asm/cpu.h>

#include "useraccess.h"

static void
useraccess_clean_range(const uint8_t *va, size_t size)
{
	CACHE_CLEAN_RANGE(va, size);
}

static void
useraccess_clean_invalidate_range(const uint8_t *va, size_t size)
{
	CACHE_CLEAN_INVALIDATE_RANGE(va, size);
}

static size_t
useraccess_copy_from_to_translated_pa(PAR_EL1_t par, gvaddr_t guest_va,
				      size_t page_size, size_t page_offset,
				      bool from_guest, void *hyp_buf,
				      size_t remaining)
{
	paddr_t guest_pa = PAR_EL1_F0_get_PA(&par.f0);
	guest_pa |= (paddr_t)guest_va & (page_size - 1U);

	size_t mapped_size = page_size - page_offset;
	void  *va	   = partition_phys_map(guest_pa, mapped_size);

	MAIR_ATTR_t attr = PAR_EL1_F0_get_ATTR(&par.f0);
	bool writeback = ((index_t)attr | (index_t)MAIR_ATTR_ALLOC_HINT_MASK) ==
			 (index_t)MAIR_ATTR_NORMAL_WB;
#if defined(ARCH_ARM_FEAT_MTE)
	writeback = writeback || (attr == MAIR_ATTR_TAGGED_NORMAL_WB);
#endif

	partition_phys_access_enable(va);

	if (compiler_unexpected(from_guest && !writeback)) {
		useraccess_clean_range((uint8_t *)va,
				       util_min(remaining, mapped_size));
	}

	size_t copied_size;
	if (from_guest) {
		copied_size = memscpy(hyp_buf, remaining, va, mapped_size);
	} else {
		copied_size = memscpy(va, mapped_size, hyp_buf, remaining);
	}

	if (compiler_unexpected(!from_guest && !writeback)) {
		useraccess_clean_invalidate_range((uint8_t *)va, copied_size);
	}

	partition_phys_access_disable(va);

	partition_phys_unmap(va, guest_pa, mapped_size);

	return copied_size;
}

static size_result_t
useraccess_copy_from_to_guest_va(gvaddr_t gvaddr, void *hvaddr, size_t size,
				 bool from_guest, bool force_access)
{
	error_t	 ret	   = OK;
	size_t	 remaining = size;
	gvaddr_t guest_va  = gvaddr;
	void	*hyp_buf   = hvaddr;

	assert(hyp_buf != NULL);
	assert(remaining != 0U);

	if (util_add_overflows((uintptr_t)hvaddr, size - 1U) ||
	    util_add_overflows(gvaddr, size - 1U)) {
		ret = ERROR_ADDR_OVERFLOW;
		goto out;
	}

	PAR_EL1_base_t saved_par =
		register_PAR_EL1_base_read_volatile_ordered(&asm_ordering);

	const size_t page_size	 = 4096U;
	size_t	     page_offset = gvaddr & (page_size - 1U);

	do {
		// Guest stage 2 lookups are in RCU read-side critical sections
		// so that unmap or access change operations can wait for them
		// to complete.
		rcu_read_start();

		if (from_guest || force_access) {
			__asm__ volatile("at S12E1R, %[guest_va];"
					 "isb                   ;"
					 : "+m"(asm_ordering)
					 : [guest_va] "r"(guest_va));
		} else {
			__asm__ volatile("at S12E1W, %[guest_va];"
					 "isb                   ;"
					 : "+m"(asm_ordering)
					 : [guest_va] "r"(guest_va));
		}

		PAR_EL1_t par = {
			.base = register_PAR_EL1_base_read_volatile_ordered(
				&asm_ordering),
		};

		if (compiler_expected(!PAR_EL1_base_get_F(&par.base))) {
			// No fault; copy to/from the translated PA
			size_t copied_size =
				useraccess_copy_from_to_translated_pa(
					par, guest_va, page_size, page_offset,
					from_guest, hyp_buf, remaining);
			assert(copied_size > 0U);
			guest_va += copied_size;
			hyp_buf = (void *)((uintptr_t)hyp_buf + copied_size);
			remaining -= copied_size;
			page_offset = 0U;
		} else if (!PAR_EL1_F1_get_S(&par.f1)) {
			// Stage 1 fault (reason is not distinguished here)
			ret = ERROR_ARGUMENT_INVALID;
		} else {
			// Stage 2 fault; return DENIED for permission faults,
			// ADDR_INVALID otherwise
			iss_da_ia_fsc_t fst = PAR_EL1_F1_get_FST(&par.f1);
			ret = ((fst == ISS_DA_IA_FSC_PERMISSION_1) ||
			       (fst == ISS_DA_IA_FSC_PERMISSION_2) ||
			       (fst == ISS_DA_IA_FSC_PERMISSION_3))
				      ? ERROR_DENIED
				      : ERROR_ADDR_INVALID;
		}

		rcu_read_finish();
	} while ((remaining != 0U) && (ret == OK));

	register_PAR_EL1_base_write_ordered(saved_par, &asm_ordering);

out:
	return (size_result_t){ .e = ret, .r = size - remaining };
}

size_result_t
useraccess_copy_from_guest_va(void *hyp_va, size_t hsize, gvaddr_t guest_va,
			      size_t gsize)
{
	size_result_t ret;
	bool force_access = false; // only write to guest_va requires this flag.
	if ((gsize == 0U) || (hsize < gsize)) {
		ret = size_result_error(ERROR_ARGUMENT_SIZE);
	} else {
		ret = useraccess_copy_from_to_guest_va(guest_va, hyp_va, gsize,
						       true, force_access);
	}
	return ret;
}

size_result_t
useraccess_copy_to_guest_va(gvaddr_t guest_va, size_t gsize, const void *hyp_va,
			    size_t hsize, bool force_access)
{
	size_result_t ret;
	if ((hsize == 0U) || (gsize < hsize)) {
		ret = size_result_error(ERROR_ARGUMENT_SIZE);
	} else {
		ret = useraccess_copy_from_to_guest_va(
			guest_va, (void *)(uintptr_t)hyp_va, hsize, false,
			force_access);
	}
	return ret;
}

static size_result_t
useraccess_copy_from_to_guest_ipa(addrspace_t *addrspace, vmaddr_t ipa,
				  void *hvaddr, size_t size, bool from_guest,
				  bool force_access, bool force_coherent)
{
	error_t ret    = OK;
	size_t	offset = 0U;

	if (util_add_overflows((uintptr_t)hvaddr, size - 1U) ||
	    util_add_overflows(ipa, size - 1U)) {
		ret = ERROR_ADDR_OVERFLOW;
		goto out;
	}

	while (offset < size) {
		paddr_t		     mapped_base;
		size_t		     mapped_size;
		pgtable_vm_memtype_t mapped_memtype;
		pgtable_access_t     mapped_vm_kernel_access;
		pgtable_access_t     mapped_vm_user_access;

		// Guest stage 2 lookups are in RCU read-side critical sections
		// so that unmap or access change operations can wait for them
		// to complete.
		rcu_read_start();

		if (!pgtable_vm_lookup(
			    &addrspace->vm_pgtable, ipa + offset, &mapped_base,
			    &mapped_size, &mapped_memtype,
			    &mapped_vm_kernel_access, &mapped_vm_user_access)) {
			rcu_read_finish();
			ret = ERROR_ADDR_INVALID;
			break;
		}

		if (!force_access &&
		    !pgtable_access_check(mapped_vm_kernel_access,
					  (from_guest ? PGTABLE_ACCESS_R
						      : PGTABLE_ACCESS_W))) {
			rcu_read_finish();
			ret = ERROR_DENIED;
			break;
		}

		size_t mapping_offset = (ipa + offset) & (mapped_size - 1U);
		mapped_base += mapping_offset;
		mapped_size -= mapping_offset;

		uint8_t *vm_addr = partition_phys_map(mapped_base, mapped_size);
		partition_phys_access_enable(vm_addr);

		uint8_t *hyp_va	  = (uint8_t *)hvaddr + offset;
		size_t	 hyp_size = size - offset;
		size_t	 copied_size;

		if (from_guest) {
			if (force_coherent ||
			    (mapped_memtype != PGTABLE_VM_MEMTYPE_NORMAL_WB)) {
				useraccess_clean_invalidate_range(
					vm_addr,
					util_min(mapped_size, hyp_size));
			}

			copied_size =
				memscpy(hyp_va, hyp_size, vm_addr, mapped_size);
		} else {
			copied_size =
				memscpy(vm_addr, mapped_size, hyp_va, hyp_size);

			if (force_coherent ||
			    (mapped_memtype != PGTABLE_VM_MEMTYPE_NORMAL_WB)) {
				useraccess_clean_range(vm_addr, copied_size);
			}
		}

		partition_phys_access_disable(vm_addr);
		partition_phys_unmap(vm_addr, mapped_base, mapped_size);

		rcu_read_finish();

		offset += copied_size;
	}

out:
	return (size_result_t){ .e = ret, .r = offset };
}

size_result_t
useraccess_copy_from_guest_ipa(addrspace_t *addrspace, void *hyp_va,
			       size_t hsize, vmaddr_t guest_ipa, size_t gsize,
			       bool force_access, bool force_coherent)
{
	size_result_t ret;
	if ((gsize == 0U) || (hsize < gsize)) {
		ret = size_result_error(ERROR_ARGUMENT_SIZE);
	} else {
		ret = useraccess_copy_from_to_guest_ipa(addrspace, guest_ipa,
							hyp_va, gsize, true,
							force_access,
							force_coherent);
	}
	return ret;
}

size_result_t
useraccess_copy_to_guest_ipa(addrspace_t *addrspace, vmaddr_t guest_ipa,
			     size_t gsize, const void *hyp_va, size_t hsize,
			     bool force_access, bool force_coherent)
{
	size_result_t ret;
	if ((hsize == 0U) || (gsize < hsize)) {
		ret = size_result_error(ERROR_ARGUMENT_SIZE);
	} else {
		ret = useraccess_copy_from_to_guest_ipa(
			addrspace, guest_ipa, (void *)(uintptr_t)hyp_va, hsize,
			false, force_access, force_coherent);
	}
	return ret;
}
