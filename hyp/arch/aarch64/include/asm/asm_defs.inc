// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <asm-generic/asm_defs.inc>

.macro vector vector_name:req
	function \vector_name, align=128, section=nosection
.endm

.macro vector_end vector_name:req
	function_end \vector_name
	.if (. - \vector_name) > 0x80
	.error "#\vector_name is too big"
	.endif
.endm

.macro	abs64 reg:req val64:req
	movz	\reg, (\val64) & 0xffff
	.ifne ((\val64) >> 16) & 0xffff
	movk	\reg, ((\val64) >> 16) & 0xffff, LSL 16
	.endif
	.ifne ((\val64) >> 32) & 0xffff
	movk	\reg, ((\val64) >> 32) & 0xffff, LSL 32
	.endif
	.ifne ((\val64) >> 48) & 0xffff
	movk	\reg, ((\val64) >> 48) & 0xffff, LSL 48
	.endif
.endm

.macro adrl reg:req sym:req
	adrp	\reg, \sym
	add	\reg, \reg, :lo12:\sym
.endm

.macro	get_tls_base , tls_base:req
	mrs	\tls_base, TPIDR_EL2
.endm

// Get the address of a thread-local symbol
.macro	adr_threadlocal, reg:req, sym:req, tls_base
.ifb \tls_base
	mrs	\reg, TPIDR_EL2
	add	\reg, \reg, :tprel_hi12:\sym
.else
	add	\reg, \tls_base, :tprel_hi12:\sym
.endif
	add	\reg, \reg, :tprel_lo12_nc:\sym
.endm

// Branch target identification helpers
#if defined(ARCH_ARM_8_5_BTI)
#define BRANCH_TARGET(type, ...)	\
	bti	type;			\
	__VA_ARGS__
#else
#define BRANCH_TARGET(type, ...)	\
	__VA_ARGS__
#endif
