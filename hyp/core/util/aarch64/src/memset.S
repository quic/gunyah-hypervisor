// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <asm/asm_defs.inc>
#include <asm/cpu.h>
#include <asm/panic.inc>


#if defined(ARCH_ARM_8_5_BTI)
// Two instructions per jump
#define JUMP_SHIFT	3
#else
#define JUMP_SHIFT	2
#endif

// memset to zeros of no more than 31 bytes
function memset_zeros_below32
	// Assume the target is size-aligned and do the largest stores first.
	tbz	x1, 4, LOCAL(memset_zeros_below16)
	stp	xzr, xzr, [x0], 16
local memset_zeros_below16:
	tbz	x1, 3, 1f
	str	xzr, [x0], 8
1:
	tbz	x1, 2, 1f
	str	wzr, [x0], 4
1:
	tbz	x1, 1, 1f
	strh	wzr, [x0], 2
1:
	tbz	x1, 0, 1f
	strb	wzr, [x0]
1:
	ret
function_end memset_zeros_below32

// memset to zeros of at least 31 bytes (i.e. large enough to align up to 16
// and do at least one 16-byte copy).
function memset_zeros_alignable
	// Align up the target address to 16. We know that the size (x1) is at
	// least 16 here, so we don't have to check it during this alignment.
	tbz	x0, 0, 1f
	strb	wzr, [x0], 1
	sub	x1, x1, 1
1:
	tbz	x0, 1, 1f
	strh	wzr, [x0], 2
	sub	x1, x1, 2
1:
	tbz	x0, 2, 1f
	str	wzr, [x0], 4
	sub	x1, x1, 4
1:
	tbz	x0, 3, 1f
	str	xzr, [x0], 8
	sub	x1, x1, 8
1:
	// At this point we've cleared up to 15 bytes, so we know there are at
	// least 16 left. We can safely fall through to _align16.
function_chain memset_zeros_alignable, memset_zeros_align16
	// Determine how many stores need to be done to align to DCZVA_BITS,
	// and also whether the remaining size is less than a DC ZVA block.
	neg	x3, x0
	cmp	x1, 1 << CPU_DCZVA_BITS
	and	x2, x3, ((1 << CPU_DCZVA_BITS) - 1)

	// If we have less than a DC ZVA block left to copy, don't align up.
	b.lt	LOCAL(memset_zeros_no_dczva)

	// Align up to the DC ZVA size by calculating a jump into an stp
	// sequence based on the number of 16-byte chunks needed for alignment
	// (which may be 0). By the time we finish this, we might again have
	// less than one DC ZVA block left, so redo the comparison.
	adr	x5, 1f
	sub	x1, x1, x2
	add	x0, x0, x2
	sub	x5, x5, x2, lsr #(4 - JUMP_SHIFT)
	cmp	x1, 1 << CPU_DCZVA_BITS
	br	x5
0:
.equ LOCAL(offset), 16 - (1 << CPU_DCZVA_BITS)
.rept (1 << (CPU_DCZVA_BITS - 4) - 1)
	BRANCH_TARGET(j, stp	xzr, xzr, [x0, LOCAL(offset)])
.equ LOCAL(offset), LOCAL(offset) + 16
.endr
1:
.if	(. - 0b) != (((1 << (CPU_DCZVA_BITS - 4)) - 1) * (1 << JUMP_SHIFT))
.error "Jump table error"
.endif
	BRANCH_TARGET(j,)
	b.lt	LOCAL(memset_zeros_no_dczva)
function_chain memset_zeros_align16, memset_zeros_dczva
	bic	x2, x1, ((1 << CPU_DCZVA_BITS) - 1)
	and	x1, x1, ((1 << CPU_DCZVA_BITS) - 1)
1:
	subs	x2, x2, (1 << CPU_DCZVA_BITS)
	dc	zva, x0
	add	x0, x0, (1 << CPU_DCZVA_BITS)
	b.ne	1b

	cbz	x1, LOCAL(return)

local memset_zeros_no_dczva:
	// Less than one DC ZVA block left, so calculate a jump into an stp
	// sequence based on the number of remaining 16-byte chunks.
	bic	x4, x1, (1 << 4) - 1
	adr	x5, 1f
	sub	x1, x1, x4
	sub	x5, x5, x4, lsr #(4 - JUMP_SHIFT)
	add	x0, x0, x4
	br	x5
0:
.equ LOCAL(offset), 16 - (1 << CPU_DCZVA_BITS)
.rept (1 << (CPU_DCZVA_BITS - 4)) - 1
	BRANCH_TARGET(j, stp	xzr, xzr, [x0, LOCAL(offset)])
.equ LOCAL(offset), LOCAL(offset) + 16
.endr
1:
.if	(. - 0b) != (((1 << (CPU_DCZVA_BITS - 4)) - 1) * (1 << JUMP_SHIFT))
.error "Jump table error"
.endif
	BRANCH_TARGET(j,)
	// There must be less than 16 bytes left now.
	cbnz	x1, LOCAL(memset_zeros_below16)
local return:
	ret
function_end memset_zeros_dczva


// memset to nonzero of no more than 31 bytes. Note that x1 has been expanded
// to contain 8 copies of the nonzero byte to set.
function memset_below32
	// Assume the target is size-aligned and do the largest stores first.
	tbz	x2, 4, LOCAL(memset_below16)
	stp	x1, x1, [x0], 16
local memset_below16:
	tbz	x2, 3, 1f
	str	x1, [x0], 8
