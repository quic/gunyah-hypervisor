// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// FIXME:
// Integrate these tests into unittest configuration

#include <assert.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>

#include <attributes.h>
#include <errno.h>

#include <asm/cpu.h>

#define LARGE_ALIGN (1 << CPU_DCZVA_BITS)
#define SMALL_ALIGN 16
#define MAX_SIZE    2048

#define BUFFER_PAD  1024
#define BUFFER_SIZE (MAX_SIZE + LARGE_ALIGN + (2 * BUFFER_PAD))

#define INIT_BYTE   0xff
#define MEMSET_BYTE 0x42

static uint8_t alignas(LARGE_ALIGN) dst_buffer[BUFFER_SIZE];

static uint8_t alignas(LARGE_ALIGN) src_buffer[BUFFER_SIZE];

typedef int    errno_t;
typedef size_t rsize_t;
extern errno_t
memset_s(void *s, rsize_t smax, int c, size_t n);

noreturn void NOINLINE
assert_failed(const char *file, int line, const char *func, const char *err)
{
	fprintf(stderr, "Assert failed in %s at %s:%d: %s\n", func, file, line,
		err);
	abort();
}

noreturn void NOINLINE
panic(const char *msg)
{
	fprintf(stderr, "panic: %s\n", msg);
	abort();
}

static size_t
memchk(const volatile uint8_t *p, int c, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (p[i] != (uint8_t)c) {
			return i;
		}
	}
	return n;
}

static size_t
memcmpchk(const volatile uint8_t *p, const volatile uint8_t *q, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (p[i] != q[i]) {
			return i;
		}
	}
	return n;
}

static void
memset_test(size_t size, size_t misalign, int c)
{
	size_t start = BUFFER_PAD + misalign;
	size_t end   = start + size;
	size_t pos;

	// We assume that we can memset the whole buffer safely... hopefully
	// any bugs in it won't crash the test before we find them!
	memset(dst_buffer, INIT_BYTE, BUFFER_SIZE);

	memset(&dst_buffer[start], c, size);

	pos = memchk(dst_buffer, INIT_BYTE, start);
	if (pos < start) {
		fprintf(stderr,
			"FAILED: memset(buffer + %#zx, %#x, %#zx) set byte at offset -%#zx to %#x\n",
			start - BUFFER_PAD, c, size, start - pos,
			dst_buffer[pos]);
		exit(2);
	}

	pos = memchk(&dst_buffer[start], c, size);
	if (pos < size) {
		fprintf(stderr,
			"FAILED: memset(buffer + %#zx, %#x, %#zx) set byte at offset %#zx to %#x\n",
			start - BUFFER_PAD, c, size, pos,
			dst_buffer[start + pos]);
		exit(2);
	}

	pos = memchk(&dst_buffer[end], INIT_BYTE, BUFFER_PAD);
	if (pos < BUFFER_PAD) {
		fprintf(stderr,
			"FAILED: memset(buffer + %#zx, %#x, %#zx) set byte at offset %#zx to %#x\n",
			start - BUFFER_PAD, c, size, size + pos,
			dst_buffer[end + pos]);
		exit(2);
	}
}

static void
memset_s_test(size_t size, size_t misalign, int c)
{
	size_t start = BUFFER_PAD + misalign;
	size_t end   = start + size;
	size_t pos;

	// We assume that we can memset the whole buffer safely... hopefully
	// any bugs in it won't crash the test before we find them!
	memset_s(dst_buffer, BUFFER_SIZE, INIT_BYTE, BUFFER_SIZE);

	memset_s(&dst_buffer[start], BUFFER_SIZE, c, size);

	pos = memchk(dst_buffer, INIT_BYTE, start);
	if (pos < start) {
		fprintf(stderr,
			"FAILED: memset(buffer + %#zx, %#x, %#zx) set byte at offset -%#zx to %#x\n",
			start - BUFFER_PAD, c, size, start - pos,
			dst_buffer[pos]);
		exit(2);
	}

	pos = memchk(&dst_buffer[start], c, size);
	if (pos < size) {
		fprintf(stderr,
			"FAILED: memset(buffer + %#zx, %#x, %#zx) set byte at offset %#zx to %#x\n",
			start - BUFFER_PAD, c, size, pos,
			dst_buffer[start + pos]);
		exit(2);
	}

	pos = memchk(&dst_buffer[end], INIT_BYTE, BUFFER_PAD);
	if (pos < BUFFER_PAD) {
		fprintf(stderr,
			"FAILED: memset(buffer + %#zx, %#x, %#zx) set byte at offset %#zx to %#x\n",
			start - BUFFER_PAD, c, size, size + pos,
			dst_buffer[end + pos]);
		exit(2);
	}
}

