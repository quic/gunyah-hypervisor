// Automatically generated. Do not modify.
//
// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

error_code_t
gunyah_hyp_test_hypercall_1(interrupt_control_register_t in0);

error_code_t
gunyah_hyp_test_hypercall_2(uint64_t in0, count_t in1,
			    interrupt_control_register_t in2);

typedef struct gunyah_hyp_test_hypercall_3_result {
	uint64_t _Alignas(register_t) out0;
	count_t _Alignas(register_t) out1;
	uint8_t _pad0[4]; // Pad for struct static zero initialization
	error_code_t _Alignas(register_t) error;
	uint8_t _pad1[4]; // Pad for struct static zero initialization
} gunyah_hyp_test_hypercall_3_result_t;

gunyah_hyp_test_hypercall_3_result_t
gunyah_hyp_test_hypercall_3(uint64_t in0);
