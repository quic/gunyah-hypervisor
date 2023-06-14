// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Barrier and wait operations.
//
// These macros should not be used unless the event interface and other
// compiler barriers are unsuitable.

// Yield the CPU to another hardware thread (SMT) / or let a simulator give CPU
// time to somthing else.
#define asm_yield() __asm__ volatile("yield")

// Ensure that writes to CPU configuration registers and other similar events
// are visible to code executing on the CPU. For example, use this between
// enabling access to floating point registers and actually using those
// registers.
#define asm_context_sync_fence()	__asm__ volatile("isb" ::: "memory")
#define asm_context_sync_ordered(order) __asm__ volatile("isb" : "+m"(*order))

// The asm_ordering variable is used as an artificial dependency to order
// different individual asm statements with respect to each other in a way that
// is lighter weight than a full "memory" clobber.
extern asm_ordering_dummy_t asm_ordering;
