// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Driver configuration constants for the GICv3 driver.

// Default priority for all interrupts.
#define GIC_PRIORITY_DEFAULT 0xA0U

#if GICV3_HAS_ITS

// Command queue length
#define GICV3_ITS_QUEUE_LEN 128U

#if GICV3_HAS_VLPI

// Default vPE ID range. We don't support sharing these, so this limits the
// number of VCPUs that may be attached to at least one VGITS.
#if !defined(GICV3_ITS_VPES)
#define GICV3_ITS_VPES 64U
#endif

#endif // GICV3_HAS_ITS

#endif // GICV3_HAS_ITS
