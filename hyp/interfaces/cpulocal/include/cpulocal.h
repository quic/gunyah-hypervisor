// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// CPU-local storage.
//
// This is a generic implementation for architectures which don't have a
// conveniently readable CPU index register, and must use a TLS variable
// that is updated on context-switch.

// Declarations and accessors for CPU-local storage.
//
// Note that variables declared this way must not be accessed if it is
// possible for the calling thread to be preempted and then migrate to a
// different CPU. To avoid this, this header provides macros that can be
// used to mark a critical section that accesses CPU-local variables.

#if SCHEDULER_CAN_MIGRATE
#define cpulocal_begin()       preempt_disable()
#define cpulocal_end()	       preempt_enable()
#define assert_cpulocal_safe() assert_preempt_disabled()
#else
#define cpulocal_begin()       ((void)0)
#define cpulocal_end()	       ((void)0)
#define assert_cpulocal_safe() ((void)0)
#endif

// Declarators for CPU-local data
#define CPULOCAL_DECLARE_EXTERN(type, name)                                    \
	extern type cpulocal_##name[PLATFORM_MAX_CORES]
#define CPULOCAL_DECLARE(type, name) type cpulocal_##name[PLATFORM_MAX_CORES]
#define CPULOCAL_DECLARE_STATIC(type, name)                                    \
	static type cpulocal_##name[PLATFORM_MAX_CORES]

// Accessors for CPU-local data
#define CPULOCAL(name) cpulocal_##name[cpulocal_get_index()]
#define CPULOCAL_BY_INDEX(name, index)                                         \
	cpulocal_##name[cpulocal_check_index(index)]

// Return true if a CPU index is valid.
bool
cpulocal_index_valid(cpu_index_t index);

// Validate and return a CPU index.
//
// In debug kernels, this will assert that the index is in range. The input is
// returned unchanged.
cpu_index_t
cpulocal_check_index(cpu_index_t index);

// Get the CPU index of the caller.
//
// All calls to this function should be inside a critical section, which may
// be either an explicit preemption disable, a spinlock, or a cpulocal
// critical section. All uses of its result should occur before the
// corresponding critical section ends.
cpu_index_t
cpulocal_get_index(void);

// Get the CPU index of a specified thread.
//
// This will return a valid CPU ID if the thread is at any stage of execution on
// that CPU between the start of a thread_context_switch_pre event switching to
// the thread and the end of a thread_context_switch_post event switching away
// from the thread. If the thread is not running, it returns cpu_index_invalid.
//
// If the caller is not the specified thread, it should hold the scheduling lock
// for the thread, and all uses of its result should occur before that lock is
// released. If the caller is the specified thread, the same restrictions apply
// as for cpulocal_get_index().
cpu_index_t
cpulocal_get_index_for_thread(const thread_t *thread);
