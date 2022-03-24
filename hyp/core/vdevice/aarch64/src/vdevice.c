// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <addrspace.h>
#include <atomic.h>
#include <compiler.h>
#include <memdb.h>
#include <rcu.h>
#include <thread.h>
#include <util.h>
#include <vcpu.h>

#include <events/vdevice.h>

#include "event_handlers.h"

vcpu_trap_result_t
vdevice_handle_vcpu_trap_data_abort_guest(ESR_EL2_t esr, vmaddr_t ipa,
					  FAR_EL2_t far)
{
	vcpu_trap_result_t ret	  = VCPU_TRAP_RESULT_UNHANDLED;
	register_t	   val	  = 0U;
	thread_t		 *thread = thread_get_self();

	ESR_EL2_ISS_DATA_ABORT_t iss =
		ESR_EL2_ISS_DATA_ABORT_cast(ESR_EL2_get_ISS(&esr));

	if (ESR_EL2_ISS_DATA_ABORT_get_ISV(&iss)) {
		bool   is_write = ESR_EL2_ISS_DATA_ABORT_get_WnR(&iss);
		bool   is_acquire_release = ESR_EL2_ISS_DATA_ABORT_get_AR(&iss);
		size_t size = 1 << ESR_EL2_ISS_DATA_ABORT_get_SAS(&iss);
		uint8_t		reg_num = ESR_EL2_ISS_DATA_ABORT_get_SRT(&iss);
		iss_da_ia_fsc_t fsc	= ESR_EL2_ISS_DATA_ABORT_get_DFSC(&iss);

		// Only translation and permission faults are considered for
		// vdevice access.

		vdevice_typed_ptr_t vdevice = { .type = VDEVICE_TYPE_NONE };
		paddr_t		    pa	    = 0U;

		if ((fsc == ISS_DA_IA_FSC_PERMISSION_1) ||
		    (fsc == ISS_DA_IA_FSC_PERMISSION_2) ||
		    (fsc == ISS_DA_IA_FSC_PERMISSION_3)) {
			rcu_read_start();

			gvaddr_t       va = FAR_EL2_get_VirtualAddress(&far);
			paddr_result_t paddr_res = addrspace_va_to_pa_read(va);
			if (paddr_res.e != OK) {
				rcu_read_finish();
				goto out;
			}

			memdb_obj_type_result_t res = memdb_lookup(paddr_res.r);
			if ((res.e != OK) ||
			    (res.r.type != MEMDB_TYPE_EXTENT)) {
				rcu_read_finish();
				goto out;
			}

			memextent_t *me = (memextent_t *)res.r.object;
			assert(me != NULL);
			vdevice = atomic_load_consume(&me->vdevice);
			if (vdevice.type == VDEVICE_TYPE_NONE) {
				rcu_read_finish();
				goto out;
			}

			pa = paddr_res.r;

		} else if ((fsc != ISS_DA_IA_FSC_TRANSLATION_1) &&
			   (fsc != ISS_DA_IA_FSC_TRANSLATION_2) &&
			   (fsc != ISS_DA_IA_FSC_TRANSLATION_3)) {
			goto out;
		} else {
			// Nothing to do
		}

		// TODO: for now the vdevice handlers with hard-coded address
		// will not be part of the vdevice types. If the IPA is not
		// mapped in the VM, we will try calling those handlers directly
		// since IPA=PA

		if (is_write) {
			bool handled = false;

			val = vcpu_gpr_read(thread, reg_num);

			if (is_acquire_release) {
				atomic_thread_fence(memory_order_release);
			}

			if (vdevice.type != VDEVICE_TYPE_NONE) {
				handled = trigger_vdevice_access_event(
					vdevice.type, vdevice.ptr, pa, ipa,
					size, &val, true);
				rcu_read_finish();
			} else {
				handled =
					trigger_vdevice_access_fixed_addr_event(
						ipa, size, &val, true);
			}
			if (handled) {
				ret = VCPU_TRAP_RESULT_EMULATED;
			}
		} else {
			bool handled = false;

			if (vdevice.type != VDEVICE_TYPE_NONE) {
				handled = trigger_vdevice_access_event(
					vdevice.type, vdevice.ptr, pa, ipa,
					size, &val, false);
				rcu_read_finish();
			} else {
				handled =
					trigger_vdevice_access_fixed_addr_event(
						ipa, size, &val, false);
			}
			if (handled) {
				// Do we need to sign-extend the result?
				if (ESR_EL2_ISS_DATA_ABORT_get_SSE(&iss) &&
				    (size != sizeof(uint64_t))) {
					uint64_t mask = 1ULL
							<< ((size * 8) - 1);
					val = (val ^ mask) - mask;
				}

				// Adjust the width if necessary
				if (!ESR_EL2_ISS_DATA_ABORT_get_SF(&iss)) {
					val = (uint32_t)val;
				}

				vcpu_gpr_write(thread, reg_num, val);

				if (is_acquire_release) {
					atomic_thread_fence(
						memory_order_acquire);
				}

				ret = VCPU_TRAP_RESULT_EMULATED;
			}
		}
	}

out:
	return ret;
}
