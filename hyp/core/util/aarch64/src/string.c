// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>
#include <string.h>

#include <compiler.h>
#include <panic.h>
#include <util.h>

#include <asm/cpu.h>
#include <asm/prefetch.h>

// Assembly functions. All of these come in at least three variants:
//
// - _align16 for at least 16 bytes with target known to be 16-aligned;
// - _alignable for at least 31 bytes with unknown target alignment;
// - _below32 for less than 32 bytes (i.e. one access of each size).
//
// Note the overlap between _alignable and _below32 at n==31; either variant
// may be used at that size. We use _below32 because the logic to trigger its
// first 16-byte copy is simpler.
//
// For memset to zero there is additionally a _dczva variant, where the target
// is aligned to a DC ZVA block (typically a 64-byte cache line) and is at
// least that size.
//
// The variants other than _below32 fall through to the more-aligned versions
// once the necessary alignment has been established.

// TODO: Clang does only simple constant propagation when LTO is enabled,
// preferring to leave it until LTO. However, the LLVM IR has no way to
// represent __builtin_constant_p(). So it is not really possible to use
// __builtin_constant_p() here to avoid runtime checks, since it will nearly
// always evaluate to 0.
//
// To make any static assertions or build-time variant selection actually
// effective, we need to move all of the definitions below into inlines in a
// header. We would then need to either include it here to generate extern
// definitions for the backend to use, or else define them in the assembly.

void
memcpy_below32(void *restrict s1, const void *restrict s2, size_t n);

void
memcpy_alignable(void *restrict s1, const void *restrict s2, size_t n);

void
memcpy_align16(void *restrict s1, const void *restrict s2, size_t n);

void
memcpy_bytes(void *restrict s1, const void *restrict s2, size_t n);

void
memset_zeros_alignable(void *s, size_t n);

void
memset_zeros_below32(void *s, size_t n);

void
memset_zeros_align16(void *s, size_t n);

void
memset_zeros_dczva(void *s, size_t n);

void
memset_alignable(void *s, uint8_t c, size_t n);

void
memset_below32(void *s, uint64_t cs, size_t n);

void
memset_align16(void *s, uint64_t cs, size_t n);

static void *
memcpy_impl(void *s1, const void *s2, size_t n)
{
	assert(compiler_sizeof_object(s1) >= n);
	assert(compiler_sizeof_object(s2) >= n);
	if (n == 0U) {
		// Nothing to do.
	} else if (n < 32U) {
		prefetch_store_keep(s1);
		prefetch_load_stream(s2);
		memcpy_below32(s1, s2, n);
	} else {
		prefetch_store_keep(s1);
		prefetch_load_stream(s2);
		uintptr_t a16 = (uintptr_t)s1 & (uintptr_t)15;
		if (a16 == 0U) {
			memcpy_align16(s1, s2, n);
		} else {
			memcpy_alignable(s1, s2, n);
		}
	}

	return s1;
}

void *
memcpy(void *restrict s1, const void *restrict s2, size_t n)
{
	return memcpy_impl(s1, s2, n);
}

size_t
memscpy(void *s1, size_t s1_size, const void *s2, size_t s2_size)
{
	size_t copy_size = util_min(s1_size, s2_size);
	(void)memcpy_impl(s1, s2, copy_size);
	return copy_size;
}

static void
memmove_bytes_reverse(uint8_t *dst, const uint8_t *src, size_t n)
{
	assert((uintptr_t)src < (uintptr_t)dst);

	size_t num = n;
	// move to a higher address, copy backwards
	const uint8_t *srcr;
	uint8_t	      *dstr;
	srcr = src + (num - 1U);
	dstr = dst + (num - 1U);

	for (; num != 0U; num--) {
		*dstr = *srcr;
		dstr--;
		srcr--;
	}
}

size_t
memsmove(void *dst, size_t dst_sz, const void *src, size_t n)
{
	if (compiler_unexpected((src == NULL) || (dst == NULL))) {
		panic("memsmove: null arg");
	}
	if (n == 0U) {
		goto out;
	}
	if (compiler_unexpected(
		    (dst_sz < n) || (dst_sz == 0U) ||
		    util_add_overflows((uintptr_t)dst, dst_sz - 1U) ||
		    util_add_overflows((uintptr_t)src, n - 1U))) {
		panic("memsmove: invalid arg");
	}

	if ((uintptr_t)dst == (uintptr_t)src) {
		// Nothing to do.
	} else if ((uintptr_t)dst < (uintptr_t)src) {
		(void)memscpy(dst, dst_sz, src, n);
	} else if (((uintptr_t)src + (n - 1U)) < (uintptr_t)dst) {
		(void)memscpy(dst, dst_sz, src, n);
	} else {
		memmove_bytes_reverse(dst, src, n);
	}

out:
	return n;
}

