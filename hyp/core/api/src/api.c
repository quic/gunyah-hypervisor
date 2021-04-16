// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <hypcall_def.h>

hypercall_hypervisor_identify_result_t
hypercall_hypervisor_identify(void)
{
	return (hypercall_hypervisor_identify_result_t){
		.hyp_api_info = hyp_api_info_default(),
		.api_flags_0  = hyp_api_flags0_default(),
		.api_flags_1  = hyp_api_flags1_default(),
		.api_flags_2  = hyp_api_flags2_default(),
	};
}
