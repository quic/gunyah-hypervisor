// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hypconstants.h>
#include <asm/asm_defs.inc>

#include "vectors_el2.inc"
#include "vectors_vcpu.inc"

function sdei_client_handle_event_entry
	disable_phys_access

	// As long as the hypervisor does not ever access addresses below SP,
	// we should be able to use SP as normal.
	//
	// Reserve an extra 16 bytes for the IA key (offset 16).
	stp	x18, lr, [sp, -32]!

#if defined(ARCH_ARM_FEAT_PAuth)
	vcpu_pauth_entry_thread_sp 16, x9, x10, x11, x12, x13
#endif

	bl	sdei_client_handle_event

#if defined(ARCH_ARM_FEAT_PAuth)
	vcpu_pauth_exit_key_sp IA, 16, x10, x11
#endif

	ldp	x18, lr, [sp], 32

	ldr	x0, =0xC4000025 // SDEI_EVENT_COMPLETE
	mov	x1, 0x0 // EV_HANDLED
	smc	0
1:
	wfi
	b	1b
function_end sdei_client_handle_event_entry
