// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Device memory fences
//
// The atomic_thread_fence() builtin only generates a fence for CPU threads,
// which means the compiler is allowed to use a DMB ISH instruction. For device
// accesses this is not good enough; we need a DMB SY.
//
// Note that the instructions here are the same for AArch64 and ARMv8 AArch32.
#define atomic_device_fence(p)                                                 \
	do {                                                                   \
		switch (p) {                                                   \
		case memory_order_relaxed:                                     \
			atomic_thread_fence(memory_order_relaxed);             \
			break;                                                 \
		case memory_order_acquire:                                     \
		case memory_order_consume:                                     \
			__asm__ volatile("dmb ld" ::: "memory");               \
			break;                                                 \
		case memory_order_release:                                     \
		case memory_order_acq_rel:                                     \
		case memory_order_seq_cst:                                     \
		default:                                                       \
			__asm__ volatile("dmb sy" ::: "memory");               \
			break;                                                 \
		}                                                              \
	} while (0)
