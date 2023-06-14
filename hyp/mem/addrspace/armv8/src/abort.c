// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(INTERFACE_VCPU)
#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <addrspace.h>
#include <atomic.h>
#include <platform_mem.h>
#include <rcu.h>
#include <spinlock.h>
#include <thread.h>

#include <asm/barrier.h>

#include "event_handlers.h"

static bool
addrspace_undergoing_bbm(addrspace_t *addrspace)
{
	bool ret;

	if (addrspace->platform_pgtable) {
		ret = platform_pgtable_undergoing_bbm();
	} else {
#if (CPU_PGTABLE_BBM_LEVEL == 0) && !defined(PLATFORM_PGTABLE_AVOID_BBM)
		// We use break-before-make for block splits and merges,
		// which might affect addresses outside the operation range
		// and therefore might cause faults that should be hidden.
		if (!spinlock_trylock(&addrspace->pgtable_lock)) {
			ret = true;
		} else {
			spinlock_release(&addrspace->pgtable_lock);
			ret = false;
		}
#else
		// Break-before-make is only used when changing the output
		// address or cache attributes, which shouldn't happen while
		// the affected pages are being accessed.
		ret = false;
#endif
	}

	return ret;
}

static vcpu_trap_result_t
addrspace_handle_guest_tlb_conflict(vmaddr_result_t ipa, FAR_EL2_t far,
				    bool s1ptw)
{
	// If this fault was not on a stage 1 PT walk, the ipa argument is not
	// valid, because the architecture allows the TLB to avoid caching it.
	// We can do a lookup on the VA to try to find it. This may fail if the
	// CPU caches S1-only translations and the conflict is in that cache.
	//
	// For a fault on a stage 1 PT walk, the ipa argument is always valid.
	if (!s1ptw) {
		ipa = addrspace_va_to_ipa_read(
			FAR_EL2_get_VirtualAddress(&far));
	} else {
		assert(ipa.e == OK);
	}

	asm_ordering_dummy_t tlbi_s2_ordering;
	if (ipa.e == OK) {
		// If the IPA is valid, the conflict may have been between S2
		// TLB entries, so flush the IPA from the S2 TLB. Note that if
		// our IPA lookup above failed, the conflict must be in S1+S2 or
		// S1-only entries, so no S2 flush is needed.
		vmsa_tlbi_ipa_input_t ipa_input = vmsa_tlbi_ipa_input_default();
		vmsa_tlbi_ipa_input_set_IPA(&ipa_input, ipa.r);
		__asm__ volatile(
			"tlbi IPAS2E1, %[VA]"
			: "=m"(tlbi_s2_ordering)
			: [VA] "r"(vmsa_tlbi_ipa_input_raw(ipa_input)));
	}

	// Regardless of whether the IPA is valid, there is always a possibility
	// that the conflict was on S1+S2 or S1-only entries. So we always flush
	// by VA. If the fault was on a stage 1 page table walk, the fault may
	// have been on a cached next-level entry, so we flush those too.
	asm_ordering_dummy_t  tlbi_s1_ordering;
	vmsa_tlbi_vaa_input_t va_input = vmsa_tlbi_vaa_input_default();
	vmsa_tlbi_vaa_input_set_VA(&va_input, FAR_EL2_get_VirtualAddress(&far));
	if (s1ptw) {
		__asm__ volatile("tlbi VAAE1, %[VA]"
				 : "=m"(tlbi_s1_ordering)
				 : [VA] "r"(vmsa_tlbi_vaa_input_raw(va_input)));
	} else {
		__asm__ volatile("tlbi VAALE1, %[VA]"
				 : "=m"(tlbi_s1_ordering)
				 : [VA] "r"(vmsa_tlbi_vaa_input_raw(va_input)));
	}

	__asm__ volatile("dsb nsh" ::"m"(tlbi_s1_ordering),
			 "m"(tlbi_s2_ordering));

	return VCPU_TRAP_RESULT_RETRY;
}

