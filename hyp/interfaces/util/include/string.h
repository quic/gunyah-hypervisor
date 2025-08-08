// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

// Local version of the standard-defined string.h
//
// Only the memory block manipulation functions (mem*()) are declared. The
// hypervisor has no need to operate on real strings, so the string
// manipulation functions (str*()) are left undefined.
//
// Note: MISRA Required Rule 21.2 states that reserved identifiers are not to
// be declared, and gives a memcpy declaration as a specific non-conforming
// example. However, the identifiers declared here (including memcpy) are
// _not_ reserved: the hypervisor is built in freestanding mode (as asserted
// below), which does not guarantee their presence and therefore must not give
// them special behaviour (see C18 clause 4, item 6). The Clang/GCC option
// -ffreestanding implies -fno-builtin for this reason.
//
// Also, we _must_ implement these functions ourselves with their standard
// semantics (regardless of MISRA 21.2) because the LLVM and GCC backends
// assume they are provided by the environment, and will generate calls to
// them even when the frontend is in freestanding mode.

#if !defined(HYP_STANDALONE_TEST)
_Static_assert(__STDC_HOSTED__ == 0,
	       "This file deviates from MISRA rule 21.2 in hosted mode");
#endif

extern size_t
memscpy(void *s1, size_t s1_size, const void *s2, size_t s2_size);

extern void *
memcpy(void *restrict s1, const void *restrict s2, size_t n);

#if defined(HYP_STANDALONE_TEST)
extern void *
memmove(void *s1, const void *s2, size_t n);

extern int
memcmp(const void *s1, const void *s2, size_t n);
#endif

// Secure memmove, requires valid src and dst buffers, and dst_sz >= n
size_t
memsmove(void *dst, size_t dst_sz, const void *src, size_t n);

extern void *
memset(void *s, int c, size_t n);

typedef int    errno_t;
typedef size_t rsize_t;

// A secure memset, guaranteed not to be optimized out
extern errno_t
memset_s(void *s, rsize_t smax, int c, rsize_t n);

// Returns the string length, without counting the trailing '\0' or maxlen if
// once maxlen characters have been searched. This function will not read
// memory beyond (str+maxlen-1).
extern rsize_t
util_strnlen(const char *str, rsize_t maxlen);

// Returns a pointer to the first character 'c' found in string str. If the
// string terminating \0 character is reached or 'c' is not found within the
// first maxlen bytes, NULL is returned.
extern char *
util_strnchr(const char *str, int32_t c, rsize_t maxlen);

// Compare strings.
//
// Up to 'n' bytes are compared, where 'n' is the minimum of the lengths of the
// two strings (including the trailing '\0') and 'maxlen'.
//
// Returns 0 if the strings are equal, or a negative value if s1 < s2, or a
// positive value if s1 > s2.
extern int
util_strncmp(const char *s1, const char *s2, size_t maxlen);

// Compare a substring to another string.
//
// This is almost the same as util_strncmp, except that the 's1_len'-th byte of
// s1 is treated as it were '\0'.
extern int
util_substrncmp(const char *s1, size_t s1_len, const char *s2, size_t maxlen);
