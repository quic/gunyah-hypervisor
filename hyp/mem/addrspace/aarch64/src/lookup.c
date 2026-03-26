// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier; BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypconstants.h>
#include <hypregisters.h>

#include <addrspace.h>
#include <pgtable.h>
#include <pgtable_armv8.h>
#include <platform_mem.h>
#include <spinlock.h>
#include <thread.h>
#include <util.h>

#include <asm/barrier.h>

#include "addrspace_lookup.h"

// Check whether an address range is within the address space.
error_t
addrspace_check_range(addrspace_t *addrspace, vmaddr_t base, size_t size)
{
	error_t err;

	if ((size != 0U) && util_add_overflows(base, size - 1U)) {
		err = ERROR_ADDR_OVERFLOW;
		goto out;
	}

	count_t bits = addrspace->vm_pgtable.control.address_bits;
	// Ensure that the value used for shift operation is within the limit 0
	// to sizeof(type) -1
	assert(bits < util_width(vmaddr_t));
	if (base >= util_bit(bits)) {
		err = ERROR_ADDR_INVALID;
	} else if ((size != 0U) && ((base + size - 1U) >= util_bit(bits))) {
		err = ERROR_ARGUMENT_SIZE;
	} else {
		err = OK;
	}

out:
	return err;
}

static bool
addrspace_undergoing_bbm(void) REQUIRE_RCU_READ
{
	thread_t *current = thread_get_self();
	assert(current != NULL);

	addrspace_t *addrspace = current->addrspace;
	assert(addrspace != NULL);

	bool ret;

	if (pgtable_vm_is_platform_managed(&addrspace->vm_pgtable)) {
		ret = platform_pgtable_undergoing_bbm();
	} else {
		ret = pgtable_vm_undergoing_bbm(&addrspace->vm_pgtable);
	}

	return ret;
}

static void
addrspace_handle_lookup_tlb_conflict(gvaddr_t addr, bool s1ptw)
{
	asm_ordering_dummy_t tlbi_s2_ordering;
	if (!s1ptw) {
		// This may be a conflict on stage 2 entries; obtain an IPA and
		// invalidate it from the S2 TLB if possible. Note that if the
		// IPA lookup fails, the conflict must be in S1+S2 or S1-only
		// entries, so no S2 invalidate is needed.
		vmaddr_result_t ipa = addrspace_va_to_ipa_read(addr);
		if (ipa.e == OK) {
			addrspace_clear_s2_tlb_conflict(ipa.r,
							&tlbi_s2_ordering);
		}
	}

	// Invalidate any S1+S2 or S1-only entries matching the VA. If the fault
	// was on a stage 1 page table walk, the fault may have involved a walk
	// cache entry, so we invalidate those too.
	asm_ordering_dummy_t tlbi_s1_ordering;
	addrspace_clear_s1_tlb_conflict(addr, s1ptw, &tlbi_s1_ordering);

	// Ensure the invalidates are complete before retrying
	__asm__ volatile("dsb nsh; isb" ::"m"(tlbi_s1_ordering),
			 "m"(tlbi_s2_ordering));
}

addrspace_va_lookup_result_t
addrspace_va_to_pa_noretry(gvaddr_t addr, bool write) REQUIRE_RCU_READ
{
	addrspace_va_lookup_result_t ret;

	thread_t *thread = thread_get_self();
	assert(thread->addrspace != NULL);

	PAR_EL1_base_t saved_par =
		register_PAR_EL1_base_read_ordered(&asm_ordering);
	if (write) {
		// Address translation, stage 1 & 2 for EL1 write access
		__asm__ volatile("at	S12E1W, %[addr]"
				 : "+m"(asm_ordering)
				 : [addr] "r"(addr));
	} else {
		// Address translation, stage 1 & 2 for EL1 read access
		__asm__ volatile("at	S12E1R, %[addr]"
				 : "+m"(asm_ordering)
				 : [addr] "r"(addr));
	}
	asm_context_sync_ordered(&asm_ordering);
	PAR_EL1_t par = {
		.base = register_PAR_EL1_base_read_ordered(&asm_ordering),
	};
	register_PAR_EL1_base_write_ordered(saved_par, &asm_ordering);

	if (!PAR_EL1_base_get_F(&par.base)) {
		// Calculate the size that is known to be mapped. Note that
		// PAR_EL1 does not report the page size and always includes
		// bits 52:12 of the output PA, regardless of the applicable
		// page and granule sizes, so this is always 4K at most.
		size_t	offset	    = (size_t)addr &
				      util_mask(PAR_EL1_F0_PA_PRESHIFT);
		paddr_t pa	    = PAR_EL1_F0_get_PA(&par.f0) + offset;
		size_t	mapped_size = util_bit(PAR_EL1_F0_PA_PRESHIFT) - offset;

		// Determine whether the mapping is coherent with the
		// hypervisor's linear map. The linear map is always mapped
		// writeback, so we accept any writeback attribute.
		MAIR_ATTR_t attr = PAR_EL1_F0_get_ATTR(&par.f0);
		bool writeback = ((index_t)attr | MAIR_ATTR_ALLOC_HINT_MASK) ==
				 (index_t)MAIR_ATTR_NORMAL_WB;
#if defined(ARCH_ARM_FEAT_MTE)
		writeback = writeback || (attr == MAIR_ATTR_TAGGED_NORMAL_WB);
#endif

		ret = addrspace_va_lookup_result_ok((addrspace_va_lookup_t){
			.phys	  = pa,
			.size	  = mapped_size,
			.coherent = writeback,
		});
	} else {
		iss_da_ia_fsc_t fst = PAR_EL1_F1_get_FST(&par.f1);
		if (fst == ISS_DA_IA_FSC_TLB_CONFLICT) {
			// TLB conflict; invalidate to clear it and then retry.
			bool s1ptw = PAR_EL1_F1_get_PTW(&par.f1);
			addrspace_handle_lookup_tlb_conflict(addr, s1ptw);
			ret = addrspace_va_lookup_result_error(ERROR_RETRY);
		} else if (!PAR_EL1_F1_get_S(&par.f1)) {
			// Stage 1 fault
			ret = addrspace_va_lookup_result_error(
				ERROR_ARGUMENT_INVALID);
		} else if ((fst == ISS_DA_IA_FSC_PERMISSION_L0) ||
			   (fst == ISS_DA_IA_FSC_PERMISSION_L1) ||
			   (fst == ISS_DA_IA_FSC_PERMISSION_L2) ||
			   (fst == ISS_DA_IA_FSC_PERMISSION_L3)) {
			// Stage 2 permission fault.
			ret = addrspace_va_lookup_result_error(ERROR_DENIED);
		} else if (addrspace_undergoing_bbm()) {
			// A map operation is in progress, so retry until it
			// finishes. Note that we might get stuck here if the
			// page table is corrupt!
			ret = addrspace_va_lookup_result_error(ERROR_RETRY);
		} else {
			// Any other stage 2 fault.
			ret = addrspace_va_lookup_result_error(
				ERROR_ADDR_INVALID);
		}
	}

	return ret;
}

