// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <asm/asm_defs.inc>

function sdei_client_handle_event_entry
	// As long as the hypervisor does not ever access addresses below SP,
	// we should be able to use SP as normal.
	stp	x18, lr, [sp, -16]!
	bl	sdei_client_handle_event
	ldp	x18, lr, [sp], 16

	ldr	x0, =0xC4000025 // SDEI_EVENT_COMPLETE
	mov	x1, 0x0 // EV_HANDLED
	smc	0
1:
	wfi
	b	1b
function_end sdei_client_handle_event_entry
