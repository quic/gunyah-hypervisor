// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

.macro panic, panic_str:req
.pushsection .rodata.panic_str\@, "aMS", @progbits, 1
local panic_str\@:
	.asciz	"\panic_str"
.popsection
	adr	x0, LOCAL(panic_str\@)
	bl	panic
.endm
