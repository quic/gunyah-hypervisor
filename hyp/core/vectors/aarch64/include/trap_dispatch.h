// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

void
vectors_exception_dispatch(kernel_trap_frame_full_t *frame)
	REQUIRE_PREEMPT_DISABLED;

SPSR_EL2_A64_t
vectors_interrupt_dispatch(void) REQUIRE_PREEMPT_DISABLED;

void
vectors_dump_regs(kernel_trap_frame_full_t *frame);

#if defined(ARCH_ARM_FEAT_PAuth)
uintptr_t
vectors_remove_pointer_auth(uintptr_t addr);
#endif
