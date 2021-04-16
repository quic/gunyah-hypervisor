// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

void
arm_vm_timer_init(void);

bool
arm_vm_timer_get_is_expired(void);

bool
arm_vm_timer_is_irq_enabled(void);

bool
arm_vm_timer_is_irq_pending(void);

void
arm_vm_timer_load_state(thread_t *thread);

void
arm_vm_timer_cancel_timeout(void);

uint32_t
arm_vm_timer_get_freqeuncy(void);

ticks_t
arm_vm_timer_get_ticks(void);

ticks_t
arm_vm_timer_get_timeout(void);

// Checks the timer control register in a thread's saved context.
// Returns true if the timer is enabled and its interrupt is not masked.
bool
arm_vm_timer_is_irq_enabled_thread(thread_t *thread);

ticks_t
arm_vm_timer_get_timeout_thread(thread_t *thread);

void
arm_vm_timer_arch_timer_hw_irq_activated(void);

void
arm_vm_timer_arch_timer_hw_irq_deactivate(void);
