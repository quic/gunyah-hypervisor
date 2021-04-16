// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <asm/asm_defs.inc>

// This function sets the TLS base register. It is intended to be the last
// operation in the boot_runtime_*_init events.
function thread_switch_boot_thread
	mov	x1, xzr
	add	x1, x1, :tprel_hi12:current_thread
	add	x1, x1, :tprel_lo12_nc:current_thread
	sub	x1, x0, x1
	msr	TPIDR_EL2, x1
	ret
function_end thread_switch_boot_thread
