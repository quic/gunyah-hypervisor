// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Integrate these tests into unittest configuration

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

	// We assume that we can memset the whole buffer safely... hopefully
	// any bugs in it won't crash the test before we find them!
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

	for (pos = 0; pos < size; pos++) {
		if (dst_buffer[dst_start + pos] ==
		    src_buffer[src_start + pos]) {
			continue;
		}
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

int
main(void)
{
	uint64_t dczid;
	__asm__("mrs	%0, dczid_el0" : "=r"(dczid));

	if (dczid != CPU_DCZVA_BITS - 2) {
		fprintf(stderr, "Unexpected DC ZVA ID: %#x (expected %#x)\n",
			(unsigned int)dczid, CPU_DCZVA_BITS - 2);
		return 1;
	}

	memset_tests();

	for (size_t i = 0; i < BUFFER_SIZE; i++) {
		src_buffer[i] = (uint8_t)random();
	}

	memcpy_tests();

	return 0;
}

// Allow slow memmove() calls from libc.
extern char memcpy_bytes_is_defined_only_in_test_programs;
char	    memcpy_bytes_is_defined_only_in_test_programs;
