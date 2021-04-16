// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <compiler.h>
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

void *
memcpy(void *restrict s1, const void *restrict s2, size_t n)
{
	assert(compiler_sizeof_object(s1) >= n);
	assert(compiler_sizeof_object(s2) >= n);
	if (n == 0) {
		// Nothing to do.
	} else if (n < 32) {
		prefetch_store_keep(s1);
		prefetch_load_stream(s2);
		memcpy_below32(s1, s2, n);
	} else {
		prefetch_store_keep(s1);
		prefetch_load_stream(s2);
		uintptr_t a16 = (uintptr_t)s1 & (uintptr_t)15;
		if (a16 == 0) {
			memcpy_align16(s1, s2, n);
		} else {
			memcpy_alignable(s1, s2, n);
		}
	}

	return s1;
}

extern size_t
memscpy(void *restrict s1, size_t s1_size, const void *restrict s2,
	size_t s2_size)
{
	size_t copy_size = util_min(s1_size, s2_size);

	memcpy(s1, s2, copy_size);

	return copy_size;
}

void *
memmove(void *s1, const void *s2, size_t n)
{
	// The hypervisor should never need memmove(), but unfortunately the
	// test program won't link if we don't define it. This is because
	// static glibc defines it in the same object as memcpy(), so if we
	// don't define it the calls in the glibc startup will pull in the
	// glibc version of memcpy() and cause duplicate definition errors.
	//
	// In cases where we know our fast memcpy will work, just call that.
	// Otherwise call a slow memcpy which is only defined in the test
	// program.
	if ((uintptr_t)s1 == (uintptr_t)s2) {
		// Nothing to do.
	} else if ((uintptr_t)s1 < (uintptr_t)s2) {
		(void)memcpy(s1, s2, n);
	} else if ((uintptr_t)s2 + CPU_MEMCPY_STRIDE < (uintptr_t)s1) {
		(void)memcpy(s1, s2, n);
	} else if (((uintptr_t)s1 + n <= (uintptr_t)s2) &&
		   ((uintptr_t)s2 + n <= (uintptr_t)s1)) {
		(void)memcpy(s1, s2, n);
	} else {
		(void)memcpy_bytes(s1, s2, n);
	}
	return s1;
}

void *
memset(void *s, int c, size_t n)
{
	assert(compiler_sizeof_object(s) >= n);
	uintptr_t a16 = (uintptr_t)s & (uintptr_t)15;

	if (n == 0) {
		// Nothing to do.
	} else if (c == 0) {
		uintptr_t a_zva = (uintptr_t)s &
				  (uintptr_t)((1 << CPU_DCZVA_BITS) - 1);
		if (n < 32) {
			prefetch_store_keep(s);
			memset_zeros_below32(s, n);
		} else if ((a_zva == 0) && ((n >> CPU_DCZVA_BITS) > 0U)) {
			memset_zeros_dczva(s, n);
		} else if (a16 == 0) {
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
		if (n < 32) {
			prefetch_store_keep(s);
			memset_below32(s, cs, n);
		} else if (a16 == 0) {
			prefetch_store_keep(s);
			memset_align16(s, cs, n);
		} else {
			prefetch_store_keep(s);
			memset_alignable(s, (uint8_t)c, n);
		}
	}

	return s;
}

size_t
strlen(const char *str)
{
	const char *end = str;

	assert(str != NULL);

	for (; *end != '\0'; end++) {
	}

	return (size_t)((uintptr_t)end - (uintptr_t)str);
}

char *
strchr(const char *str, int c)
{
	uintptr_t   ret = (uintptr_t)NULL;
	const char *end = str;

	for (; *end != '\0'; end++) {
		if (*end == c) {
			ret = (uintptr_t)end;
			break;
		}
	}

	return (char *)ret;
}
