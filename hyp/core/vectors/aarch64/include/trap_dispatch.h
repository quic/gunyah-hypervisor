// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

void
vectors_exception_dispatch(kernel_trap_frame_full_t *frame)
	REQUIRE_PREEMPT_DISABLED;

SPSR_EL2_A64_t
vectors_interrupt_dispatch(void) REQUIRE_PREEMPT_DISABLED;

void
vectors_dump_regs(kernel_trap_frame_full_t *frame);
