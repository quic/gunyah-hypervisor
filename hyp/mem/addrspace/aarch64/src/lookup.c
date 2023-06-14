// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier; BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <limits.h>

#include <hypregisters.h>

#include <addrspace.h>
#include <thread.h>
#include <util.h>

#include <asm/barrier.h>

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
	assert(bits < (sizeof(vmaddr_t) * (size_t)CHAR_BIT));
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

#if defined(INTERFACE_VCPU)
paddr_result_t
addrspace_va_to_pa_read(gvaddr_t addr)
{
	paddr_result_t ret;

	thread_t *thread = thread_get_self();
	assert(thread->kind == THREAD_KIND_VCPU);

	PAR_EL1_base_t saved_par =
		register_PAR_EL1_base_read_ordered(&asm_ordering);
	__asm__ volatile("at	S12E1R, %[addr]"
			 : "+m"(asm_ordering)
			 : [addr] "r"(addr));
	asm_context_sync_ordered(&asm_ordering);
	PAR_EL1_t par = {
		.base = register_PAR_EL1_base_read_ordered(&asm_ordering),
	};
	register_PAR_EL1_base_write_ordered(saved_par, &asm_ordering);

	if (!PAR_EL1_base_get_F(&par.base)) {
		paddr_t pa = PAR_EL1_F0_get_PA(&par.f0);
		pa |= (gvaddr_t)addr & 0xfffU;
		ret = paddr_result_ok(pa);
	} else if (PAR_EL1_F1_get_S(&par.f1)) {
		// Stage 2 fault
		ret = paddr_result_error(ERROR_DENIED);
	} else {
		// Stage 1 fault
		ret = paddr_result_error(ERROR_ADDR_INVALID);
	}

	return ret;
}

vmaddr_result_t
addrspace_va_to_ipa_read(gvaddr_t addr)
{
	vmaddr_result_t ret;

	thread_t *thread = thread_get_self();
	assert(thread->kind == THREAD_KIND_VCPU);

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
#endif
