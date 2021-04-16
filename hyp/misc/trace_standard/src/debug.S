// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause


// .text.debug is a dummy section used to KEEP any symbols that may need to be
// externally visible and not optimized by LTO - usually for debuggers
	.section .text.debug, "ax", @progbits
	adrp	x0, hyp_trace
