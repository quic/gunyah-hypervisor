// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Driver configuration constants for the GICv3 driver.

// Default priority for all interrupts.
#define GIC_PRIORITY_DEFAULT 0xA0U

#if GICV3_HAS_ITS

// Command queue length
#define GICV3_ITS_QUEUE_LEN 128U

#endif // GICV3_HAS_ITS
