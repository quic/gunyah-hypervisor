// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

void
arm_vm_timer_init(arm_vm_timer_type_t tt);

bool
arm_vm_timer_get_is_expired(arm_vm_timer_type_t tt);

bool
arm_vm_timer_is_irq_enabled(arm_vm_timer_type_t tt);

bool
arm_vm_timer_is_irq_pending(arm_vm_timer_type_t tt);

void
arm_vm_timer_load_state(thread_t *thread);

void
arm_vm_timer_cancel_timeout(arm_vm_timer_type_t tt);

uint32_t
arm_vm_timer_get_freqeuncy(void);

ticks_t
arm_vm_timer_get_ticks(void);

ticks_t
arm_vm_timer_get_timeout(arm_vm_timer_type_t tt);

// Checks the timer control register in a thread's saved context.
// Returns true if the timer is enabled and its interrupt is not masked.
bool
arm_vm_timer_is_irq_enabled_thread(thread_t *thread, arm_vm_timer_type_t tt);

ticks_t
arm_vm_timer_get_timeout_thread(thread_t *thread, arm_vm_timer_type_t tt);

void
arm_vm_timer_arch_timer_hw_irq_activated(arm_vm_timer_type_t tt);

void
arm_vm_timer_arch_timer_hw_irq_deactivate(arm_vm_timer_type_t tt);
