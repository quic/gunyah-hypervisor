// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <compiler.h>
#include <thread.h>
#include <util.h>
#include <vcpu.h>

#include <events/vdevice.h>

#include "event_handlers.h"

vcpu_trap_result_t
vdevice_handle_vcpu_trap_data_abort_guest(ESR_EL2_t esr, vmaddr_t ipa)
{
	vcpu_trap_result_t ret = VCPU_TRAP_RESULT_UNHANDLED;
	register_t	   val;
	thread_t *	   thread = thread_get_self();

	ESR_EL2_ISS_DATA_ABORT_t iss =
		ESR_EL2_ISS_DATA_ABORT_cast(ESR_EL2_get_ISS(&esr));

	if (ESR_EL2_ISS_DATA_ABORT_get_ISV(&iss)) {
		bool   is_write = ESR_EL2_ISS_DATA_ABORT_get_WnR(&iss);
		bool   is_acquire_release = ESR_EL2_ISS_DATA_ABORT_get_AR(&iss);
		size_t size = 1 << ESR_EL2_ISS_DATA_ABORT_get_SAS(&iss);
		uint8_t		reg_num = ESR_EL2_ISS_DATA_ABORT_get_SRT(&iss);
		iss_da_ia_fsc_t fsc	= ESR_EL2_ISS_DATA_ABORT_get_DFSC(&iss);

		// Only translation faults are considered for vdevice access.
		if ((fsc != ISS_DA_IA_FSC_TRANSLATION_1) &&
		    (fsc != ISS_DA_IA_FSC_TRANSLATION_2) &&
		    (fsc != ISS_DA_IA_FSC_TRANSLATION_3)) {
			goto out;
		}

		// TODO: For now just trigger the vdevice_access event. The
		// subscribers will check the IPA range and decide whether to
		// take any action.
		// In the future we will need to perform a vdevice look-up
		// using special memory extents, and if a matching vdevice is
		// found, trigger a selector event based on the vdevice ID.
		if (is_write) {
			val = vcpu_gpr_read(thread, reg_num);

			if (is_acquire_release) {
				atomic_thread_fence(memory_order_release);
			}
			if (trigger_vdevice_access_event(ipa, size, &val,
							 true)) {
				ret = VCPU_TRAP_RESULT_EMULATED;
			}
		} else {
			if (trigger_vdevice_access_event(ipa, size, &val,
							 false)) {
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
