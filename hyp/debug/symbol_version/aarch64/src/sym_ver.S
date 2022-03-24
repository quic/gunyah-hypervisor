// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hypsymversion.h>

#include <asm/asm_defs.inc>

// .text.debug is a dummy section used to KEEP any symbols that may need to be
// externally visible and not optimized by LTO - usually for debuggers
.section .text.debug, "ax", @progbits
	adrp x0, hyp_sym_version_pointer

// We use a random value as the symbols version, put it directly in a symbol and
// in memory. The external debugger reads the values of this symbol and the
// memory. If they don't match it means the user has loaded the wrong symbols.
.equ	hyp_sym_version, HYP_SYM_VERSION
const64 hyp_sym_version_pointer, HYP_SYM_VERSION
