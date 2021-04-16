// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

void
vectors_exception_dispatch(kernel_trap_frame_t *frame);

SPSR_EL2_A64_t
vectors_interrupt_dispatch(void);
