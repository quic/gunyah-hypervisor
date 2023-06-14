// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <asm/asm_defs.inc>
#include <asm/cpu.h>
#include <asm/panic.inc>

#if defined(ARCH_ARM_FEAT_BTI)
// Two instructions per jump
#define JUMP_SHIFT	3
#else
#define JUMP_SHIFT	2
#endif

// memcpy of no more than 31 bytes
function memcpy_below32
	// Assume the target is size-aligned and do the largest copies first.
	tbz	x2, 4, LOCAL(memcpy_below16)
	ldp	x3, x4, [x1], 16
	stp	x3, x4, [x0], 16
local memcpy_below16:
	tbz	x2, 3, 1f
	ldr	x3, [x1], 8
	str	x3, [x0], 8
1:
	tbz	x2, 2, 1f
	ldr	w3, [x1], 4
	str	w3, [x0], 4
1:
	tbz	x2, 1, 1f
	ldrh	w3, [x1], 2
	strh	w3, [x0], 2
1:
	tbz	x2, 0, 1f
	ldrb	w3, [x1]
	strb	w3, [x0]
1:
	ret
function_end memcpy_below32


// memcpy of at least 31 bytes (i.e. large enough to align up to 16 and do at
// least one 16-byte copy).
function memcpy_alignable
	// Align up the target address to 16. We know that the size (x2) is at
	// least 16 here, so we don't have to check it during this alignment.
	tbz	x0, 0, 1f
	ldrb	w3, [x1], 1
	sub	x2, x2, 1
	strb	w3, [x0], 1
1:
	tbz	x0, 1, 1f
	ldrh	w3, [x1], 2
	sub	x2, x2, 2
	strh	w3, [x0], 2
1:
	tbz	x0, 2, 1f
	ldr	w3, [x1], 4
	sub	x2, x2, 4
	str	w3, [x0], 4
1:
	tbz	x0, 3, 1f
	ldr	x3, [x1], 8
	sub	x2, x2, 8
	str	x3, [x0], 8
1:
	// At this point we've copied up to 15 bytes, so we know there are at
	// least 16 left. We can safely fall through to _align16.
	prfm	pldl1strm, [x1]
	prfm	pstl1keep, [x0]
function_chain memcpy_alignable, memcpy_align16
	// Use 128-byte chunks to do the copy, because we have 16 usable
	// temporary registers. Copy at least two chunks, so we can unroll the
	// loop and store one chunk while loading the next. Note that this is
	// slightly suboptimal for the A55, which can only dispatch one load
	// and one store each cycle.
#if CPU_MEMCPY_STRIDE != 0x100
#error CPU_MEMCPY_STRIDE is defined incorrectly
#endif

	// The first line was prefetched by the caller; prefetch the rest of
	// the first chunk.
.equ LOCAL(offset), 1 << CPU_L1D_LINE_BITS
.rept (0x80 >> CPU_L1D_LINE_BITS) - 1
	prfm	pldl1strm, [x1, LOCAL(offset)]
	prfm	pstl1keep, [x0, LOCAL(offset)]
.equ LOCAL(offset), LOCAL(offset) +  (1 << CPU_L1D_LINE_BITS)
.endr

	// If we don't have at least two chunks to copy, skip the loop.
	cmp	x2, 0x100
	b.lt	LOCAL(memcpy_below256)

	// Prefetch loads for the second chunk.
.rept (0x80 >> CPU_L1D_LINE_BITS)
	prfm	pldl1strm, [x1, LOCAL(offset)]
.equ LOCAL(offset), LOCAL(offset) +  (1 << CPU_L1D_LINE_BITS)
.endr

	// Offset the destination pointer so that we can put an 0x80 byte
	// post-index writeback on the last store in the loop.
	add	x0, x0, 0x70

	// Pre-calculate the size after the minimal copy, so that we can
	// decrement it by 0x80 and check for termination in a single
	// instruction during the copy loop.
	sub	x2, x2, 0x100

	// Load the first chunk. The last load will pre-index writeback the
	// source pointer, which allows us to do the same on the last load in
	// the loop.
	ldp	x3, x4, [x1, 0x0]
	ldp	x5, x6, [x1, 0x10]
	ldp	x7, x8, [x1, 0x20]
	ldp	x9, x10, [x1, 0x30]
	ldp	x11, x12, [x1, 0x40]
	ldp	x13, x14, [x1, 0x50]
	ldp	x15, x16, [x1, 0x60]
	ldp	x17, x18, [x1, 0x70]!

	// Loop storing chunk n while loading chunk n+1
local memcpy_chunk128_loop:
	// Reduce size by 128 (up to the end of chunk n+2); if it is still
	// positive we will continue the loop after chunk n+1 is loaded.
	subs	x2, x2, 0x80

	// Prefetch loads for chunk n+2 and stores for chunk n+1
.equ LOCAL(offset), 0
.rept (0x80 >> CPU_L1D_LINE_BITS)
	prfm	pstl1keep, [x0, LOCAL(offset) + 0x10]
	prfm	pldl1strm, [x1, LOCAL(offset) + 0x90]