addrspace_va_lookup_result_t
addrspace_va_to_pa(gvaddr_t addr, bool write)
{
	addrspace_va_lookup_result_t ret;
	do {
		ret = addrspace_va_to_pa_noretry(addr, write);
	} while (ret.e == ERROR_RETRY);
	return ret;
}

vmaddr_result_t
addrspace_va_to_ipa_read(gvaddr_t addr)
{
	vmaddr_result_t ret;

	thread_t *thread = thread_get_self();
	assert(thread->addrspace != NULL);

	PAR_EL1_base_t saved_par =
		register_PAR_EL1_base_read_ordered(&asm_ordering);
	__asm__ volatile("at	S1E1R, %[addr]"
			 : "+m"(asm_ordering)
			 : [addr] "r"(addr));
	asm_context_sync_ordered(&asm_ordering);
	PAR_EL1_t par = {
		.base = register_PAR_EL1_base_read_ordered(&asm_ordering),
	};
	register_PAR_EL1_base_write_ordered(saved_par, &asm_ordering);

	if (!PAR_EL1_base_get_F(&par.base)) {
		vmaddr_t ipa = PAR_EL1_F0_get_PA(&par.f0);
		ipa |= (vmaddr_t)addr & 0xfffU;
		ret = vmaddr_result_ok(ipa);
	} else if (PAR_EL1_F1_get_S(&par.f1)) {
		// Stage 2 fault (on a S1 page table walk access)
		ret = vmaddr_result_error(ERROR_DENIED);
	} else {
		// Stage 1 fault
		ret = vmaddr_result_error(ERROR_ADDR_INVALID);
	}

	return ret;
}

void
addrspace_clear_s1_tlb_conflict(gvaddr_t guest_va, bool s1ptw,
				asm_ordering_dummy_t *ordering)
{
	vmsa_tlbi_vaa_input_t va_input = vmsa_tlbi_vaa_input_default();
	vmsa_tlbi_vaa_input_set_VA(&va_input, guest_va);
	if (s1ptw) {
		// If the fault occurred during a S1 PT walk, the conflict may
		// have involved a walk cache entry, so we must invalidate all
		// matching entries.
		__asm__ volatile("tlbi VAAE1, %[VA]"
				 : "=m"(*ordering)
				 : [VA] "r"(vmsa_tlbi_vaa_input_raw(va_input)));
	} else {
		// If the conflict was not found during an S1 PT walk, it must
		// have been on a leaf entry. So, we can limit the invalidation
		// to leaf entries.
		__asm__ volatile("tlbi VAALE1, %[VA]"
				 : "=m"(*ordering)
				 : [VA] "r"(vmsa_tlbi_vaa_input_raw(va_input)));
	}
}

void
addrspace_clear_s2_tlb_conflict(vmaddr_t	      guest_ipa,
				asm_ordering_dummy_t *ordering)
{
	vmsa_tlbi_ipa_input_t ipa_input = vmsa_tlbi_ipa_input_default();
	vmsa_tlbi_ipa_input_set_IPA(&ipa_input, guest_ipa);
	// If the conflict was found during a S1 PT walk, or if a VA to IPA
	// lookup was successful, then the conflict may have involved a S2
	// entry, so we must invalidate all S2 entries matching the IPA.
	__asm__ volatile("tlbi IPAS2E1, %[VA]"
			 : "=m"(*ordering)
			 : [VA] "r"(vmsa_tlbi_ipa_input_raw(ipa_input)));
}
