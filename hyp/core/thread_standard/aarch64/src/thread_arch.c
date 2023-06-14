// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <compiler.h>
#include <idle.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <pgtable.h>
#include <platform_timer.h>
#include <preempt.h>
#include <prng.h>
#include <scheduler.h>
#include <thread.h>
#include <trace.h>

#include <events/thread.h>

#include "event_handlers.h"
#include "thread_arch.h"

typedef register_t (*fptr_t)(register_t arg);
typedef void (*fptr_noreturn_t)(register_t arg);

const size_t thread_stack_min_align    = 16;
const size_t thread_stack_alloc_align  = PGTABLE_HYP_PAGE_SIZE;
const size_t thread_stack_size_default = PGTABLE_HYP_PAGE_SIZE;

static size_t
thread_get_tls_offset(void)
{
	size_t offset = 0;
	__asm__("add     %0, %0, :tprel_hi12:current_thread	;"
		"add     %0, %0, :tprel_lo12_nc:current_thread	;"
		: "+r"(offset));
	return offset;
}

static uintptr_t
thread_get_tls_base(thread_t *thread)
{
	return (uintptr_t)thread - thread_get_tls_offset();
}

static noreturn void
thread_arch_main(thread_t *prev, ticks_t schedtime) LOCK_IMPL
{
	thread_t *thread = thread_get_self();

	trigger_thread_start_event();

	trigger_thread_context_switch_post_event(prev, schedtime, (ticks_t)0UL);
	object_put_thread(prev);

	thread_func_t thread_func =
		trigger_thread_get_entry_fn_event(thread->kind);
	trigger_thread_load_state_event(true);

	if (thread_func != NULL) {
		preempt_enable();
		thread_func(thread->params);
	}

	thread_exit();
}

thread_t *
thread_arch_switch_thread(thread_t *next_thread, ticks_t *schedtime)
{
	// The previous thread and the scheduling time must be kept in X0 and X1
	// to ensure that thread_arch_main() receives them as arguments on the
	// first context switch.
	register thread_t *old __asm__("x0")   = thread_get_self();
	register ticks_t   ticks __asm__("x1") = *schedtime;

	// The remaining hard-coded registers here are only needed to ensure a
	// correct clobber list below. The union of the clobber list, hard-coded
	// registers and explicitly saved registers (x29, sp and pc) must be the
	// entire integer register state.
	register register_t old_pc __asm__("x2");
	register register_t old_sp __asm__("x3");
	register register_t old_fp __asm__("x4");
	register uintptr_t  old_context __asm__("x5") =
		(uintptr_t)&old->context.pc;
	static_assert(offsetof(thread_t, context.sp) ==
			      offsetof(thread_t, context.pc) +
				      sizeof(next_thread->context.pc),
		      "PC and SP must be adjacent in context");
	static_assert(offsetof(thread_t, context.fp) ==
			      offsetof(thread_t, context.sp) +
				      sizeof(next_thread->context.sp),
		      "SP and FP must be adjacent in context");

	// The new PC must be in x16 or x17 so ARMv8.5-BTI will treat the BR
	// below as a call trampoline, and thus allow it to jump to the BTI C
	// instruction at a new thread's entry point.
	register register_t new_pc __asm__("x16") = next_thread->context.pc;
	register register_t new_sp __asm__("x6")  = next_thread->context.sp;
	register register_t new_fp __asm__("x7")  = next_thread->context.fp;
	register uintptr_t  new_tls_base __asm__("x8") =
		thread_get_tls_base(next_thread);

	__asm__ volatile(
		"adr	%[old_pc], .Lthread_continue.%=		;"
		"mov	%[old_sp], sp				;"
		"mov	%[old_fp], x29				;"
		"mov   sp, %[new_sp]				;"
		"mov   x29, %[new_fp]				;"
		"msr	TPIDR_EL2, %[new_tls_base]		;"
		"stp	%[old_pc], %[old_sp], [%[old_context]]	;"
		"str	%[old_fp], [%[old_context], 16]		;"
		"br	%[new_pc]				;"
		".Lthread_continue.%=:				;"
#if defined(ARCH_ARM_FEAT_BTI)
		"bti	j					;"
#endif
		: [old] "+r"(old), [old_pc] "=&r"(old_pc),
		  [old_sp] "=&r"(old_sp), [old_fp] "=&r"(old_fp),
		  [old_context] "+r"(old_context), [new_pc] "+r"(new_pc),
		  [new_sp] "+r"(new_sp), [new_fp] "+r"(new_fp),
		  [new_tls_base] "+r"(new_tls_base), [ticks] "+r"(ticks)
		: /* This must not have any inputs */
		: "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x17", "x18",
		  "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27",
		  "x28", "x30", "cc", "memory");

	// Update schedtime from the tick count passed by the previous thread
	*schedtime = ticks;

	return old;
}

