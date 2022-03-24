// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

//
// Debugging
//

#define VGIC_TRACE(id, vic, vcpu, fmt, ...)                                    \
	TRACE(VGIC, VGIC_##id, "{:#x} {:#x} " fmt, (uintptr_t)vic,             \
	      (uintptr_t)vcpu, __VA_ARGS__)

#define VGIC_DEBUG_TRACE(id, vic, vcpu, fmt, ...)                              \
	TRACE(VGIC_DEBUG, VGIC_##id, "{:#x} {:#x} " fmt, (uintptr_t)vic,       \
	      (uintptr_t)vcpu, __VA_ARGS__)

//
// VIRQ routing and delivery
//

thread_t *
vgic_get_route_from_state(vic_t *vic, vgic_delivery_state_t dstate,
			  bool use_local_vcpu);

thread_t *
vgic_get_route_for_spi(vic_t *vic, virq_t virq, bool use_local_vcpu);

thread_t *
vgic_find_target(vic_t *vic, virq_source_t *source);

vgic_delivery_state_t
vgic_deliver(virq_t virq, vic_t *vic, thread_t *vcpu, virq_source_t *source,
	     _Atomic vgic_delivery_state_t *dstate,
	     vgic_delivery_state_t assert_dstate, bool is_private)
	EXCLUDE_SCHEDULER_LOCK(vcpu);

bool
vgic_undeliver(vic_t *vic, thread_t *vcpu,
	       _Atomic vgic_delivery_state_t *dstate, virq_t virq,
	       vgic_delivery_state_t clear_dstate, bool check_route)
	EXCLUDE_SCHEDULER_LOCK(vcpu);

void
vgic_undeliver_all(vic_t *vic, thread_t *vcpu) EXCLUDE_SCHEDULER_LOCK(vcpu);

void
vgic_deactivate(vic_t *vic, thread_t *vcpu, virq_t virq,
		_Atomic vgic_delivery_state_t *dstate,
		vgic_delivery_state_t old_dstate, bool set_edge,
		bool set_hw_active);

void
vgic_sync_all(vic_t *vic, bool wakeup);

void
vgic_update_enables(vic_t *vic, GICD_CTLR_DS_t gicd_ctlr);

void
vgic_retry_unrouted(vic_t *vic);

cpu_index_t
vgic_lr_owner_lock(thread_t *vcpu) ACQUIRE_LOCK(vcpu->vgic_lr_owner_lock)
	ACQUIRE_PREEMPT_DISABLED;

cpu_index_t
vgic_lr_owner_lock_nopreempt(thread_t *vcpu)
	ACQUIRE_LOCK(vcpu->vgic_lr_owner_lock) REQUIRE_PREEMPT_DISABLED;

void
vgic_lr_owner_unlock(thread_t *vcpu) RELEASE_LOCK(vcpu->vgic_lr_owner_lock)
	RELEASE_PREEMPT_DISABLED;

void
vgic_lr_owner_unlock_nopreempt(thread_t *vcpu)
	RELEASE_LOCK(vcpu->vgic_lr_owner_lock) REQUIRE_PREEMPT_DISABLED;

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

_Atomic(vgic_delivery_state_t) *
vgic_find_dstate(vic_t *vic, thread_t *vcpu, virq_t virq);

bool
vgic_delivery_state_is_level_asserted(const vgic_delivery_state_t *x);

bool
vgic_delivery_state_is_pending(const vgic_delivery_state_t *x);

void
vgic_read_lr_state(index_t i);

bool
vgic_has_lpis(vic_t *vic);

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

#if GICV3_HAS_GICD_ICLAR
void
vgic_gicd_set_irq_classes(vic_t *vic, irq_t irq_num, bool class0, bool class1);
#endif

// GICR
thread_t *
vgic_get_thread_by_gicr_index(vic_t *vic, index_t gicr_num);

void
vgic_gicr_rd_set_control(vic_t *vic, thread_t *gicr_vcpu, GICR_CTLR_t ctlr);

GICR_CTLR_t
vgic_gicr_rd_get_control(vic_t *vic, thread_t *gicr_vcpu);

void
vgic_gicr_rd_set_statusr(thread_t *gicr_vcpu, GICR_STATUSR_t statusr, bool set);

bool
vgic_gicr_rd_check_sleep(thread_t *gicr_vcpu);

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
vgic_icc_set_group_enable(bool is_group_1, ICC_IGRPEN_EL1_t igrpen);

void
vgic_icc_irq_deactivate(vic_t *vic, irq_t irq_num);

void
vgic_icc_generate_sgi(vic_t *vic, ICC_SGIR_EL1_t sgir, bool is_group_1);
