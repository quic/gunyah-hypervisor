// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <smccc.h>

#if defined(SMC_TRACE_ID__MIN)
#include <string.h>

#include <smc_trace.h>
#endif

void
smccc_1_1_call(smccc_function_id_t fn_id, uint64_t (*args)[6],
	       uint64_t (*ret)[4], uint64_t *session_ret, uint32_t client_id)
{
	assert(args != NULL);
	assert(ret != NULL);

#if defined(SMC_TRACE_ID__MIN)
	register_t trace_regs[SMC_TRACE_REG_MAX];

	trace_regs[0] = smccc_function_id_raw(fn_id);
	memscpy(&trace_regs[1], sizeof(trace_regs) - sizeof(trace_regs[0]),
		args, sizeof(*args));
	trace_regs[7] = client_id;

	smc_trace_log(SMC_TRACE_ID_EL2_64CAL, &trace_regs, 8U);
#endif

	register register_t x0 __asm__("x0") = smccc_function_id_raw(fn_id);
	register register_t x1 __asm__("x1") = (*args)[0];
	register register_t x2 __asm__("x2") = (*args)[1];
	register register_t x3 __asm__("x3") = (*args)[2];
	register register_t x4 __asm__("x4") = (*args)[3];
	register register_t x5 __asm__("x5") = (*args)[4];
	register register_t x6 __asm__("x6") = (*args)[5];
	register register_t x7 __asm__("x7") = client_id;

	// Note: In ARM DEN0028B (SMCCC is not versioned), and X4-X17 defined
	// as unpredictable scratch registers and may not be preserved after an
	// SMC call. From ARM DEN0028C, X4-X17 are explicitly required to be
	// preserved. There are three SMCCC versions called out (1.0, 1.1 and
	// 1.2 - DEN 0028C/D) with no mention of the previous defined behaviour,
	// or which version changed to SMC register return semantics. We
	// therefore treat X4-X17 return state as unpredictable here.
	//
	// Note too, the hypervior EL1-EL2 SMCCC interface implemented does
	// preserve unused result registers and temporary registers X4-X17 for
	// future 1.2+ compatibility.
	__asm__ volatile("smc    #0\n"
			 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x6),
			   "+r"(x4), "+r"(x5), "+r"(x7)
			 :
			 : "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
			   "x16", "x17", "memory");

	(*ret)[0] = x0;
	(*ret)[1] = x1;
	(*ret)[2] = x2;
	(*ret)[3] = x3;

	if (session_ret != NULL) {
		*session_ret = x6;
	}
#if defined(SMC_TRACE_ID__MIN)
	memscpy(&trace_regs[0], sizeof(trace_regs), ret, sizeof(*ret));
	trace_regs[4] = 0U;
	trace_regs[5] = 0U;
	trace_regs[6] = x6;

	smc_trace_log(SMC_TRACE_ID_EL2_64RET, &trace_regs, 7U);
#endif
}
