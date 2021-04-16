// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Internal vic module functions that are called by vic_base

// Configure a new virtual interrupt controller object.
error_t
vic_configure(vic_t *vic, count_t max_vcpus, count_t max_virqs);

// Attach a new VCPU to an active virtual interrupt controller object.
error_t
vic_attach_vcpu(vic_t *vic, thread_t *vcpu, index_t index);

#if VIC_BASE_FORWARD_PRIVATE
// Copy the given percpu VIRQ's state (enable bits, triggering, etc) to the
// given physical percpu IRQ (or vice versa for hardwired configuration).
void
vic_claim_private(vic_t *vic, vcpu_t *vcpu, virq_t virq, cpu_index_t pcpu,
		  irq_t pirq);

// Tear down any configuration that was done by the function above.
void
vic_release_private(vic_t *vic, vcpu_t *vcpu, virq_t virq, cpu_index_t pcpu,
		    irq_t pirq);

// bind PPI
error_t
vic_bind_private_forward_private(vic_forward_private_t *forward_private,
				 vic_t *vic, thread_t *vcpu, virq_t virq,
				 irq_t pirq, cpu_index_t pcpu);

#endif // VIC_BASE_FORWARD_PRIVATE