1:
	tbz	x2, 2, 1f
	str	w1, [x0], 4
1:
	tbz	x2, 1, 1f
	strh	w1, [x0], 2
1:
	tbz	x2, 0, 1f
	strb	w1, [x0]
1:
	ret
function_end memset_below32

// memset to nonzero of at least 31 bytes (i.e. large enough to align up to 16
// and do at least one 16-byte copy). For the _align16 entry point, x1 has
// been expanded to contain at 8 copies of the nonzero byte to set; for the
// _alignable entry point, it has not been expanded yet, but has been
// zero-extended from 8 bits if necessary.
function memset_alignable
	// Align up the target address to 16, and simultaneously duplicate the
	// byte to set until we have 16 copies of it. We know that the size
	// (x2) is at least 16 here, so we don't have to check it during this
	// alignment.
	orr	x3, x1, x1, lsl 8
	tbz	x0, 0, 1f
	strb	w1, [x0], 1
	sub	x2, x2, 1
1:
	orr	x1, x3, x3, lsl 16
	tbz	x0, 1, 1f
	strh	w3, [x0], 2
	sub	x2, x2, 2
1:
	orr	x3, x1, x1, lsl 32
	tbz	x0, 2, 1f
	str	w1, [x0], 4
	sub	x2, x2, 4
1:
	mov	x1, x3
	tbz	x0, 3, 1f
	str	x3, [x0], 8
	sub	x2, x2, 8
1:
	// At this point we've cleared up to 15 bytes, so we know there are at
	// least 16 left; also we have expanded x1. We can safely fall through
	// to _align16.
	prfm	pstl1keep, [x0]
function_chain memset_alignable, memset_align16
	// Use the larger of 128-byte chunks or cache lines for large copies
.equ LOCAL(chunk), 0x80
.ifge (1 << CPU_L1D_LINE_BITS) - LOCAL(chunk)
.equ LOCAL(chunk), 1 << CPU_L1D_LINE_BITS
.endif
	// The first line was prefetched by the caller; prefetch the rest of
	// the first chunk.
.equ LOCAL(offset), 1 << CPU_L1D_LINE_BITS
.rept (LOCAL(chunk) >> CPU_L1D_LINE_BITS) - 1
	prfm	pstl1keep, [x0, LOCAL(offset)]
.equ LOCAL(offset), LOCAL(offset) +  (1 << CPU_L1D_LINE_BITS)
.endr

	// Unrolled loop copying chunks
	subs	x3, x2, LOCAL(chunk)
	and	x2, x2, LOCAL(chunk) - 1
	b.lt	2f
1:
	// Prefetch the next chunk.
.rept (LOCAL(chunk) >> CPU_L1D_LINE_BITS)
	prfm	pstl1keep, [x0, LOCAL(offset)]
.equ LOCAL(offset), LOCAL(offset) +  (1 << CPU_L1D_LINE_BITS)
.endr
	// Write the chunk with a sequence of 16-byte stores.
.equ LOCAL(offset), 0
.rept (LOCAL(chunk) / 0x10)
	stp	x1, x1, [x0, LOCAL(offset)]
.equ LOCAL(offset), LOCAL(offset) + 0x10
.endr
	subs	x3, x3, LOCAL(chunk)
	add	x0, x0, LOCAL(chunk)
	b.ge	1b
2:
	// Calculated jump for up to 7 16-byte chunks.
	lsr	x4, x2, 4
	adr	x5, 1f
	sub	x2, x2, x4, lsl 4
	add	x0, x0, x4, lsl 4
	sub	x6, x5, x4, lsl JUMP_SHIFT
	br	x6
.equ LOCAL(offset), 0x10 - LOCAL(chunk)
.rept (LOCAL(chunk) / 0x10) - 1
	BRANCH_TARGET(j, stp	x1, x1, [x0, LOCAL(offset)])
.equ LOCAL(offset), LOCAL(offset) + 0x10
.endr
1:
	// There must be less than 16 bytes left now.
	BRANCH_TARGET(j,)
	cbnz	x2, LOCAL(memset_below16)
	ret
function_end memset_align16

// Optimize memset_s to perform size, alignment and store zero checks and call
// optimized functions above.
function memset_s
	cbz	x0, LOCAL(memset_s_null)
	uxtb	w2, w2
	cbz	x3, LOCAL(out)
	cmp	x3, x1
	bhi	LOCAL(memset_s_size)
	// Note, 15.99MiB is smaller than RSIZE_MAX, its the largest immediate
	cmp	x1, 4095, lsl 12
	b.hi	LOCAL(memset_s_size)

	cmp	x3, 8
	blo	3f
	// Copy is at least 8 bytes, assume pointers are aligned

	// Vector duplicate the byte value in x2
	orr	x2, x2, x2, lsl 8
	orr	x2, x2, x2, lsl 16
	orr	x2, x2, x2, lsl 32

	bic	x1, x3, 7
	// Copy 8 bytes per loop
2:
	str	x2, [x0], #8
	subs	x1, x1, 8
	b.gt	2b

	and	x3, x3, 7
	cbnz	x3, 3f
local out:
	mov	x0, xzr
	ret

3:	// Slow copy 1 byte per loop
	strb	w2, [x0], #1
	subs	x3, x3, 1
	b.ne	3b

	b	LOCAL(out)

local memset_s_null:
	panic   "memset_s: null pointer"
local memset_s_size:
	panic   "memset_s: bad size"
function_end memset_s
