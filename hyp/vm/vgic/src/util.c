// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <atomic.h>
#include <util.h>

#include "internal.h"

vgic_irq_type_t
vgic_get_irq_type(virq_t irq)
{
	vgic_irq_type_t type;

	if (irq < (virq_t)(GIC_SGI_BASE + GIC_SGI_NUM)) {
		type = VGIC_IRQ_TYPE_SGI;
	} else if ((irq >= (virq_t)GIC_PPI_BASE) &&
		   (irq < (virq_t)(GIC_PPI_BASE + GIC_PPI_NUM))) {
		type = VGIC_IRQ_TYPE_PPI;
	} else if ((irq >= (virq_t)GIC_SPI_BASE) &&
		   (irq < (virq_t)(GIC_SPI_BASE + GIC_SPI_NUM))) {
		type = VGIC_IRQ_TYPE_SPI;
	}
#if VGIC_HAS_EXT_IRQS
	else if ((irq >= (virq_t)GIC_PPI_EXT_BASE) &&
		 (irq < (virq_t)(GIC_PPI_EXT_BASE + GIC_PPI_EXT_NUM))) {
		type = VGIC_IRQ_TYPE_PPI_EXT;
	} else if ((irq >= (virq_t)GIC_SPI_EXT_BASE) &&
		   (irq < (virq_t)(GIC_SPI_EXT_BASE + GIC_SPI_EXT_NUM))) {
		type = VGIC_IRQ_TYPE_SPI_EXT;
	}
#endif
#if VGIC_HAS_LPI
	else if (irq >= (virq_t)GIC_LPI_BASE) {
		type = VGIC_IRQ_TYPE_LPI;
	}
#endif
	else {
		type = VGIC_IRQ_TYPE_RESERVED;
	}

	return type;
}

bool
vgic_irq_is_private(virq_t virq)
{
	bool result;
	switch (vgic_get_irq_type(virq)) {
	case VGIC_IRQ_TYPE_SGI:
	case VGIC_IRQ_TYPE_PPI:
		// If adding any classes here (e.g. PPI_EXT) you _must_ audit
		// all callers of this function and fix up their array indexing
		result = true;
		break;
	case VGIC_IRQ_TYPE_SPI:
	case VGIC_IRQ_TYPE_RESERVED:
	default:
		result = false;
		break;
	}
	return result;
}

bool
vgic_irq_is_spi(virq_t virq)
{
	bool result;
	switch (vgic_get_irq_type(virq)) {
	case VGIC_IRQ_TYPE_SPI:
		// If adding any classes here (e.g. SPI_EXT) you _must_ audit
		// all callers of this function and fix up their array indexing
		result = true;
		break;
	case VGIC_IRQ_TYPE_SGI:
	case VGIC_IRQ_TYPE_PPI:
	case VGIC_IRQ_TYPE_RESERVED:
	default:
		result = false;
		break;
	}
	return result;
}

bool
vgic_irq_is_ppi(virq_t virq)
{
	bool result;
	switch (vgic_get_irq_type(virq)) {
	case VGIC_IRQ_TYPE_PPI:
		// If adding any classes here (e.g. PPI_EXT) you _must_ audit
		// all callers of this function and fix up their array indexing
		result = true;
		break;
	case VGIC_IRQ_TYPE_SGI:
	case VGIC_IRQ_TYPE_SPI:
	case VGIC_IRQ_TYPE_RESERVED:
	default:
		result = false;
		break;
	}
	return result;
}

virq_source_t *
vgic_find_source(vic_t *vic, thread_t *vcpu, virq_t virq)
{
	virq_source_t *source;

	// Load the source object pointer for a VIRQ. This must be a load
	// acquire to ensure that this is accessed prior to reading the virq
	// delivery state's level_src bit, because that bit being set should
	// guarantee that this pointer is non-NULL (see vic_unbind()).

	switch (vgic_get_irq_type(virq)) {
	case VGIC_IRQ_TYPE_SPI:
		assert(vic != NULL);
		if ((virq - GIC_SPI_BASE) < vic->sources_count) {
			source = atomic_load_acquire(
				&vic->sources[virq - GIC_SPI_BASE]);
		} else {
			source = NULL;
		}
		break;
	case VGIC_IRQ_TYPE_PPI:
		assert(vcpu != NULL);
		source = atomic_load_acquire(
			&vcpu->vgic_sources[virq - GIC_PPI_BASE]);
		break;
	case VGIC_IRQ_TYPE_SGI:
	case VGIC_IRQ_TYPE_RESERVED:
	default:
		source = NULL;
		break;
	}
	return source;
}

_Atomic vgic_delivery_state_t *
vgic_find_dstate(vic_t *vic, thread_t *vcpu, virq_t virq)
{
	_Atomic vgic_delivery_state_t *dstate;
	switch (vgic_get_irq_type(virq)) {
	case VGIC_IRQ_TYPE_SGI:
	case VGIC_IRQ_TYPE_PPI:
		assert(vcpu != NULL);
		dstate = &vcpu->vgic_private_states[virq];
		break;
	case VGIC_IRQ_TYPE_SPI:
		assert(vic != NULL);
		dstate = &vic->spi_states[virq - GIC_SPI_BASE];
		break;
	case VGIC_IRQ_TYPE_RESERVED:
	default:
		// Invalid IRQ number
		dstate = NULL;
		break;
	}
	return dstate;
}

bool
vgic_delivery_state_is_level_asserted(const vgic_delivery_state_t *x)
{
	return vgic_delivery_state_get_level_sw(x) ||
	       vgic_delivery_state_get_level_msg(x) ||
	       vgic_delivery_state_get_level_src(x);
}

bool
vgic_delivery_state_is_pending(const vgic_delivery_state_t *x)
{
	return vgic_delivery_state_get_cfg_is_edge(x)
		       ? vgic_delivery_state_get_edge(x)
		       : vgic_delivery_state_is_level_asserted(x);
}