static void
memset_tests(void)
{
	size_t size, dst_misalign;
	printf("Testing memset...");
	for (size = 0; size <= MAX_SIZE; size++) {
		if ((size % 64) == 0) {
			printf("\n%#5zx: .", size);
		} else {
			printf(".");
		}
		for (dst_misalign = 0; dst_misalign < LARGE_ALIGN;
		     dst_misalign++) {
			memset_test(size, dst_misalign, MEMSET_BYTE);
			memset_test(size, dst_misalign, 0);
			memset_s_test(size, dst_misalign, MEMSET_BYTE);
			memset_s_test(size, dst_misalign, 0);
		}
	}
	printf("\nPASS\n");
}

static void
memcpy_test(size_t size, size_t src_misalign, size_t dst_misalign)
{
	size_t src_start = BUFFER_PAD + src_misalign;
	size_t dst_start = BUFFER_PAD + dst_misalign;
	size_t dst_end	 = dst_start + size;
	size_t pos;

	// We tested memset first, so it should be safe to use it to clear
	// the destination buffer.
	memset(dst_buffer, INIT_BYTE, BUFFER_SIZE);

	memcpy(&dst_buffer[dst_start], &src_buffer[src_start], size);

	pos = memchk(dst_buffer, INIT_BYTE, dst_start);
	if (pos < dst_start) {
		fprintf(stderr,
			"FAILED: memcpy(dst + %#zx, src + %#zx, %#zx) set byte at offset -%#zx to %#x\n",
			dst_start - BUFFER_PAD, src_start - BUFFER_PAD, size,
			dst_start - pos, dst_buffer[pos]);
		exit(2);
	}

	pos = memcmpchk(&dst_buffer[dst_start], &src_buffer[src_start], size);
	if (pos < size) {
		fprintf(stderr,
			"FAILED: memcpy(dst + %#zx, src + %#zx, %#zx) set byte at offset %#zx to %#x (should be %#x)\n",
			dst_start - BUFFER_PAD, src_start - BUFFER_PAD, size,
			pos, dst_buffer[dst_start + pos],
			src_buffer[src_start + pos]);
		exit(2);
	}

	pos = memchk(&dst_buffer[dst_end], INIT_BYTE, BUFFER_PAD);
	if (pos < BUFFER_PAD) {
		fprintf(stderr,
			"FAILED: memcpy(dst + %#zx, src + %#zx, %#zx) set byte at offset %#zx to %#x\n",
			dst_start - BUFFER_PAD, src_start - BUFFER_PAD, size,
			size + pos, dst_buffer[dst_end + pos]);
		exit(2);
	}
}

static void
memcpy_tests(void)
{
	size_t size, src_misalign, dst_misalign;
	printf("Testing memcpy...");
	for (size = 0; size <= MAX_SIZE; size++) {
		if ((size % 64) == 0) {
			printf("\n%#5zx: .", size);
		} else {
			printf(".");
		}
		for (dst_misalign = 0; dst_misalign < LARGE_ALIGN;
		     dst_misalign++) {
			for (src_misalign = 0; src_misalign < SMALL_ALIGN;
			     src_misalign++) {
				memcpy_test(size, src_misalign, dst_misalign);
			}
		}
	}
	printf("\nPASS\n");
}

