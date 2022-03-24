// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// The functions in this file deal only with the C execution context; i.e.
// everything that is directly visible to the compiler.
//
// This typically includes the stack pointer, frame pointer, general-purpose
// registers, condition flags, and program counter. It may also include floating
// point and/or vector registers, where required by the architecture.
//
// This does not include control registers for guest VMs, the MMU, etc., which
// are switched by other modules' handlers for the context switch events.

// The default size and minimum alignment for a thread's kernel stack.
extern const size_t thread_stack_min_align;
extern const size_t thread_stack_alloc_align;
extern const size_t thread_stack_size_default;

// Map the kernel stack of a new thread.
error_t
thread_arch_map_stack(thread_t *thread);

// Unmap the kernel stack of a thread.
void
thread_arch_unmap_stack(thread_t *thread);

// Set up the execution context for a new thread.
void
thread_arch_init_context(thread_t *thread);

// Switch from the execution context of the current thread to some other thread.
//
// This function does not return until it is called again by some other thread,
// specifying this thread as an argument. At that point, it returns a pointer to
// the thread it switched from, which need not be the same as the thread that
// the original call switched to.
thread_t *
thread_arch_switch_thread(thread_t *next_thread);

// Set the current thread, assuming there was no previous current thread.
//
// This function is called at the end of the CPU power-on sequence.
noreturn void
thread_arch_set_thread(thread_t *next_thread);
