// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// From ARMv8.0 onwards (but not ARMv7), losing the local monitor triggers an
// event, so we can obtain the required semantics by loading with LDAX* and
// polling with WFE. The default store-release updates are sufficient on their
// own. See the generic event header for detailed requirements.

#define asm_event_wait(p) __asm__ volatile("wfe" ::"m"(*p))

#if defined(CLANG_CTU_AST)

// Clang CTU analysis does not support _Generic in ASTs; use the default
// definition of load-before-wait.
#include <asm-generic/event.h>

// Ensure this is never compiled to object code
__asm__(".error");

#else

// clang-format off
#define asm_event_load_before_wait(p) _Generic(				       \
	(p),								       \
	_Atomic uint64_t *: asm_event_load64_before_wait,		       \
	_Atomic uint32_t *: asm_event_load32_before_wait,		       \
	_Atomic uint16_t *: asm_event_load16_before_wait,		       \
	_Atomic uint8_t *: asm_event_load8_before_wait,		       \
	_Atomic bool *: asm_event_loadbool_before_wait)(p)
// clang-format on

#define asm_event_load_bf_before_wait(name, p)                                 \
	name##_cast(asm_event_load_before_wait(name##_atomic_ptr_raw(p)))

#include <asm-generic/event.h>

static inline ALWAYS_INLINE bool
asm_event_loadbool_before_wait(_Atomic bool *p)
{
	uint8_t ret;
	__asm__("ldaxrb %w0, %1" : "=r"(ret) : "Q"(*p));
	return ret != 0U;
}

static inline ALWAYS_INLINE uint8_t
asm_event_load8_before_wait(_Atomic uint8_t *p)
{
	uint8_t ret;
	__asm__("ldaxrb %w0, %1" : "=r"(ret) : "Q"(*p));
	return ret;
}

static inline ALWAYS_INLINE uint16_t
asm_event_load16_before_wait(_Atomic uint16_t *p)
{
	uint16_t ret;
	__asm__("ldaxrh %w0, %1" : "=r"(ret) : "Q"(*p));
	return ret;
}

static inline ALWAYS_INLINE uint32_t
asm_event_load32_before_wait(_Atomic uint32_t *p)
{
	uint32_t ret;
	__asm__("ldaxr %w0, %1" : "=r"(ret) : "Q"(*p));
	return ret;
}

static inline ALWAYS_INLINE uint64_t
asm_event_load64_before_wait(_Atomic uint64_t *p)
{
	uint64_t ret;
	__asm__("ldaxr %0, %1" : "=r"(ret) : "Q"(*p));
	return ret;
}

#endif // !defined(CLANG_CTU_AST)
