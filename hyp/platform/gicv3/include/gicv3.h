// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// IRQ functions

count_t
gicv3_irq_max(void);

gicv3_irq_type_t
gicv3_get_irq_type(irq_t irq);

bool
gicv3_irq_is_percpu(irq_t irq);

error_t
gicv3_irq_check(irq_t irq);

void
gicv3_irq_enable(irq_t irq);

void
gicv3_irq_enable_local(irq_t irq);

void
gicv3_irq_enable_percpu(irq_t irq, cpu_index_t cpu);

void
gicv3_irq_disable(irq_t irq);

void
gicv3_irq_disable_local(irq_t irq);

void
gicv3_irq_disable_local_nowait(irq_t irq);

void
gicv3_irq_disable_percpu(irq_t irq, cpu_index_t cpu);

void
gicv3_irq_cancel_nowait(irq_t irq);

irq_trigger_result_t
gicv3_irq_set_trigger(irq_t irq, irq_trigger_t trigger);

irq_trigger_result_t
gicv3_irq_set_trigger_percpu(irq_t irq, irq_trigger_t trigger, cpu_index_t cpu);

error_t
gicv3_spi_set_route(irq_t irq, GICD_IROUTER_t route);

irq_result_t
gicv3_irq_acknowledge(void);

void
gicv3_irq_priority_drop(irq_t irq);

void
gicv3_irq_deactivate(irq_t irq);

void
gicv3_irq_deactivate_percpu(irq_t irq, cpu_index_t cpu);

// IPI specific functions

void
gicv3_ipi_others(ipi_reason_t ipi);

void
gicv3_ipi_one(ipi_reason_t ipi, cpu_index_t cpu);

void
gicv3_ipi_clear(ipi_reason_t ipi);
