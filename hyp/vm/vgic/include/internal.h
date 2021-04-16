// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

//
// Debugging
//

#define VGIC_TRACE(id, vic, vcpu, fmt, ...)                                    \
	TRACE(VGIC, VGIC_##id, "{:#x} {:#x} " fmt, (uintptr_t)vic,             \
	      (uintptr_t)vcpu, __VA_ARGS__)

//
// VIRQ routing
//

GICD_IROUTER_t
vgic_get_router(vic_t *vic, virq_t virq);

thread_t *
vgic_get_route(vic_t *vic, virq_t virq);

thread_t *
vgic_get_route_or_owner(vic_t *vic, thread_t *vcpu, virq_t virq);

//
// VIRQ delivery
//

vgic_delivery_state_t
vgic_deliver(virq_t virq, vic_t *vic, thread_t *vcpu, virq_source_t *source,
	     _Atomic vgic_delivery_state_t *dstate,
	     vgic_delivery_state_t assert_dstate, bool is_hw, bool is_private);

bool
vgic_undeliver(vic_t *vic, thread_t *vcpu,
	       _Atomic vgic_delivery_state_t *dstate, virq_t virq,
	       bool hw_detach, vgic_delivery_state_t clear_dstate,
	       bool reclaim);

void
vgic_defer(vic_t *vic, thread_t *vcpu, index_t lr, bool reroute);

void
vgic_deactivate(vic_t *vic, thread_t *vcpu, virq_t virq,
		_Atomic vgic_delivery_state_t *dstate,
		vgic_delivery_state_t old_dstate, bool set_edge);

void
vgic_sync_all(vic_t *vic, bool wakeup);

//
// Utility functions (IRQ types, bit manipulations etc)
//

#define hwirq_from_virq_source(p)                                              \
	(assert(p != NULL),                                                    \
	 assert(p->trigger == VIRQ_TRIGGER_VGIC_FORWARDED_SPI),                \
	 hwirq_container_of_vgic_spi_source(p))

#define fwd_private_from_virq_source(p)                                        \
	(assert(p != NULL),                                                    \
	 assert(p->trigger == VIRQ_TRIGGER_VIC_BASE_FORWARD_PRIVATE),          \
	 vic_forward_private_container_of_source(p))

vgic_irq_type_t
vgic_get_irq_type(virq_t irq);

bool
vgic_irq_is_private(virq_t virq);

bool
vgic_irq_is_spi(virq_t virq);

bool
vgic_irq_is_ppi(virq_t virq);

virq_source_t *
vgic_find_source(vic_t *vic, thread_t *vcpu, virq_t virq);

_Atomic vgic_delivery_state_t *
vgic_find_dstate(vic_t *vic, thread_t *vcpu, virq_t virq);

bool
vgic_delivery_state_is_level_asserted(const vgic_delivery_state_t *x);

bool
vgic_delivery_state_is_pending(const vgic_delivery_state_t *x);

void
vgic_read_lr_state(index_t i);

//
// Register trap handlers
//

// GICD
void
vgic_gicd_set_control(vic_t *vic, GICD_CTLR_DS_t ctlr);

void
vgic_gicd_set_statusr(vic_t *vic, GICD_STATUSR_t statusr, bool set);

void
vgic_gicd_change_irq_pending(vic_t *vic, irq_t irq_num, bool set, bool is_msi);

void
vgic_gicd_change_irq_enable(vic_t *vic, irq_t irq_num, bool set);

void
vgic_gicd_change_irq_active(vic_t *vic, irq_t irq_num, bool set);

void
vgic_gicd_set_irq_group(vic_t *vic, irq_t irq_num, bool is_group_1);

void
vgic_gicd_set_irq_priority(vic_t *vic, irq_t irq_num, uint8_t priority);

void
vgic_gicd_set_irq_config(vic_t *vic, irq_t irq_num, bool is_edge);

void
vgic_gicd_set_irq_router(vic_t *vic, irq_t irq_num, uint8_t aff0, uint8_t aff1,
			 uint8_t aff2, uint8_t aff3, bool is_1n);

// GICR
thread_t *
vgic_get_thread_by_gicr_index(vic_t *vic, index_t gicr_num);

void
vgic_gicr_rd_set_control(vic_t *vic, thread_t *gicr_vcpu, GICR_CTLR_t ctlr);

void
vgic_gicr_rd_set_statusr(thread_t *gicr_vcpu, GICR_STATUSR_t statusr, bool set);

void
vgic_gicr_rd_set_wake(vic_t *vic, thread_t *gicr_vcpu, bool processor_sleep);

void
vgic_gicr_sgi_change_sgi_ppi_enable(vic_t *vic, thread_t *gicr_vcpu,
				    irq_t irq_num, bool set);

void
vgic_gicr_sgi_change_sgi_ppi_pending(vic_t *vic, thread_t *gicr_vcpu,
				     irq_t irq_num, bool set);

void
vgic_gicr_sgi_change_sgi_ppi_active(vic_t *vic, thread_t *gicr_vcpu,
				    irq_t irq_num, bool set);

void
vgic_gicr_sgi_set_sgi_ppi_group(vic_t *vic, thread_t *gicr_vcpu, irq_t irq_num,
				bool is_group_1);

void
vgic_gicr_sgi_set_sgi_ppi_priority(vic_t *vic, thread_t *gicr_vcpu,
				   irq_t irq_num, uint8_t priority);

void
vgic_gicr_sgi_set_ppi_config(vic_t *vic, thread_t *gicr_vcpu, irq_t irq_num,
			     bool is_edge);

// GICC
void
vgic_icc_irq_deactivate(vic_t *vic, irq_t irq_num);

void
vgic_icc_generate_sgi(vic_t *vic, ICC_SGIR_EL1_t sgir, bool is_group_1);
