// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Thread functions used only by the boot code.

// Switch to the idle thread at the end of the boot sequence.
noreturn void
thread_boot_set_idle(void) REQUIRE_PREEMPT_DISABLED;