// Retry faults if they may have been caused by break before make during block
// splits in the direct physical access region
static vcpu_trap_result_t
addrspace_handle_guest_translation_fault(FAR_EL2_t far)
{
	vcpu_trap_result_t ret;

	uintptr_t addr = FAR_EL2_get_VirtualAddress(&far);

	thread_t *current = thread_get_self();
	assert(current != NULL);

	addrspace_t *addrspace = current->addrspace;
	assert(addrspace != NULL);

	rcu_read_start();
	if (!addrspace_undergoing_bbm(addrspace)) {
		// There is no BBM in progress, but there might have been when
		// the fault occurred. Perform a lookup to see whether the
		// accessed address is now mapped in S2.
		//
		// If the accessed address no longer faults in stage 2, we can
		// just retry the faulting access. Otherwise we can consider the
		// fault to be fatal, because there is no BBM operation still in
		// progress.
		ret = (addrspace_va_to_pa_read(addr).e != ERROR_DENIED)
			      ? VCPU_TRAP_RESULT_RETRY
			      : VCPU_TRAP_RESULT_UNHANDLED;
	} else {
		// A map operation is in progress, so retry until it finishes.
		// Note that we might get stuck here if the page table is
		// corrupt!
		ret = VCPU_TRAP_RESULT_RETRY;
	}
	rcu_read_finish();

	return ret;
}

vcpu_trap_result_t
addrspace_handle_vcpu_trap_data_abort_guest(ESR_EL2_t esr, vmaddr_result_t ipa,
					    FAR_EL2_t far)
{
	vcpu_trap_result_t ret = VCPU_TRAP_RESULT_UNHANDLED;

	ESR_EL2_ISS_DATA_ABORT_t iss =
		ESR_EL2_ISS_DATA_ABORT_cast(ESR_EL2_get_ISS(&esr));
	iss_da_ia_fsc_t fsc = ESR_EL2_ISS_DATA_ABORT_get_DFSC(&iss);

	if (fsc == ISS_DA_IA_FSC_TLB_CONFLICT) {
		ret = addrspace_handle_guest_tlb_conflict(
			ipa, far, ESR_EL2_ISS_DATA_ABORT_get_S1PTW(&iss));
	}

	// Only translation faults can be caused by BBM
	if ((fsc == ISS_DA_IA_FSC_TRANSLATION_1) ||
	    (fsc == ISS_DA_IA_FSC_TRANSLATION_2) ||
	    (fsc == ISS_DA_IA_FSC_TRANSLATION_3)) {
		ret = addrspace_handle_guest_translation_fault(far);
	}

	return ret;
}

vcpu_trap_result_t
addrspace_handle_vcpu_trap_pf_abort_guest(ESR_EL2_t esr, vmaddr_result_t ipa,
					  FAR_EL2_t far)
{
	vcpu_trap_result_t ret = VCPU_TRAP_RESULT_UNHANDLED;

	ESR_EL2_ISS_INST_ABORT_t iss =
		ESR_EL2_ISS_INST_ABORT_cast(ESR_EL2_get_ISS(&esr));
	iss_da_ia_fsc_t fsc = ESR_EL2_ISS_INST_ABORT_get_IFSC(&iss);

	if (fsc == ISS_DA_IA_FSC_TLB_CONFLICT) {
		ret = addrspace_handle_guest_tlb_conflict(
			ipa, far, ESR_EL2_ISS_INST_ABORT_get_S1PTW(&iss));
	}

	// Only translation faults can be caused by BBM
	if ((fsc == ISS_DA_IA_FSC_TRANSLATION_1) ||
	    (fsc == ISS_DA_IA_FSC_TRANSLATION_2) ||
	    (fsc == ISS_DA_IA_FSC_TRANSLATION_3)) {
		ret = addrspace_handle_guest_translation_fault(far);
	}

	return ret;
}
#else
extern char unused;
#endif
