// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

// Miscellaneous utility macros.
//
// These all have simple definitions - no compiler builtins or other language
// extensions. Look in compiler.h for those.

#define util_width(x) (sizeof(x) * (size_t)CHAR_BIT)

#define util_bit(b)  ((uintmax_t)1U << (b))
#define util_mask(n) (util_bit(n) - 1U)

// Safe variants of above bitwise operations when shift value may be greater
// than or equal to the register size.
#define util_bit_safe(b)                                                       \
	(((b) < util_width(uintmax_t)) ? util_bit(b) : (uintmax_t)0U)
#define util_mask_safe(n)                                                      \
	(((n) < util_width(uintmax_t)) ? util_mask(n) : ~(uintmax_t)0U)

#define util_max(x, y) (((x) > (y)) ? (x) : (y))
#define util_min(x, y) (((x) < (y)) ? (x) : (y))

// Arithmetic predicates with intent that is not obvious when open-coded
#define util_is_p2_or_zero(x) (((x) & ((x) - 1U)) == 0U)
#define util_is_p2(x)	      (((x) != 0U) && util_is_p2_or_zero(x))
#define util_is_baligned(x, a)                                                 \
	(assert(util_is_p2(a)), (((x) & ((a) - 1U)) == 0U))
#define util_is_p2aligned(x, b)	 (((x) & (util_bit(b) - 1U)) == 0U)
#define util_add_overflows(a, b) ((a) > ~(__typeof__((a) + (b)))(b))

// This version can be used in static asserts
#define util_is_baligned_assert(x, a)                                          \
	(util_is_p2(a) && (((x) & ((a) - 1U)) == 0U))

// Align up or down to bytes (which must be a power of two)
#if defined(__TYPED_DSL__)
#define util_balign_down(x, a) ((x) & ~((a) - 1U))
#else
#define util_balign_down(x, a)                                                 \
	(assert(util_is_p2(a)), (x) & ~((__typeof__(x))(a) - 1U))
#endif
#define util_balign_up(x, a) util_balign_down((x) + ((a) - 1U), a)

// Round up or down to a multiple of an unsigned constant, which may not be a
// power of two. Rounding to a non-constant at runtime should be avoided,
// because it will perform a slow divide operation.
#define util_round_down(x, a) ((x) - ((x) % (a)))
#define util_round_up(x, a)   util_round_down((x) + ((a) - 1U), (a))

// Align up or down to a power-of-two size (in bits)
#define util_p2align_down(x, b)                                                \
	(assert(util_width(x) > (b)), (((x) >> (b)) << (b)))
#define util_p2align_up(x, b) util_p2align_down((x) + util_bit(b) - 1U, b)

// Generate an identifier that can be declared inside a macro without
// shadowing anything else declared in the same file, given a base name to
// disambiguate uses within one macro expansion. Generally the name should be
// prefixed with the name of the macro it's being used in.
//
// Note that this should only ever be used as a macro parameter; otherwise
// it is difficult to determine what identifier it expanded to.
#define util_cpp_unique_ident(name) util_cpp_paste_expanded(name, __LINE__)

// Paste two tokens together, after macro-expansion of the arguments.
#define util_cpp_paste_expanded(name, suffix) util_cpp_paste(name, suffix)

// Paste two tokens together, before macro-expansion of the arguments.
//
// This is only really useful in util_cpp_paste_expanded(). In any other macro
// definition, use ## directly, which is equivalent and more concise.
#define util_cpp_paste(name, suffix) name##suffix

// Return the size (including NUL) of a string literal.
#define sizeof_literal(s) _Generic(&(s), char (*)[sizeof(s)]: sizeof(s))

// Return the number of elements in an array.
#define util_array_size(a) (sizeof(a) / sizeof((a)[0]))

// Return the size of a structure member.
#define util_sizeof_member(type, member) sizeof(((type *)NULL)->member)

// Return the number of elements in an array-typed structure member.
#define util_member_array_size(type, member)                                   \
	(util_sizeof_member(type, member) / util_sizeof_member(type, member[0]))

// Check whether a given offset is with the bounds of a structure member
#define util_offset_in_range(offset, type, member)                             \
	(((offset) >= offsetof(type, member)) &&                               \
	 ((offset) <                                                           \
	  (offsetof(type, member) + util_sizeof_member(type, member))))

// Check whether a given range equals the bounds of a structure member
#define util_range_is_member(offset, size, type, member)                       \
	(((offset) == offsetof(type, member)) &&                               \
	 ((size) == util_sizeof_member(type, member)))

// Check whether a given range is at the start of and within the bounds of a
// structure member
#define util_range_at_member(offset, size, type, member)                       \
	(((offset) == offsetof(type, member)) &&                               \
	 ((size) <= util_sizeof_member(type, member)))

// clang-format off
// Produce a case range with ... separated by spaces, since clang-format does not
// have any support for GNU case-ranges and otherwise removes spaces before or
// after the ...
#define util_case_range(a, b) (a) ... (b)
// Provide a case range `<offset_start> ... <offset_end>` for a given structure
// member. Case range is inclusive so we need to subtract 1 from the end value
#define util_offset_case_range(type, member)                                   \
	offsetof(type, member) ... (offsetof(type, member) +                   \
				  util_sizeof_member(type, member) - 1U)
// clang-format on

// Convert an expanded preprocessor macro into a string.
#define util_cpp_stringify_expanded(x) util_cpp_stringify(x)

// Convert a token into a string
#define util_cpp_stringify(x) #x
