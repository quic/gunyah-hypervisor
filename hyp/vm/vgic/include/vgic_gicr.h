// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

// Interfaces used by the VGIC ITS for LPI cache invalidation.

#if VGIC_HAS_LPI
gic_lpi_prop_result_t
vgic_gicr_copy_propbase_one(vic_t *vic, thread_t *gicr_vcpu, irq_t vlpi);

bool
vgic_gicr_copy_propbase_all(vic_t *vic, thread_t *gicr_vcpu,
			    bool zero_remainder);

#if GICV3_HAS_VLPI_V4_1
void
vgic_gicr_rd_invlpi(vic_t *vic, thread_t *gicr_vcpu, virq_t vlpi_num);

void
vgic_gicr_rd_invall(vic_t *vic, thread_t *gicr_vcpu);

bool
vgic_gicr_get_inv_pending(vic_t *vic, thread_t *gicr_vcpu);
#endif
#endif
