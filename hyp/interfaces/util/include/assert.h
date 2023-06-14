// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Local version of the standard-defined assert.h

#if !defined(HYP_STANDALONE_TEST)
_Static_assert(__STDC_HOSTED__ == 0,
	       "This file deviates from MISRA rule 21.2 in hosted mode");
#endif

#define static_assert _Static_assert

// Use a Clang extension to assert that an expression is true if its value
// can be statically determined.
static inline _Bool
assert_if_const(_Bool x)
	__attribute__((diagnose_if(!(x), "Static assert failure", "error"),
		       always_inline))
{
	return x;
}

#if defined(__KLOCWORK__)
_Noreturn void
panic(const char *str);
#define assert(x) ((x) ? (void)0 : panic("assertion"))
#elif defined(NDEBUG)
// Strictly this should be defined to ((void)0), but since assert_if_const()
// has no runtime overhead we may as well use it. Also the fact that the
// expression is evaluated means we don't need to put maybe-unused annotations
// on variables that are only used in assert expressions.
#define assert(x) (void)assert_if_const(x)
#else
_Noreturn void
assert_failed(const char *file, int line, const char *func, const char *err);
#define assert(x)                                                              \
	(assert_if_const(x) ? (void)0                                          \
			    : assert_failed(__FILE__, __LINE__, __func__, #x))
#endif

#if defined(VERBOSE) && VERBOSE
#define assert_debug assert
#else
#define assert_debug(x) (void)assert_if_const(x)
#endif
