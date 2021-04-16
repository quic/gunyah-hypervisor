// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#if !defined(UNIT_TESTS)
#include <hypregisters.h>

#include <partition.h>
#include <rcu.h>
#include <util.h>

#include <asm/barrier.h>

#include "useraccess.h"

static error_t
useraccess_copy_from_to_guest(gvaddr_t gvaddr, void *hvaddr, size_t size,
			      bool from_guest)
{
	error_t	 ret	   = OK;
	size_t	 remaining = size;
	gvaddr_t guest_va  = gvaddr;
	void *	 hyp_buf   = hvaddr;

	assert(hyp_buf != NULL);
	assert(remaining != 0U);

	if (util_add_overflows((uintptr_t)hvaddr, size - 1U) ||
	    util_add_overflows(gvaddr, size - 1U)) {
		ret = ERROR_ADDR_OVERFLOW;
		goto out;
	}

	PAR_EL1_RAW_t saved_par =
		register_PAR_EL1_RAW_read_volatile_ordered(&asm_ordering);

	const size_t page_size	 = 4096U;
	size_t	     page_offset = gvaddr & (page_size - 1U);

	do {
		// Guest stage 2 lookups are in RCU read-side critical sections
		// so that unmap or access change operations can wait for them
		// to complete.
		rcu_read_start();

		if (from_guest) {
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

		PAR_EL1_RAW_t par = register_PAR_EL1_RAW_read_volatile_ordered(
			&asm_ordering);

		PAR_EL1_F0_t par_f0  = PAR_EL1_F0_cast(PAR_EL1_RAW_raw(par));
		bool	     success = !PAR_EL1_F0_get_F(&par_f0);

		if (success) {
			paddr_t guest_pa = PAR_EL1_F0_get_PA(&par_f0);
			guest_pa |= (paddr_t)guest_va & (page_size - 1U);

			size_t mapped_size = page_size - page_offset;
			void * va = partition_phys_map(guest_pa, mapped_size);

			partition_phys_access_enable(va);
			size_t copied_size;
			if (from_guest) {
				copied_size = memscpy(hyp_buf, remaining, va,
						      mapped_size);
			} else {
				copied_size = memscpy(va, mapped_size, hyp_buf,
						      remaining);
			}
			partition_phys_access_disable(va);

			partition_phys_unmap(va, guest_pa, mapped_size);

			assert(copied_size > 0U);
			guest_va += copied_size;
			hyp_buf = (void *)((uintptr_t)hyp_buf + copied_size);
			remaining -= copied_size;
			page_offset = 0U;
		} else {
			ret = ERROR_ADDR_INVALID;
		}

		rcu_read_finish();
	} while ((remaining != 0U) && (ret == OK));

	register_PAR_EL1_RAW_write_ordered(saved_par, &asm_ordering);

out:
	return ret;
}

error_t
useraccess_copy_from_guest(void *hyp_va, size_t hsize, gvaddr_t guest_va,
			   size_t gsize)
{
	error_t err;
	if ((gsize == 0U) || (hsize < gsize)) {
		err = ERROR_ARGUMENT_SIZE;
	} else {
		err = useraccess_copy_from_to_guest(guest_va, hyp_va, gsize,
						    true);
	}
	return err;
}

error_t
useraccess_copy_to_guest(gvaddr_t guest_va, size_t gsize, void *hyp_va,
			 size_t hsize)
{
	error_t err;
	if ((hsize == 0U) || (gsize < hsize)) {
		err = ERROR_ARGUMENT_SIZE;
	} else {
		err = useraccess_copy_from_to_guest(guest_va, hyp_va, hsize,
						    false);
	}
	return err;
}
#endif
