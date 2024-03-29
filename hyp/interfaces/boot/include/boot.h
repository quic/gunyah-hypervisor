// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

extern const char hypervisor_version[];
extern const char hypervisor_build_date[];

// Triggers the hypervisor hand-over event
void
boot_start_hypervisor_handover(void) REQUIRE_PREEMPT_DISABLED;
