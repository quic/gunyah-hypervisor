// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

bool
inject_inst_data_abort(ESR_EL2_t esr_el2, esr_ec_t ec, iss_da_ia_fsc_t fsc,
		       FAR_EL2_t far, vmaddr_t ipa, bool is_data_abort);

void
inject_undef_abort(ESR_EL2_t esr_el2);
