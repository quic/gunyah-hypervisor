// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

error_t
vcpu_bind_virq(thread_t *vcpu, vic_t *vic, virq_t virq,
	       vcpu_virq_type_t virq_type);

error_t
vcpu_unbind_virq(thread_t *vcpu, vcpu_virq_type_t virq_type);
