// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Miscellaneous definitions describing the CPU implementation.

// The size in address bits of a line in the innermost visible data cache.
#define CPU_L1D_LINE_BITS 6U

// The size in address bits of the CPU's DC ZVA block. This is nearly always
// the same as CPU_L1D_LINE_BITS.
#define CPU_DCZVA_BITS 6U

// The largest difference between the source and destination pointers during
// the optimised memcpy() for this CPU. This is here because it might depend
// on CPU_L1D_LINE_BITS in some implementations.
#define CPU_MEMCPY_STRIDE 256U
