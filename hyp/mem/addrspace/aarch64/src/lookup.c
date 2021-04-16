// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier; BSD-3-Clause

#if !defined(UNIT_TESTS)
#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <addrspace.h>
#include <thread.h>

#include <asm/barrier.h>

paddr_result_t
addrspace_va_to_pa_read(gvaddr_t addr)
{
	bool	success;
	paddr_t pa = 0U;

	thread_t *thread = thread_get_self();
	assert(thread->kind == THREAD_KIND_VCPU);

	PAR_EL1_RAW_t saved_par =
		register_PAR_EL1_RAW_read_ordered(&asm_ordering);

	__asm__ volatile("at	S12E1R, %[addr]		;"
			 "isb				;"
			 : "+m"(asm_ordering)
			 : [addr] "r"(addr));

	PAR_EL1_RAW_t par_raw =
		register_PAR_EL1_RAW_read_ordered(&asm_ordering);

	PAR_EL1_F0_t par = PAR_EL1_F0_cast(PAR_EL1_RAW_raw(par_raw));
	success		 = !PAR_EL1_F0_get_F(&par);

	if (success) {
		pa = PAR_EL1_F0_get_PA(&par);
		pa |= (gvaddr_t)addr & 0xfffU;
	}

	register_PAR_EL1_RAW_write_ordered(saved_par, &asm_ordering);

	return success ? paddr_result_ok(pa)
		       : paddr_result_error(ERROR_ADDR_INVALID);
}

vmaddr_result_t
addrspace_va_to_ipa_read(gvaddr_t addr)
{
	bool	 success;
	vmaddr_t ipa = 0U;

	thread_t *thread = thread_get_self();
	assert(thread->kind == THREAD_KIND_VCPU);

	PAR_EL1_RAW_t saved_par =
		register_PAR_EL1_RAW_read_ordered(&asm_ordering);

	__asm__ volatile("at	S1E1R, %[addr]		;"
			 "isb				;"
			 : "+m"(asm_ordering)
			 : [addr] "r"(addr));

	PAR_EL1_RAW_t par_raw =
		register_PAR_EL1_RAW_read_ordered(&asm_ordering);

	PAR_EL1_F0_t par = PAR_EL1_F0_cast(PAR_EL1_RAW_raw(par_raw));
	success		 = !PAR_EL1_F0_get_F(&par);

	if (success) {
		ipa = PAR_EL1_F0_get_PA(&par);
		ipa |= (vmaddr_t)addr & 0xfffU;
	}

	register_PAR_EL1_RAW_write_ordered(saved_par, &asm_ordering);

	return success ? vmaddr_result_ok(ipa)
		       : vmaddr_result_error(ERROR_ADDR_INVALID);
}
#endif