.equ LOCAL(offset), LOCAL(offset) +  (1 << CPU_L1D_LINE_BITS)
.endr

	// Interleave the stores and loads; increment the pointers by 0x80
	// using the last instructions in the loop, to minimise stalls waiting
	// for the writebacks.
	stp	x3, x4, [x0, -0x70]
	ldp	x3, x4, [x1, 0x10]
	stp	x5, x6, [x0, -0x60]
	ldp	x5, x6, [x1, 0x20]
	stp	x7, x8, [x0, -0x50]
	ldp	x7, x8, [x1, 0x30]
	stp	x9, x10, [x0, -0x40]
	ldp	x9, x10, [x1, 0x40]
	stp	x11, x12, [x0, -0x30]
	ldp	x11, x12, [x1, 0x50]
	stp	x13, x14, [x0, -0x20]
	ldp	x13, x14, [x1, 0x60]
	stp	x15, x16, [x0, -0x10]
	ldp	x15, x16, [x1, 0x70]
	stp	x17, x18, [x0], 0x80
	ldp	x17, x18, [x1, 0x80]!

	// If the result of the subs above was non-negative then there's
	// another chunk to do
	b.ge	LOCAL(memcpy_chunk128_loop)

	// Prefetch stores for the chunk after the end of the loop.
.equ LOCAL(offset), 0
.rept (0x80 >> CPU_L1D_LINE_BITS)
	prfm	pstl1keep, [x0, LOCAL(offset) + 0x10]
.equ LOCAL(offset), LOCAL(offset) +  (1 << CPU_L1D_LINE_BITS)
.endr
	// The size has an offset of -128 at this point; undo it so we can
	// fall through to the smaller copies below.
	adds	x2, x2, 0x80

	// Store the last chunk.
	stp	x3, x4, [x0, -0x70]
	stp	x5, x6, [x0, -0x60]
	stp	x7, x8, [x0, -0x50]
	stp	x9, x10, [x0, -0x40]
	stp	x11, x12, [x0, -0x30]
	stp	x13, x14, [x0, -0x20]
	stp	x15, x16, [x0, -0x10]
	stp	x17, x18, [x0]

	// If there's nothing left to copy, just return.
	b.eq	LOCAL(return)

	// At this point, we have -16-byte offsets on both pointers; undo them
	// so we can fall through.
	add	x0, x0, 16
	add	x1, x1, 16

local memcpy_below256:
	// If there is at least one chunk left, copy one chunk.
	cmp	x2, 0x80
	blt	LOCAL(memcpy_below128)

	ldp	x3, x4, [x1]
	ldp	x5, x6, [x1, 0x10]
	ldp	x7, x8, [x1, 0x20]
	ldp	x9, x10, [x1, 0x30]
	stp	x3, x4, [x0]
	ldp	x11, x12, [x1, 0x40]
	stp	x5, x6, [x0, 0x10]
	ldp	x13, x14, [x1, 0x50]
	stp	x7, x8, [x0, 0x20]
	ldp	x15, x16, [x1, 0x60]
	stp	x9, x10, [x0, 0x30]
	ldp	x17, x18, [x1, 0x70]
	stp	x11, x12, [x0, 0x40]
	stp	x13, x14, [x0, 0x50]
	stp	x15, x16, [x0, 0x60]
	stp	x17, x18, [x0, 0x70]

	add	x0, x0, 0x80
	add	x1, x1, 0x80
	sub	x2, x2, 0x80

local memcpy_below128:
	// There are up to 7 16-byte blocks left to copy. Calculate jump
	// offsets into an ldp sequence and then an stp sequence to copy the
	// right number of them. Since there are only 7, we have two extra
	// temporary registers: x3 and x4.
	bic	x4, x2, (1 << 4) - 1
	adr	x3, 1f
	add	x0, x0, x4
	sub	x3, x3, x4, lsr #(4 - JUMP_SHIFT)
	add	x1, x1, x4
	sub	x2, x2, x4
	br	x3

0:
	BRANCH_TARGET(j, ldp	x5, x6, [x1, -0x70])
	BRANCH_TARGET(j, ldp	x7, x8, [x1, -0x60])
	BRANCH_TARGET(j, ldp	x9, x10, [x1, -0x50])
	BRANCH_TARGET(j, ldp	x11, x12, [x1, -0x40])
	BRANCH_TARGET(j, ldp	x13, x14, [x1, -0x30])
	BRANCH_TARGET(j, ldp	x15, x16, [x1, -0x20])
	BRANCH_TARGET(j, ldp	x17, x18, [x1, -0x10])
1:
.if	(. - 0b) != (7 * (1 << JUMP_SHIFT))
.error "Jump table error"
.endif
	BRANCH_TARGET(j,)

	adr	x3, 1f
	sub	x3, x3, x4, lsr #(4 - JUMP_SHIFT)
	br	x3

0:
	BRANCH_TARGET(j, stp	x5, x6, [x0, -0x70])
	BRANCH_TARGET(j, stp	x7, x8, [x0, -0x60])
	BRANCH_TARGET(j, stp	x9, x10, [x0, -0x50])
	BRANCH_TARGET(j, stp	x11, x12, [x0, -0x40])
	BRANCH_TARGET(j, stp	x13, x14, [x0, -0x30])
	BRANCH_TARGET(j, stp	x15, x16, [x0, -0x20])
	BRANCH_TARGET(j, stp	x17, x18, [x0, -0x10])
1:
.if	(. - 0b) != (7 * (1 << JUMP_SHIFT))
.error "Jump table error"
.endif
	BRANCH_TARGET(j,)
	// There must be less than 16 bytes left now.
	cbnz	x2, LOCAL(memcpy_below16)
local return:
	ret
function_end memcpy_align16


// Slow memcpy, used by memmove() when the destination overlaps the source and
// is between 1 and CPU_MEMCPY_STRIDE bytes above it. This should never be
// used in the hypervisor so we don't care about its performance. It exists
// only so the libc references to memmove() in the test program don't crash.
function memcpy_bytes
	cbz	x2, 2f
1:
	subs	x2, x2, 1
	ldrb	w3, [x1], 1
	strb	w3, [x0], 1
	b.ne	1b
2:
	ret
// Force a link failure if this function is used in hypervisor builds
.word	memcpy_bytes_is_defined_only_in_test_programs
function_end memcpy_bytes
