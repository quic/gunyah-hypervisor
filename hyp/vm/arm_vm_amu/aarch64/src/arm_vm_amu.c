// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include "event_handlers.h"

#if (ARCH_ARM_VER < 84) && defined(ARCH_ARM_8_4_AMU)
// AMU should not be enabled on ARM cores earlier than v8.4
#error AMU configuration error
#endif

#if defined(ARCH_ARM_8_4_AMU)
error_t
arm_vm_amu_handle_object_activate_thread(thread_t *thread)
{
	assert(thread != NULL);

	if (thread->kind == THREAD_KIND_VCPU) {
		// Give HLOS full access to AMU registers. For the rest of the
		// VMs, trap accesses to AMU registers without handling them, so
		// an abort gets injected to any non-HLOS guest that tries to
		// use AMU.
		if (vcpu_option_flags_get_hlos_vm(&thread->vcpu_options)) {
			CPTR_EL2_E2H1_set_TAM(&thread->vcpu_regs_el2.cptr_el2,
					      false);
		} else {
			CPTR_EL2_E2H1_set_TAM(&thread->vcpu_regs_el2.cptr_el2,
					      true);
		}
	}

	return OK;
}
#endif
