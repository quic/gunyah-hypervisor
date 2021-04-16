// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// clang-format off
.macro function func:req local="global" section="section" align="align"
	.ifeqs "\local", "local"
	.else
	.ifeqs "\local", "global"
		.global \func
	.else
		.err "invalid argument to function macro"
	.endif
	.endif
	.ifeqs "\section", "section"
		.section .text.\func, "ax", @progbits
	.else
	.ifeqs "\section", "nosection"
	.else
		.err "invalid argument to function macro"
	.endif
	.endif
	.ifeqs "\align", "align"
		.balign 8
	.else
	.ifeqs "\align", "noalign"
	.else
		.balign \align
	.endif
	.endif

	.type	\func, STT_FUNC
\func:
.endm

.macro function_end func:req
	.size \func, . - \func
.endm

.macro function_chain prev_func:req next_func:req
	function_end \prev_func
	function \next_func, section=nosection align=noalign
.endm

#define LOCAL(name) .L##name

.macro	local, label
.L\label
.endm

.macro _data_sym sym:req size:req local="global" align="noalign"
	.ifeqs "\local", "local"
	.else
	.ifeqs "\local", "global"
		.global \sym
	.else
		.err "invalid argument to _data_sym macro"
	.endif
	.endif
	.ifeqs "\align", "noalign"
	.else
		.balign \align
	.endif
	.type	\sym, STT_OBJECT
	.size	\sym, \size
.endm

.macro _const sym:req size:req align:req local
	.section .rodata.\sym, "a", @progbits
	_data_sym \sym, size=\size, align=\align, local=\local
\sym:
.endm

.macro _data sym:req size:req align:req local
	.section .data.\sym, "aw", @progbits
	_data_sym \sym, size=\size, align=\align, local=\local
\sym:
.endm

.macro _bss sym:req size:req align:req local
	.section .bss.\sym, "aw", @nobits
	_data_sym \sym, size=\size, align=\align, local=\local
\sym:
.endm

.macro const32 sym, value=0, local="global"
	_const \sym, 4, 4, \local
	.4byte \value
	.previous
.endm

.macro const64 sym, value=0, local="global"
	_const \sym, 8, 8, \local
	.8byte \value
	.previous
.endm

.macro data32 sym, value=0, local="global"
	_data \sym, 4, 4, \local
	.4byte \value
	.previous
.endm

.macro data64 sym, value=0, local="global"
	_data \sym, 8, 8, \local
	.8byte \value
	.previous
.endm

.macro bss32 sym, local="global"
	_bss \sym, 4, 4, \local
	.4byte 0
	.previous
.endm

.macro bss64 sym, local="global"
	_bss \sym, 8, 8, \local
	.8byte 0
	.previous
.endm

	  // clang-format on
