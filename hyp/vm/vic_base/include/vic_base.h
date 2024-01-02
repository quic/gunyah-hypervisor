// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Internal vic module functions that are called by vic_base

// Configure a new virtual interrupt controller object.
error_t
vic_configure(vic_t *vic, count_t max_vcpus, count_t max_virqs,
	      count_t max_msis, bool allow_fixed_vmaddr);

// Attach a new VCPU to an active virtual interrupt controller object.
error_t
vic_attach_vcpu(vic_t *vic, thread_t *vcpu, index_t index);

#if VIC_BASE_FORWARD_PRIVATE
// bind PPI
error_t
vic_bind_private_forward_private(virq_source_t *source, vic_t *vic,
				 thread_t *vcpu, virq_t virq);

void
vic_sync_private_forward_private(virq_source_t *source, vic_t *vic,
				 thread_t *vcpu, virq_t virq, irq_t pirq,
				 cpu_index_t pcpu);

#endif // VIC_BASE_FORWARD_PRIVATE
