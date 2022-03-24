// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Macros wrapping miscellaneous compiler builtins to make them easier to use
// correctly. Note that this is not intended for compiler independence, only
// for readability.

// Branch probability hints.
//
// Note that the argument and result of each of these macros must be
// essentially boolean (MISRA rule 14.4), but the argument and result of
// __builtin_expect have type long.
#define compiler_expected(x)   (__builtin_expect((x) ? 1 : 0, 1) != 0)
#define compiler_unexpected(x) (__builtin_expect((x) ? 1 : 0, 0) != 0)

// Bit operations.
//
// On ARM, prefer clz and clrsb as they expand to single instructions (CLZ and
// CLS). ffs and ctz need an extra RBIT first.

#if defined(CLANG_CTU_AST)

// Clang CTU analysis does not support _Generic in ASTs; cast everything to
// uintmax_t instead, and offset the results for the ones that are
// size-dependent.

#define compiler_bitsize_offset(x)                                             \
	(8 * (int)(sizeof(x) - sizeof(unsigned long long)))
#define compiler_ffs(x) (index_t) __builtin_ffsll((unsigned long long)(x))
#define compiler_clz(x)                                                        \
	(index_t)(compiler_bitsize_offset(x) +                                 \
		  __builtin_clzll((unsigned long long)(x)))
#define compiler_ctz(x) (index_t) __builtin_ctzll((unsigned long long)(x))
#define compiler_clrsb(x)                                                      \
	(index_t)(compiler_bitsize_offset(x) +                                 \
		  __builtin_clrsbll((long long)(x)))

// Ensure this is never compiled to object code
__asm__(".error");

#else

// clang-format off
#define compiler_ffs(x) (index_t)_Generic(				       \
	(x),								       \
	long long: __builtin_ffsll(x),					       \
	unsigned long long: __builtin_ffsll((long long)(x)),		       \
	long: __builtin_ffsl(x),					       \
	unsigned long: __builtin_ffsl((long)(x)),			       \
	int: __builtin_ffs(x),						       \
	unsigned int: __builtin_ffs((int)(x)))

#define compiler_clz(x) (assert((x) != 0U), (index_t)_Generic(		       \
	(x),								       \
	unsigned long long: __builtin_clzll,				       \
	unsigned long: __builtin_clzl,				       \
	unsigned int: __builtin_clz)(x))

#define compiler_ctz(x) (assert((x) != 0U), (index_t)_Generic(		       \
	(x),								       \
	unsigned long long: __builtin_ctzll,				       \
	unsigned long: __builtin_ctzl,				       \
	unsigned int: __builtin_ctz)(x))

#define compiler_clrsb(x) (index_t)_Generic(				       \
	(x), long long: __builtin_clrsbll,				       \
	long: __builtin_clrsbl,					       \
	int: __builtin_clrsb)(x)
// clang-format on

#endif // !defined(CLANG_CTU_AST)

#define compiler_msb(x) ((sizeof(x) * 8U) - 1U - compiler_clz(x))

// Object sizes, for use in minimum buffer size assertions. These return
// (size_t)-1 if the size cannot be determined statically, so the assertion
// should become a no-op in that case. LLVM has an intrinsic for this, so
// the static determination can be made after inlining by LTO.
#define compiler_sizeof_object(ptr)    __builtin_object_size((ptr), 1)
#define compiler_sizeof_container(ptr) __builtin_object_size((ptr), 0)