noreturn void
thread_arch_set_thread(thread_t *thread)
{
	// This should only be called on the idle thread during power-up, which
	// should already be the current thread for TLS. It discards the current
	// execution state.
	assert(thread == thread_get_self());
	assert(thread == idle_thread());

	// The previous thread and the scheduling time must be kept in X0 and X1
	// to ensure that thread_arch_main() receives them as arguments on the
	// first context switch during CPU cold boot. The scheduling time is set
	// to 0 because we consider the idle thread to have been scheduled at
	// the epoch. These are unused on warm boot, which is always resuming a
	// thread_freeze() call.
	register thread_t *old __asm__("x0")   = thread;
	register ticks_t   ticks __asm__("x1") = (ticks_t)0U;

	// The new PC must be in x16 or x17 so ARMv8.5-BTI will treat the BR
	// below as a call trampoline, and thus allow it to jump to the BTI C
	// instruction at a new thread's entry point.
	register register_t new_pc __asm__("x16");
	new_pc		  = thread->context.pc;
	register_t new_sp = thread->context.sp;
	register_t new_fp = thread->context.fp;

	__asm__ volatile(
		"mov   sp, %[new_sp]			;"
		"mov   x29, %[new_fp]			;"
		"br	%[new_pc]			;"
		:
		: [old] "r"(old), [ticks] "r"(ticks), [new_pc] "r"(new_pc),
		  [new_sp] "r"(new_sp), [new_fp] "r"(new_fp)
		: "memory");
	__builtin_unreachable();
}

register_t
thread_freeze(fptr_t fn, register_t param, register_t resumed_result)
{
	TRACE(DEBUG, INFO, "thread_freeze start fn: {:#x} param: {:#x}",
	      (uintptr_t)fn, (uintptr_t)param);

	trigger_thread_save_state_event();

	thread_t *thread = thread_get_self();
	assert(thread != NULL);

	// The parameter must be kept in X0 so the freeze function gets it as an
	// argument.
	register register_t x0 __asm__("x0") = param;

	// The remaining hard-coded registers here are only needed to
	// ensure a correct clobber list below. The union of the clobber
	// list, fixed output registers and explicitly saved registers
	// (x29, sp and pc) must be the entire integer register state.
	register register_t saved_pc __asm__("x1");
	register register_t saved_sp __asm__("x2");
	register uintptr_t  context __asm__("x3") =
		(uintptr_t)&thread->context.pc;
	register fptr_t fn_reg __asm__("x4") = fn;
	register bool	is_resuming __asm__("x5");

	static_assert(offsetof(thread_t, context.sp) ==
			      offsetof(thread_t, context.pc) +
				      sizeof(thread->context.pc),
		      "PC and SP must be adjacent in context");
	static_assert(offsetof(thread_t, context.fp) ==
			      offsetof(thread_t, context.sp) +
				      sizeof(thread->context.sp),
		      "SP and FP must be adjacent in context");

	__asm__ volatile(
		"adr	%[saved_pc], .Lthread_freeze.resumed.%=	;"
		"mov	%[saved_sp], sp				;"
		"stp	%[saved_pc], %[saved_sp], [%[context]]	;"
		"str	x29, [%[context], 16]			;"
		"blr	%[fn_reg]				;"
		"mov	%[is_resuming], 0			;"
		"b	.Lthread_freeze.done.%=			;"
		".Lthread_freeze.resumed.%=:			;"
#if defined(ARCH_ARM_FEAT_BTI)
		"bti	j					;"
#endif
		"mov	%[is_resuming], 1			;"
		".Lthread_freeze.done.%=:			;"
		: [is_resuming] "=%r"(is_resuming), [saved_pc] "=&r"(saved_pc),
		  [saved_sp] "=&r"(saved_sp), [context] "+r"(context),
		  [fn_reg] "+r"(fn_reg), "+r"(x0)
		: /* This must not have any inputs */
		: "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14",
		  "x15", "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
		  "x24", "x25", "x26", "x27", "x28", "x30", "cc", "memory");

	if (is_resuming) {
		x0 = resumed_result;
		trigger_thread_load_state_event(false);

		TRACE(DEBUG, INFO, "thread_freeze resumed: {:#x}", x0);
	} else {
		TRACE(DEBUG, INFO, "thread_freeze returned: {:#x}", x0);
	}

	return x0;
}

noreturn void
thread_reset_stack(fptr_noreturn_t fn, register_t param)
{
	thread_t	   *thread	     = thread_get_self();
	register register_t x0 __asm__("x0") = param;
	uintptr_t new_sp = (uintptr_t)thread->stack_base + thread->stack_size;

	__asm__ volatile("mov	sp, %[new_sp]	;"
			 "mov	x29, 0		;"
			 "blr	%[new_pc]	;"
			 :
			 : [new_pc] "r"(fn), [new_sp] "r"(new_sp), "r"(x0)
			 : "memory");
	panic("returned to thread_reset_stack()");
}

void
thread_arch_init_context(thread_t *thread)
{
	assert(thread != NULL);

	thread->context.pc = (uintptr_t)thread_arch_main;
	thread->context.sp = (uintptr_t)thread->stack_base + thread->stack_size;
	thread->context.fp = (uintptr_t)0;
}

error_t
thread_arch_map_stack(thread_t *thread)
{
	error_t err;

	assert(thread != NULL);
	assert(thread->stack_base != 0U);

	partition_t *partition = thread->header.partition;
	paddr_t	     stack_phys =
		partition_virt_to_phys(partition, thread->stack_mem);

	pgtable_hyp_start();
	err = pgtable_hyp_map(partition, thread->stack_base, thread->stack_size,
			      stack_phys, PGTABLE_HYP_MEMTYPE_WRITEBACK,
			      PGTABLE_ACCESS_RW,
			      VMSA_SHAREABILITY_INNER_SHAREABLE);
	pgtable_hyp_commit();

	return err;
}

void
thread_arch_unmap_stack(thread_t *thread)
{
	pgtable_hyp_start();
	pgtable_hyp_unmap(thread->header.partition, thread->stack_base,
			  thread->stack_size, thread->stack_size);
	pgtable_hyp_commit();
}
