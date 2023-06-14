// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

void
platform_pmu_hw_irq_deactivate(void) REQUIRE_PREEMPT_DISABLED;

bool
platform_pmu_is_hw_irq_pending(void);