#if defined(HYP_STANDALONE_TEST)
void *
memmove(void *dst, const void *src, size_t n)
{
	(void)memsmove(dst, n, src, n);
	return dst;
}
#endif

errno_t
memset_s(void *s, rsize_t smax, int c, rsize_t n)
{
	assert(compiler_sizeof_object(s) >= smax);
	uintptr_t a16 = (uintptr_t)s & (uintptr_t)15;

	errno_t err = 0;

	if (s == NULL) {
		err = 1;
		goto out_null;
	}
	if (n > smax) {
		err = 1;
		n   = smax;
	}

	if (n == 0U) {
		// Nothing to do.
	} else if (c == 0) {
		uintptr_t a_zva = (uintptr_t)s &
				  (uintptr_t)util_mask(CPU_DCZVA_BITS);
		if (n < 32U) {
			prefetch_store_keep(s);
			memset_zeros_below32(s, n);
		} else if ((a_zva == 0U) && ((n >> CPU_DCZVA_BITS) > 0U)) {
			memset_zeros_dczva(s, n);
		} else if (a16 == 0U) {
			prefetch_store_keep(s);
			memset_zeros_align16(s, n);
		} else {
			prefetch_store_keep(s);
			memset_zeros_alignable(s, n);
		}
	} else {
		uint64_t cs = (uint64_t)(uint8_t)c;
		cs |= cs << 8;
		cs |= cs << 16;
		cs |= cs << 32;
		if (n < 32U) {
			prefetch_store_keep(s);
			memset_below32(s, cs, n);
		} else if (a16 == 0U) {
			prefetch_store_keep(s);
			memset_align16(s, cs, n);
		} else {
			prefetch_store_keep(s);
			memset_alignable(s, (uint8_t)c, n);
		}
	}

out_null:
	return err;
}

void *
memset(void *s, int c, size_t n)
{
	(void)memset_s(s, n, c, n);
	return s;
}

rsize_t
util_strnlen(const char *str, rsize_t maxlen)
{
	const char *pos = str;
	rsize_t	    len = 0U;

	assert(str != NULL);
	assert(maxlen != 0U);

	while (pos[len] != '\0') {
		len++;
		if (len == maxlen) {
			break;
		}
	}

	return len;
}

char *
util_strnchr(const char *str, int32_t c, rsize_t maxlen)
{
	uintptr_t   ret = (uintptr_t)NULL;
	const char *pos = str;

	assert(str != NULL);
	assert(maxlen != 0U);

	for (rsize_t len = 0U; len < maxlen; len++) {
		if (pos[len] == '\0') {
			break;
		}
		if (pos[len] == (char)c) {
			ret = (uintptr_t)&pos[len];
			break;
		}
	}

	return (char *)ret;
}

int
util_strncmp(const char *s1, const char *s2, size_t maxlen)
{
	assert(s1 != NULL);
	assert(s2 != NULL);

	int ret = 0;

	for (size_t i = 0U; i < maxlen; i++) {
		char c1 = s1[i];
		char c2 = s2[i];

		// If c1 == c2 and c1 != '\0', then c2 != '\0'.
		if ((c1 == '\0') || (c1 != c2)) {
			ret = (int)(unsigned char)c1 - (int)(unsigned char)c2;
			goto out;
		}
	}

out:
	return ret;
}

int
util_substrncmp(const char *s1, size_t s1_len, const char *s2, size_t maxlen)
{
	assert(s1 != NULL);
	assert(s2 != NULL);

	int ret = 0;

	for (size_t i = 0U; i < maxlen; i++) {
		char c1;
		if ((i + 1U) < s1_len) {
			c1 = s1[i];
		} else {
			c1 = '\0';
		}
		char c2 = s2[i];

		// If c1 == c2 and c1 != '\0', then c2 != '\0'.
		if ((c1 == '\0') || (c1 != c2)) {
			ret = (int)(unsigned char)c1 - (int)(unsigned char)c2;
			goto out;
		}
	}

out:
	return ret;
}
