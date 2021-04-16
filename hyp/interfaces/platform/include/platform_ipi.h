// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Platform routines for raising and handling hardware IPIs.
//
// There are two variants of this API: one for when the platform has enough
// separate IPI lines to allocate one per registered reason, and another for
// when it doesn't. They are similar, but the latter does not take reason
// arguments and does not have mask and unmask calls.
//
// The semantics of the individual calls are similar to the high-level API in
// ipi.h, but they are not expected to provide any mechanism for fast-path
// delivery without raising a hardware interrupt, nor for multiplexing when
// there are more possible IPI reasons than physical IPI lines.

#include <hypconstants.h>

#if PLATFORM_IPI_LINES > ENUM_IPI_REASON_MAX_VALUE
void
platform_ipi_others(ipi_reason_t ipi);

void
platform_ipi_one(ipi_reason_t ipi, cpu_index_t cpu);

void
platform_ipi_mask(ipi_reason_t ipi);

void
platform_ipi_unmask(ipi_reason_t ipi);

void
platform_ipi_clear(ipi_reason_t ipi);
#else
void
platform_ipi_others(void);

void
platform_ipi_one(cpu_index_t cpu);
#endif
