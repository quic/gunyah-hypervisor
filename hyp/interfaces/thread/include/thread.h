// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// A function pointer that is called to start a thread.
//
// This is the return type of the thread_get_entry_fn event.
//
// The type system does not support function pointers, but this type is never
// stored persistently, so we can declare it by hand.
typedef void (*thread_func_t)(uintptr_t param);

// Return the caller's thread structure.
thread_t *
thread_get_self(void);

// Switch immediately to the specified thread.
//
// The caller must be running with preemption disabled and must not be the
// specified thread. The caller must take an additional reference to the
// specified thread prior to the call, which will be released when the thread
// stops running. If the switch fails, the reference will be released
// immediately before returning.
//
// The second argument is the absolute time at which the scheduler made the
// decision to run this thread. It is not used directly by this module, but is
// passed to context switch event handlers for use in time accounting.
//
// This function will fail if the specified thread is already running on another
// CPU. The scheduler is responsible for guaranteeing this.
error_t
thread_switch_to(thread_t *thread, ticks_t curticks) REQUIRE_PREEMPT_DISABLED;

// Kill a thread. This marks it as exiting, sends an interrupt to any CPU that
// is currently running it, and switches to it on the current CPU if it is not
// currently running. The switch is performed regardless of the thread's
// scheduling state, including block state, CPU affinity, priority or
// timeslice length.
//
// The thread is not guaranteed to have exited when this function returns. If
// such a guarantee is required, call thread_join() on it afterwards.
//
// The caller must either be the specified thread, or hold a reference to the
// specified thread.
error_t
thread_kill(thread_t *thread);

// Return true if the specified thread has had thread_kill() called on it.
//
// This function has relaxed memory semantics. If the thread may be running on a
// remote CPU, or may have been killed by a remote CPU, it is the caller's
// responsibility to ensure that the memory access is ordered correctly.
//
// The caller must either be the specified thread, or hold a reference to the
// specified thread, or be in an RCU read-side critical section.
bool
thread_is_dying(const thread_t *thread);

// Return true if the specified thread has exited.
//
// This function has relaxed memory semantics. If the thread may be running on a
// remote CPU, or may have just exited on a remote CPU, it is the caller's
// responsibility to ensure that the memory access is ordered correctly.
//
// The caller must either hold a reference to the specified thread, or be in an
// RCU read-side critical section.
bool
thread_has_exited(const thread_t *thread);

// Block until a specified thread has exited.
//
// The caller must not be the specified thread, and must hold a reference to
// the specified thread. Also, the caller is responsible for preventing
// deadlocks in case the specified thread is in an uninterruptible wait.
void
thread_join(thread_t *thread);

// Block until a specified thread has exited or the caller is killed.
//
// The caller must not be the specified thread, and must hold a reference to
// the specified thread. The specified thread should have been killed; if it
// has not, this function may block indefinitely.
//
// Returns true if the specified thread has exited, or false if the caller was
// killed while waiting for it.
//
// Note that if this returns false, the caller has preemption disabled and may
// be running while blocked or on a CPU it does not have affinity to.
bool
thread_join_killable(thread_t *thread);

// Prepare to call a function that may power off the CPU.
//
// This function saves any parts of the register state that are needed to resume
// the calling thread, and then calls the specified function. If that function
// returns, its result will be passed through to the caller. If it does not
// return, then when the calling thread is restarted (typically by the warm boot
// path), this function will return the resumed_result argument to the caller.
//
// There is no opportunity to add the calling thread to a scheduler queue, so
// this should only be called by a thread that has strict affinity to the CPU
// that is potentially powering off. This will typically be an idle thread.
register_t
thread_freeze(register_t (*fn)(register_t), register_t param,
	      register_t resumed_result) REQUIRE_PREEMPT_DISABLED;

// Reset the current thread's stack.
//
// This can be called to discard the current stack before calling a function
// that does not return. If the specified function does return, it will panic.
//
// Care should be taken to call this only when the return stack does not need to
// be unwound to clean anything up: no locks held, no stack variables on global
// queues, etc.
noreturn void
thread_reset_stack(void (*fn)(register_t), register_t param);

// Exit the current thread.
//
// This should be called from any thread that reaches the top of its kernel
// stack if it is either in killed state or has no user context.
noreturn void
thread_exit(void);
