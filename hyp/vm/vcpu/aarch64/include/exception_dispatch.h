// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

void
vcpu_interrupt_dispatch(void) REQUIRE_PREEMPT_DISABLED;

void
vcpu_exception_dispatch(bool is_aarch64) REQUIRE_PREEMPT_DISABLED;

void
vcpu_error_dispatch(void) REQUIRE_PREEMPT_DISABLED;
