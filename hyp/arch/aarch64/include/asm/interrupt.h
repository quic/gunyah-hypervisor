// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Macros to enable or disable interrupts.

// Enable all aborts and interrupts, with a full compiler barrier. Self-hosted
// debug is left disabled.
#define asm_interrupt_enable() __asm__ volatile("msr daifclr, 0x7" ::: "memory")

// Enable all aborts and interrupts, with a compiler release fence. Self-hosted
// debug is left disabled.
//
// The argument should be a pointer to a flag that has previously been read to
// decide whether to enable interrupts. The pointer will not be dereferenced by
// this macro.
#define asm_interrupt_enable_release(flag_ptr)                                 \
	do {                                                                   \
		atomic_signal_fence(memory_order_release);                     \
		__asm__ volatile("msr daifclr, 0x7" : "+m"(*(flag_ptr)));      \
	} while ((_Bool)0)

// Disable all aborts and interrupts, with a full compiler barrier.
#define asm_interrupt_disable()                                                \
	__asm__ volatile("msr daifset, 0x7" ::: "memory")

// Disable all aborts and interrupts, with a compiler acquire fence.
//
// The argument should be a pointer to a flag that will be written (by the
// caller) after this macro completes to record the fact that we have
// disabled interrupts. The pointer will not be dereferenced by this macro.
//
// Warning: the flag must not be CPU-local if this configuration allows context
// switches in interrupt handlers! If it is, use asm_interrupt_disable() (with
// a full barrier) and ensure that the CPU ID is determined _after_ disabling
// interrupts. A thread-local flag is ok, however.
#define asm_interrupt_disable_acquire(flag_ptr)                                \
	do {                                                                   \
		__asm__ volatile("msr daifset, 0x7" ::"m"(*(flag_ptr)));       \
		atomic_signal_fence(memory_order_acquire);                     \
	} while ((_Bool)0)