static void
memmove_test(size_t size, ptrdiff_t overlap)
{
	// We assume here that memmove() is based on memcpy(), so we don't need
	// to re-test with different alignments; just different amounts of
	// overlap is enough.
	size_t src_start = BUFFER_PAD + overlap;
	size_t src_end	 = src_start + size;
	size_t dst_start = BUFFER_PAD;
	size_t dst_end	 = dst_start + size;
	size_t pos;

	// We tested memset first, so it should be safe to use it to clear
	// the destination buffer.
	memset(dst_buffer, INIT_BYTE, BUFFER_SIZE);

	// We also tested memcpy already, so it should be safe to use it to copy
	// some random bytes from the source buffer into the destination buffer
	// at the source location.
	memcpy(&dst_buffer[src_start], src_buffer, size);

	// Now move from the source location to the destination location, both
	// within the destination buffer.
	memmove(&dst_buffer[dst_start], &dst_buffer[src_start], size);

	size_t start = (overlap > 0) ? dst_start : src_start;
	pos	     = memchk(dst_buffer, INIT_BYTE, start);
	if (pos < start) {
		fprintf(stderr,
			"FAILED: memmove(dst, dst + %td, %#zx) set byte at dst + %td to %#x (1)\n",
			overlap, size, dst_start - pos, dst_buffer[pos]);
		exit(2);
	}

	pos = memcmpchk(&dst_buffer[dst_start], src_buffer, size);
	if (pos < size) {
		fprintf(stderr,
			"FAILED: memmove(dst, dst + %td, %#zx) set byte at dst + %#zx to %#x (should be %#x) (2)\n",
			overlap, size, pos, dst_buffer[dst_start + pos],
			src_buffer[pos]);
		exit(2);
	}

	if ((overlap > 0) && (size > overlap)) {
		pos = memcmpchk(&dst_buffer[dst_end],
				&src_buffer[size - overlap], overlap);
		if (pos < overlap) {
			fprintf(stderr,
				"FAILED: memmove(dst, dst + %td, %#zx) set byte at dst + %td to %#x (3a, should be %#x, %#zx, %#zx)\n",
				overlap, size, size + pos,
				dst_buffer[dst_end + pos],
				src_buffer[size - overlap + pos], pos, overlap);
			exit(2);
		}
	} else if ((overlap < 0) && (size > -overlap)) {
		pos = memcmpchk(&dst_buffer[src_start], src_buffer, -overlap);
		if (pos < -overlap) {
			fprintf(stderr,
				"FAILED: memmove(dst, dst + %td, %#zx) set byte at dst + %td to %#x (3b, should be %#x, %#zx, %#zx)\n",
				overlap, size, overlap + pos,
				dst_buffer[src_start + pos], src_buffer[pos],
				pos, overlap);
			exit(2);
		}
	}

	size_t end = (overlap > 0) ? src_end : dst_end;
	size_t cnt = BUFFER_SIZE - end;
	pos	   = memchk(&dst_buffer[end], INIT_BYTE, cnt);
	if (pos < cnt) {
		fprintf(stderr,
			"FAILED: memmove(dst, dst + %td, %#zx) set byte at dst + %#zx to %#x (4)\n",
			overlap, size, end - dst_start, dst_buffer[end + pos]);
		exit(2);
	}
}

void
memmove_tests(void)
{
	size_t	  size;
	ptrdiff_t overlap;
	printf("Testing memmove...");

	for (size = 0; size <= MAX_SIZE; size++) {
		if ((size % 64) == 0) {
			printf("\n%#5zx: .", size);
		} else {
			printf(".");
		}
		static_assert(BUFFER_PAD <= (2 * LARGE_ALIGN),
			      "Buffer padding too small");
		for (overlap = (-2 * LARGE_ALIGN); overlap <= (2 * LARGE_ALIGN);
		     overlap++) {
			if (overlap == 0) {
				continue;
			}
			memmove_test(size, overlap);
		}
	}
}

int
main(void)
{
	uint64_t dczid;
	__asm__("mrs	%0, dczid_el0" : "=r"(dczid));

	if (dczid != CPU_DCZVA_BITS - 2) {
		fprintf(stderr,
			"ERROR: Unexpected DC ZVA ID: %#x (expected %#x)\n",
			(unsigned int)dczid, CPU_DCZVA_BITS - 2);
		return 1;
	}

	memset_tests();

	for (size_t i = 0; i < BUFFER_SIZE; i++) {
		src_buffer[i] = (uint8_t)random();
	}

	memcpy_tests();

	memmove_tests();

	return 0;
}

// Allow slow memmove() calls from libc.
extern char memcpy_bytes_is_defined_only_in_test_programs;
char	    memcpy_bytes_is_defined_only_in_test_programs;
