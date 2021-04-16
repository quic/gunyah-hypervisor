// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// This file documents the permitted function attributes, and defines short
// names for them. Do not use any attribute specifier unless it is listed
// below.
//
// An attribute that is listed here may be used directly (not via a macro) on an
// inline function in an interface or asm header, to avoid introducing an
// inconvenient dependency on this header. Attributes used this way must be
// exactly as defined below; no extra whitespace or punctuation, and no listing
// multiple attributes in a single __attribute__ specifier.
//
// This rule does not apply to language constructs that have an effect that is
// similar or equivalent to an attribute, such as _Noreturn or _Alignas.

// Don't inline the function. This is used to mark cold functions for which
// inlining would be a waste of space and/or would make debugging inconvenient.
#define NOINLINE __attribute__((noinline))

// Always inline the function. This is used for certain inline assembler
// wrappers which cannot safely be wrapped in a function call, such as
// load-exclusive instructions which might lose their exclusivity.
#define ALWAYS_INLINE __attribute__((always_inline))

// Mark the function this applies to as WEAK
#define WEAK __attribute__((weak))
