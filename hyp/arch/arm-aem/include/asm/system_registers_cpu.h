// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

// AArch64 System Register Encoding (CPU Implementation Defined Registers)
//
// This list is not exhaustive, it contains mostly registers likely to be
// trapped and accessed indirectly.

#define ISS_MRS_MSR_CPUACTLR_EL1   ISS_OP0_OP1_CRN_CRM_OP2(3, 0, 15, 1, 0)
#define ISS_MRS_MSR_CPUACTLR2_EL1  ISS_OP0_OP1_CRN_CRM_OP2(3, 0, 15, 1, 1)
#define ISS_MRS_MSR_CPUACTLR3_EL1  ISS_OP0_OP1_CRN_CRM_OP2(3, 0, 15, 1, 2)
#define ISS_MRS_MSR_CPUACTLR4_EL1  ISS_OP0_OP1_CRN_CRM_OP2(3, 0, 15, 1, 3)
#define ISS_MRS_MSR_CPUACTLR5_EL1  ISS_OP0_OP1_CRN_CRM_OP2(3, 0, 15, 9, 0)
#define ISS_MRS_MSR_CPUACTLR6_EL1  ISS_OP0_OP1_CRN_CRM_OP2(3, 0, 15, 9, 1)
#define ISS_MRS_MSR_CPUECTLR_EL1   ISS_OP0_OP1_CRN_CRM_OP2(3, 0, 15, 1, 4)
#define ISS_MRS_MSR_CPUECTLR2_EL1  ISS_OP0_OP1_CRN_CRM_OP2(3, 0, 15, 1, 5)
#define ISS_MRS_MSR_CPUPWRCTLR_EL1 ISS_OP0_OP1_CRN_CRM_OP2(3, 0, 15, 2, 7)
