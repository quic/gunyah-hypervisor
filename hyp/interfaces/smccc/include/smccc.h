// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

void
smccc_1_1_call(smccc_function_id_t fn_id, uint64_t (*args)[6],
	       uint64_t (*ret)[4], uint64_t *session_ret, uint32_t client_id);
